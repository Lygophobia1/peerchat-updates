#pragma once
#include "protocol.hpp"
#include "group_key_manager.hpp"
#include <map>
#include <sstream>


namespace peerchat::group_attachments::rpc {
// Local Windows ABI projection only. JSON remains in the existing managed RPC
// client; this bounded value tree conveys its authenticated response to Node.
struct Value {
 unsigned kind=0;std::uint64_t number=0;std::string text;std::vector<Value> array;std::map<std::string,Value> object;
 Value()=default;Value(const char* s):kind(4),text(s){};Value(std::string s):kind(4),text(std::move(s)){};
 explicit Value(std::uint64_t n):kind(3),number(n){};explicit Value(bool b):kind(b?2:1){};
 static Value map(){Value v;v.kind=6;return v;}static Value list(){Value v;v.kind=5;return v;}
 Value& operator[](const std::string& key){protocol::check(kind==6);return object[key];}
 const Value& at(const std::string& key)const{protocol::check(kind==6);auto i=object.find(key);protocol::check(i!=object.end());return i->second;}
 std::string string()const{protocol::check(kind==4);return text;}std::uint64_t integer()const{protocol::check(kind==3);return number;}bool flag()const{protocol::check(kind==1||kind==2);return kind==2;}
 std::string string(const std::string& k)const{return at(k).string();}std::uint64_t integer(const std::string& k)const{return at(k).integer();}
 bool has(const std::string& k)const{return kind==6&&object.count(k);}
};
inline std::string quote(const std::string& s){std::string out="\"";const char* hex="0123456789abcdef";for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=char(c);}return out+'"';}
inline std::string json(const Value& v){switch(v.kind){case 0:return "null";case 1:return "false";case 2:return "true";case 3:return std::to_string(v.number);case 4:return quote(v.text);case 5:{std::string out="[";for(auto& x:v.array){if(out.size()>1)out+=',';out+=json(x);}return out+']';}case 6:{std::string out="{";for(auto& x:v.object){if(out.size()>1)out+=',';out+=quote(x.first)+':'+json(x.second);}return out+'}';}default:throw std::runtime_error("Invalid attachment projection");}}
inline std::string string(protocol::Reader& r,size_t maximum=100000){auto size=r.number();protocol::check(size<=maximum&&size<=r.remaining());std::string value(r.data.begin()+r.cursor,r.data.begin()+r.cursor+size);r.cursor+=size;unicode::decode(value);return value;}
inline Value read(protocol::Reader& r,unsigned depth=0,size_t* nodes=nullptr){size_t local=0;if(!nodes)nodes=&local;protocol::check(depth<=12&&++*nodes<=4096&&r.remaining()>0);Value v;v.kind=r.data[r.cursor++];switch(v.kind){case 0:case 1:case 2:break;case 3:v.number=r.number();protocol::check(v.number<=INT64_MAX);break;case 4:v.text=string(r);break;case 5:{auto n=r.number();protocol::check(n<=512);while(n--)v.array.push_back(read(r,depth+1,nodes));break;}case 6:{auto n=r.number();protocol::check(n<=64);while(n--){auto key=string(r,128);protocol::check(v.object.emplace(key,read(r,depth+1,nodes)).second);}break;}default:protocol::check(false);}return v;}
inline std::string base64(const Blob& bytes,bool url=true){
 const char* alphabet=url?"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_":"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 std::string out;out.reserve((bytes.size()+2)/3*4);
 for(size_t at=0;at<bytes.size();at+=3){auto left=bytes.size()-at;std::uint32_t value=std::uint32_t(bytes[at])<<16;if(left>1)value|=std::uint32_t(bytes[at+1])<<8;if(left>2)value|=bytes[at+2];out+=alphabet[(value>>18)&63];out+=alphabet[(value>>12)&63];if(left>1)out+=alphabet[(value>>6)&63];else if(!url)out+='=';if(left>2)out+=alphabet[value&63];else if(!url)out+='=';}
 return out;
}
inline Blob unbase64(std::string text,size_t max,bool url=true){
 protocol::check(max<=64*1024*1024&&text.size()<=(max*4+2)/3+4);
 const std::string alphabet=url?"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_":"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 Blob result;result.reserve(std::min(max,text.size()/4*3+3));std::uint32_t value=0;unsigned bits=0;
 for(char c:text){if(c=='=')break;auto index=alphabet.find(c);protocol::check(index!=std::string::npos);value=(value<<6)|static_cast<unsigned>(index);bits+=6;if(bits>=8){bits-=8;protocol::check(result.size()<max);result.push_back(static_cast<unsigned char>(value>>bits));}}
 protocol::check(base64(result,url)==text);return result;
}

inline group_keys::RosterSnapshot roster(const Value& v){group_keys::RosterSnapshot s;s.serverGroupId=v.string("groupId");s.legacyGroupId=v.string("legacyGroupId");s.distributorPeerId=v.string("distributorIdentity");s.generation=v.integer("rosterGeneration");s.creationProof=unbase64(v.string("creationProof"),2048,false);auto& rows=v.at("members");protocol::check(rows.kind==5&&rows.array.size()<=256);for(auto& row:rows.array){group_keys::RosterDevice d;d.accountId=row.integer("accountId");d.peerId=row.string("publicIdentity");auto role=row.string("role"),state=row.string("state");protocol::check(role=="owner"||role=="admin"||role=="member");protocol::check(state=="member"||state=="invited");d.role=role=="owner"?GroupRole::Owner:role=="admin"?GroupRole::Admin:GroupRole::Member;d.invited=state=="invited";s.devices.push_back(d);}protocol::check(protocol::id(s.serverGroupId,32)&&protocol::id(s.legacyGroupId,32)&&s.generation&&protocol::id(s.distributorPeerId));return s;}
}
