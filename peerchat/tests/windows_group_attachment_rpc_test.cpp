#define NOMINMAX
#include "windows_group_attachment_rpc.hpp"
#include <iostream>
#include <limits>
using namespace peerchat;
namespace rpc=peerchat::group_attachments::rpc;
using V=rpc::Value;
void check(bool value){if(!value)throw std::runtime_error("Attachment RPC codec assertion failed");}
template<class F> void rejects(F&& function){bool rejected=false;try{function();}catch(...){rejected=true;}check(rejected);}
// Mirror the managed AttachmentNetworkCodec wire, not its JSON serializer.
void text(protocol::Writer& w,const std::string& value){w.number(value.size());w.data.insert(w.data.end(),value.begin(),value.end());}
void encode(protocol::Writer& w,const V& value){w.data.push_back(static_cast<unsigned char>(value.kind));switch(value.kind){case 0:case 1:case 2:return;case 3:w.number(value.number);return;case 4:text(w,value.text);return;case 5:w.number(value.array.size());for(const auto& child:value.array)encode(w,child);return;case 6:w.number(value.object.size());for(const auto& child:value.object){text(w,child.first);encode(w,child.second);}return;default:return;}}
Blob wire(const V& value){protocol::Writer w(protocol::FileTransfer);encode(w,value);return std::move(w.data);}
V decode(const Blob& bytes){protocol::Reader r(bytes);check(r.type()==protocol::FileTransfer);auto value=rpc::read(r);r.end();return value;}
V roster(){auto value=V::map();value["groupId"]=V(std::string(32,'a'));value["legacyGroupId"]=V(std::string(32,'b'));value["distributorIdentity"]=V(std::string(64,'c'));value["rosterGeneration"]=V(std::uint64_t(7));value["creationProof"]=V(rpc::base64(Blob{1,2,3},false));auto member=V::map();member["accountId"]=V(std::uint64_t(3));member["publicIdentity"]=V(std::string(64,'c'));member["role"]=V("owner");member["state"]=V("member");value["members"]=V::list();value["members"].array.push_back(member);return value;}
int main(){try{
 V value=V::map();value["null"]=V();value["false"]=V(false);value["true"]=V(true);value["integer"]=V(std::uint64_t(INT64_MAX));value["unicode"]=V(u8"📷 日本語\n\"quote\"\\path\t");value["list"]=V::list();value["list"].array={V(std::uint64_t(0)),V("item")};auto encoded=wire(value);auto parsed=decode(encoded);check(rpc::json(parsed)==rpc::json(value));check(parsed.integer("integer")==INT64_MAX&&parsed.at("true").flag()&&!parsed.at("false").flag());check(rpc::quote(std::string("\0\1\n",3))=="\"\\u0000\\u0001\\u000a\"");check(parsed.string("unicode")==value.string("unicode"));rejects([&]{parsed.integer("unicode");});rejects([&]{parsed.at("missing");});
 for(size_t length:{0u,1u,2u,3u,31u,32768u,65536u}){Blob bytes(length);for(size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<unsigned char>(i*29+71);for(bool url:{false,true}){auto coded=rpc::base64(bytes,url);check(rpc::unbase64(coded,length,url)==bytes);if(length)rejects([&]{rpc::unbase64(coded,length-1,url);});if(url)check(coded.find_first_of("+/=\r\n")==std::string::npos);}}
 for(const auto& invalid:{"A","AA=","AA==","AB","AA\n","+A","/A","AA ","AA\t"})rejects([&]{rpc::unbase64(invalid,16);});
 rejects([&]{rpc::unbase64("AA",16,false);});rejects([&]{rpc::unbase64("AB==",16,false);});rejects([&]{rpc::unbase64("AA==\r\n",16,false);});
 Blob maximum(65536,255);V append=V::map();append["type"]=V("cache_append");append["requestId"]=V(std::string(32,'a'));append["lease"]=V(std::string(32,'b'));append["offset"]=V(std::uint64_t(268435456));append["data"]=V(rpc::base64(maximum));check(rpc::json(append).size()<100000);check(decode(wire(append)).string("data")==rpc::base64(maximum));
 // Every truncated value, trailing byte, invalid tag and non-signed-range
 // integer must fail before it can become a controller response.
 for(size_t size=0;size<encoded.size();++size){Blob truncated(encoded.begin(),encoded.begin()+size);rejects([&]{decode(truncated);});}auto trailing=encoded;trailing.push_back(0);rejects([&]{decode(trailing);});V invalid;invalid.kind=255;rejects([&]{decode(wire(invalid));});rejects([&]{decode(wire(V(std::uint64_t(INT64_MAX)+1)));});
 {protocol::Writer w(protocol::FileTransfer);w.data.push_back(6);w.number(2);text(w,"duplicate");w.data.push_back(0);text(w,"duplicate");w.data.push_back(0);rejects([&]{decode(w.data);});}
 for(const auto& badUtf8:{std::string("\xc0\xaf",2),std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4),std::string("\x80",1)})rejects([&]{decode(wire(V(badUtf8)));});
 {V big=V::list();big.array.resize(513);rejects([&]{decode(wire(big));});big.array.resize(512);check(decode(wire(big)).array.size()==512);}
 {V big=V::map();for(unsigned n=0;n<65;++n)big[std::to_string(n)]=V();rejects([&]{decode(wire(big));});big.object.erase("64");check(decode(wire(big)).object.size()==64);big[std::string(129,'k')]=V();rejects([&]{decode(wire(big));});}
 {V nested;for(unsigned n=0;n<12;++n){V parent=V::list();parent.array.push_back(std::move(nested));nested=std::move(parent);}check(decode(wire(nested)).kind==5);V parent=V::list();parent.array.push_back(std::move(nested));rejects([&]{decode(wire(parent));});}
 {V many=V::list();for(unsigned n=0;n<8;++n){V part=V::list();part.array.resize(511);many.array.push_back(std::move(part));}rejects([&]{decode(wire(many));});many.array.back().array.resize(510);check(decode(wire(many)).array.size()==8);}
 {protocol::Writer w(protocol::FileTransfer);w.data.push_back(4);text(w,std::string(100001,'x'));rejects([&]{decode(w.data);});}
 auto good=roster();auto snapshot=rpc::roster(decode(wire(good)));check(snapshot.generation==7&&snapshot.devices.size()==1&&snapshot.devices[0].role==GroupRole::Owner&&!snapshot.devices[0].invited&&snapshot.creationProof==Blob({1,2,3}));
 {auto wrong=good;wrong["groupId"]=V("not-a-group");rejects([&]{rpc::roster(wrong);});wrong=good;wrong["rosterGeneration"]=V(std::uint64_t(0));rejects([&]{rpc::roster(wrong);});wrong=good;wrong["members"].array[0]["role"]=V("superadmin");rejects([&]{rpc::roster(wrong);});wrong=good;wrong["members"].array[0]["state"]=V("revoked");rejects([&]{rpc::roster(wrong);});wrong=good;wrong["members"].array.resize(257,good.at("members").array[0]);rejects([&]{rpc::roster(wrong);});}
 std::cout<<"PASS Windows attachment RPC codec: UTF-8/value-tree roundtrip, canonical base64/url, 64-KiB frame bounds, truncation/trailing/duplicate/depth/node/count/integer rejection, roster projection\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
