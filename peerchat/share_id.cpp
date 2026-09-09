#include "share_id.hpp"
#include <windows.h>
#include <limits>
#include <sstream>
namespace peerchat {
static std::string utf8(const std::wstring& s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),out.data(),n,nullptr,nullptr);return out;}
static std::wstring wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);if(n<=0)return {};std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),out.data(),n);return out;}
static std::string b64(const std::string& in){static const char* a="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";std::string o;unsigned v=0;int bits=0;for(unsigned char c:in){v=(v<<8)|c;bits+=8;while(bits>=6){bits-=6;o+=a[(v>>bits)&63];}}if(bits)o+=a[(v<<(6-bits))&63];return o;}
static bool unb64(const std::string& in,std::string& out){unsigned v=0;int bits=0;for(char c:in){int x=c>='A'&&c<='Z'?c-'A':c>='a'&&c<='z'?c-'a'+26:c>='0'&&c<='9'?c-'0'+52:c=='-'?62:c=='_'?63:-1;if(x<0)return false;v=(v<<6)|x;bits+=6;if(bits>=8){bits-=8;out.push_back(char((v>>bits)&255));}}return in.size()%4!=1;}
static bool id(const std::string& s){if(s.size()!=64)return false;for(char c:s)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
std::wstring encodeShareId(const SharedContact& c){if(c.host.empty()||c.host.size()>253||!c.port||!id(c.peerId))return {};std::string p=utf8(c.host)+"\n"+std::to_string(c.port)+"\n"+c.peerId+"\n";return L"P2PCHAT1:"+wide(b64(p));}
bool parseShareId(const std::wstring& value,SharedContact& c,std::wstring& error){error.clear();if(value.rfind(L"P2PCHAT1:",0)!=0){error=L"This is not a PeerChat ID.";return false;}auto b=utf8(value.substr(9));if(b.empty()||b.size()>1400){error=L"The PeerChat ID is empty or too large.";return false;}std::string p;if(!unb64(b,p)){error=L"The PeerChat ID encoding is invalid.";return false;}size_t a=p.find('\n'),d=p.find('\n',a+1),e=p.find('\n',d+1);if(a==std::string::npos||d==std::string::npos||e!=p.size()-1||a==0||d<=a+1){error=L"The PeerChat ID fields are invalid.";return false;}auto host=wide(p.substr(0,a));if(host.empty()||host.size()>253){error=L"The endpoint hostname is invalid.";return false;}unsigned long port=0;try{port=std::stoul(p.substr(a+1,d-a-1));}catch(...){port=0;}if(port==0||port>65535||!id(p.substr(d+1,e-d-1))){error=L"The endpoint or peer identity is invalid.";return false;}c={host,(std::uint16_t)port,p.substr(d+1,e-d-1)};return true;}
}
