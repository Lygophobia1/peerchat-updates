// Narrow C ABI: application serializes control calls; no C++ ownership crosses.
#include "group_screen_share_session.hpp"
#include <sstream>
#include <stdexcept>
#define SCREEN_GROUP_API extern "C" __declspec(dllexport)
namespace {
struct Bridge {peerchat::GroupScreenShareSession session{std::make_unique<peerchat::RelayMeshScreenShareTopology>()};std::wstring output;};
std::string ascii(const wchar_t* p){std::string s;if(p)for(;*p;++p){if(*p>127)throw std::invalid_argument("Invalid text");s+=static_cast<char>(*p);}return s;}
std::wstring wide(const std::string& s){return {s.begin(),s.end()};}
std::wstring quote(const std::wstring& s){std::wostringstream o;o<<L'"';for(auto c:s){if(c==L'"'||c==L'\\')o<<L'\\'<<c;else if(c<32){const wchar_t* hex=L"0123456789abcdef";o<<L"\\u00"<<hex[(c>>4)&15]<<hex[c&15];}else o<<c;}o<<L'"';return o.str();}
peerchat::MediaIceConfiguration ice(const wchar_t* host,unsigned short port,const wchar_t* user,const wchar_t* password,std::int64_t expiry,std::uint64_t profile,std::uint64_t discovery,std::uint64_t configuration){peerchat::MediaIceConfiguration c;c.host=ascii(host);c.port=port;c.username=ascii(user);c.password=ascii(password);c.expiresUtc=expiry;c.profileSessionGeneration=profile;c.discoveryGeneration=discovery;c.generation=configuration;return c;}
}
SCREEN_GROUP_API void* pc_group_screen_create(){try{return new Bridge;}catch(...){return nullptr;}}
SCREEN_GROUP_API void pc_group_screen_destroy(void* h){try{delete static_cast<Bridge*>(h);}catch(...){}}
SCREEN_GROUP_API int pc_group_screen_start(void* h,const wchar_t* group,const wchar_t* room,const wchar_t* epoch,
    std::uint64_t account,const wchar_t* device,const wchar_t* participation,const wchar_t* share,std::uint64_t revision,std::uint64_t roster,std::uint64_t generation,
    int sender,unsigned sourceKind,std::uint64_t sourceHandle,std::uint64_t window,unsigned fps,unsigned bitrate,int cursor,
    const wchar_t* host,unsigned short port,const wchar_t* username,const wchar_t* password,std::int64_t expiry,std::uint64_t profile,std::uint64_t discovery,std::uint64_t configuration){
    try{if(!h||fps<1||fps>120||bitrate<100000||bitrate>100000000)return 0;
        peerchat::ScreenRoomBinding b{ascii(group),ascii(room),ascii(epoch),ascii(device),ascii(participation),ascii(share),account,revision,roster,generation,sender!=0};
        pc_screen_encoder_options capture{};capture.structure_size=sizeof(capture);capture.source_kind=sourceKind;capture.source_handle=sourceHandle;capture.frames_per_second=fps;capture.bitrate_bits_per_second=bitrate;capture.capture_cursor=cursor!=0;
        return static_cast<Bridge*>(h)->session.start(std::move(b),ice(host,port,username,password,expiry,profile,discovery,configuration),capture,window)?1:0;
    }catch(...){return 0;}
}
SCREEN_GROUP_API int pc_group_screen_configure(void* h,const wchar_t* host,unsigned short port,const wchar_t* user,const wchar_t* password,std::int64_t expiry,std::uint64_t profile,std::uint64_t discovery,std::uint64_t generation){try{auto c=ice(host,port,user,password,expiry,profile,discovery,generation);if(!h||!c.valid(std::time(nullptr)))return 0;static_cast<Bridge*>(h)->session.configure(std::move(c));return 1;}catch(...){return 0;}}
SCREEN_GROUP_API int pc_group_screen_reconcile(void* h,std::uint64_t revision,std::uint64_t roster,const wchar_t* rows){
    try{if(!h)return 0;std::vector<peerchat::AuthorizedScreenEndpoint> peers;std::istringstream input(ascii(rows));std::string row;
        while(std::getline(input,row,';')){if(row.empty()||peers.size()>=32)return 0;std::istringstream fields(row);std::string a,d,p,extra;
            if(!std::getline(fields,a,',')||!std::getline(fields,d,',')||!std::getline(fields,p,',')||std::getline(fields,extra,','))return 0;
            if(a.empty()||a.find_first_not_of("0123456789")!=std::string::npos)return 0;
            peers.push_back({std::stoull(a),d,p});}
        return static_cast<Bridge*>(h)->session.reconcile(revision,roster,peers)?1:0;
    }catch(...){return 0;}
}
SCREEN_GROUP_API int pc_group_screen_signal(void* h,const wchar_t* source,const wchar_t* route,std::uint64_t generation,const wchar_t* kind,const wchar_t* data,const wchar_t* mid){try{return h&&static_cast<Bridge*>(h)->session.signaling({ascii(source),ascii(route),ascii(kind),ascii(data),ascii(mid),generation});}catch(...){return 0;}}
SCREEN_GROUP_API void pc_group_screen_pump(void* h){try{if(h)static_cast<Bridge*>(h)->session.pump();}catch(...){if(h)static_cast<Bridge*>(h)->session.stop();}}
SCREEN_GROUP_API const wchar_t* pc_group_screen_take_signals(void* h){
    if(!h)return L"[]";
    auto& b=*static_cast<Bridge*>(h);try{std::wostringstream o;o<<L'[';bool first=true;for(const auto& s:b.session.takeSignals()){
        if(!first)o<<L',';
        first=false;o<<L"{\"sourceKey\":"<<quote(wide(s.sourceKey))<<L",\"routeId\":"<<quote(wide(s.routeId))<<L",\"generation\":"<<s.generation<<L",\"kind\":"<<quote(wide(s.kind))<<L",\"data\":"<<quote(wide(s.data))<<L",\"mid\":"<<quote(wide(s.mid))<<L'}';}o<<L']';b.output=o.str();}catch(...){b.output=L"[]";}return b.output.c_str();
}
SCREEN_GROUP_API const wchar_t* pc_group_screen_snapshot(void* h){
    if(!h)return L"{}";
    auto& b=*static_cast<Bridge*>(h);try{auto s=b.session.snapshot();std::wostringstream o;o<<L"{\"active\":"<<(s.active?L"true":L"false")<<L",\"error\":"<<quote(wide(s.error))<<L",\"capturePipelineCount\":"<<(s.active&&s.binding.sender?1:0)<<L",\"encoderCount\":"<<(s.active&&s.binding.sender?1:0)<<L",\"capturedFrames\":"<<s.capture.captured_frames<<L",\"encodedFrames\":"<<s.capture.encoded_frames<<L",\"encoderQueueDrops\":"<<s.capture.dropped_encoded_queue<<L",\"fanoutCopies\":"<<s.fanoutCopies<<L",\"encodedAuBytesDrained\":"<<s.encodedBytes<<L",\"width\":"<<s.width<<L",\"height\":"<<s.height<<L",\"keyframes\":"<<s.capture.idr_nals<<L",\"captureFpsAverage\":"<<(s.elapsedMs?s.capture.captured_frames*1000.0/s.elapsedMs:0)<<L",\"encodeFpsAverage\":"<<(s.elapsedMs?s.capture.encoded_frames*1000.0/s.elapsedMs:0)<<L",\"encodedDrainMbpsAverage\":"<<(s.elapsedMs?s.encodedBytes*0.008/s.elapsedMs:0)<<L",\"activeRouteCount\":"<<std::count_if(s.routes.begin(),s.routes.end(),[](const auto& route){return route.connected;})<<L",\"routes\":[";bool first=true;
        for(const auto& r:s.routes){if(!first)o<<L',';first=false;o<<L"{\"sourceKey\":"<<quote(wide(r.sourceKey))<<L",\"connected\":"<<(r.connected?L"true":L"false")<<L",\"turn\":"<<(r.turn?L"true":L"false")<<L",\"state\":"<<quote(wide(r.state))<<L",\"reason\":"<<quote(wide(r.reason))<<L",\"details\":"<<quote(wide(r.details))<<L",\"sent\":"<<r.sent<<L",\"bytes\":"<<r.bytes<<L",\"received\":"<<r.received<<L",\"receivedBytes\":"<<r.receivedBytes<<L",\"dropped\":"<<r.dropped<<L",\"decoded\":"<<r.decoded<<L",\"presented\":"<<r.presented<<L'}';}o<<L"]}";b.output=o.str();}catch(...){b.output=L"{}";}return b.output.c_str();
}
SCREEN_GROUP_API void pc_group_screen_stop(void* h){try{if(h)static_cast<Bridge*>(h)->session.stop();}catch(...){}}
