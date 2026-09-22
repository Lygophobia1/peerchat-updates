#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifndef WC_ERR_INVALID_CHARS
#define WC_ERR_INVALID_CHARS 0x00000080
#endif
#include "storage.hpp"
#include <stdexcept>

namespace peerchat::protocol {
constexpr size_t MaxPacket=70000;
inline void check(bool condition){if(!condition)throw std::runtime_error("Invalid peer message");}
inline bool id(const std::string& s,size_t n=64){if(s.size()!=n)return false;for(auto c:s)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
enum Type:unsigned char { Hello=1,Text=2,Ack=3,Accepted=4,GroupEvent=5,GroupAck=6,CallOffer=7,CallAnswer=8,CallDecline=9,CallEnd=10,CallBusy=11,CallConnected=12,CallCandidate=13,Handoff=14,CallMediaRestart=15,CallMediaAnswer=16,CallMediaCandidate=17,CallMediaRecoveryRequest=18 };
constexpr std::uint64_t CapabilityMutualTls=1,CapabilityLogicalOrdering=2,CapabilitySignedGroups=4,CapabilityVoiceCalls=8;
constexpr std::uint64_t CapabilityDirectHandoff=16;
// Per-connection intent, advertised only for a deliberate replacement of a
// previously authenticated handoff-capable binding. Not a persistent feature bit.
constexpr std::uint64_t CapabilityHandoffCandidate=32;
constexpr std::uint64_t CapabilityMediaRecovery=64;
inline Blob frame(const Blob& data){check(data.size()<=MaxPacket);Blob out;for(int i=3;i>=0;i--)out.push_back(static_cast<unsigned char>(data.size()>>(8*i)));out.insert(out.end(),data.begin(),data.end());return out;}
struct Writer {
    Blob data;
    explicit Writer(Type type):data{'P','T',1,static_cast<unsigned char>(type)}{}
    void number(std::uint64_t v){for(int i=0;i<8;i++)data.push_back(static_cast<unsigned char>(v>>(8*i)));}
    void string(const std::string& v){check(v.size()<=65536);number(v.size());data.insert(data.end(),v.begin(),v.end());}
    void bytes(const Blob& v){check(v.size()<=65536);number(v.size());data.insert(data.end(),v.begin(),v.end());}
    void text(const std::wstring& v){check(v.size()<=16384);if(v.empty()){string({});return;}int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,v.data(),static_cast<int>(v.size()),nullptr,0,nullptr,nullptr);check(n>0);std::string s(n,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,v.data(),static_cast<int>(v.size()),s.data(),n,nullptr,nullptr);string(s);}
    Blob frame(){return protocol::frame(data);}
};
struct Reader {
    const Blob& data;size_t cursor=4;
    explicit Reader(const Blob& b):data(b){check(b.size()>=4&&b[0]=='P'&&b[1]=='T'&&b[2]==1);}
    unsigned char type()const{return data[3];}
    std::uint64_t number(){check(data.size()-cursor>=8);std::uint64_t n=0;for(int i=0;i<8;i++)n|=std::uint64_t(data[cursor++])<<(8*i);return n;}
    std::string string(){auto n=number();check(n<=65536&&n<=data.size()-cursor);std::string s(data.begin()+cursor,data.begin()+cursor+static_cast<size_t>(n));cursor+=static_cast<size_t>(n);return s;}
    Blob bytes(){auto n=number();check(n<=65536&&n<=data.size()-cursor);Blob value(data.begin()+cursor,data.begin()+cursor+static_cast<size_t>(n));cursor+=static_cast<size_t>(n);return value;}
    std::wstring text(){auto s=string();if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);check(n>0&&n<=16384);std::wstring text(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),text.data(),n);for(auto c:text)check(c>=32||c==L'\n'||c==L'\r'||c==L'\t');return text;}
    size_t remaining()const{return data.size()-cursor;}
    void end(){check(cursor==data.size());}
};
}
