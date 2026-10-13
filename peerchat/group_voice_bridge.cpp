// Narrow application/native boundary. No C++ object or allocator crosses it.
// The application serializes all calls; device/callback workers stay native.
#include "group_voice_session.hpp"
#include "statistics.hpp"
#include <chrono>
#include <sstream>
#include <stdexcept>
#define GROUP_API extern "C" __declspec(dllexport)
namespace {
struct Bridge {bool aec=false;peerchat::GroupVoiceSession session{std::make_unique<peerchat::RelayMeshVoiceTopology>()};std::wstring output;peerchat::statistics::CounterRates rates;};
std::string ascii(const wchar_t* p){std::string s;if(p)for(;*p;++p){if(*p>127)throw std::invalid_argument("Invalid text");s+=static_cast<char>(*p);}return s;}
std::wstring wide(const std::string& s){return {s.begin(),s.end()};}
std::wstring quote(const std::wstring& s){std::wostringstream o;o<<L'"';for(auto c:s){if(c==L'"'||c==L'\\')o<<L'\\'<<c;else if(c<32){const wchar_t* hex=L"0123456789abcdef";o<<L"\\u00"<<hex[(c>>4)&15]<<hex[c&15];}else o<<c;}o<<L'"';return o.str();}
}
GROUP_API void* pc_group_voice_create(){try{return new Bridge;}catch(...){return nullptr;}}
GROUP_API int pc_group_voice_aec(void* h,int enabled){if(!h)return 0;auto& b=*static_cast<Bridge*>(h);if(b.session.snapshot().active)return 0;b.aec=enabled!=0;return 1;}
GROUP_API void pc_group_voice_destroy(void* h){delete static_cast<Bridge*>(h);}
GROUP_API int pc_group_voice_start(void* h,const wchar_t* group,const wchar_t* room,const wchar_t* epoch,
    std::uint64_t account,const wchar_t* device,const wchar_t* participation,std::uint64_t revision,std::uint64_t roster,std::uint64_t generation,
    const wchar_t* host,unsigned short port,const wchar_t* username,const wchar_t* password,std::int64_t expiry,
    std::uint64_t profileGeneration,std::uint64_t discoveryGeneration,std::uint64_t configurationGeneration,
    const wchar_t* receiveDll,int input,int output,int inputVolume,int outputVolume,int processing,int ns,int agc,unsigned nsStrength,unsigned agcStrength){
    try{
        if(!h||!receiveDll||inputVolume<0||inputVolume>200||outputVolume<0||outputVolume>200||nsStrength>2||agcStrength>2)return 0;
        peerchat::VoiceRoomBinding binding{ascii(group),ascii(room),ascii(epoch),ascii(device),ascii(participation),account,revision,roster,generation};
        peerchat::MediaIceConfiguration ice;ice.host=ascii(host);ice.port=port;ice.username=ascii(username);ice.password=ascii(password);ice.expiresUtc=expiry;
        ice.profileSessionGeneration=profileGeneration;ice.discoveryGeneration=discoveryGeneration;ice.generation=configurationGeneration;
        peerchat::VoiceMedia::ProcessingOptions options;options.echoCancellation=static_cast<Bridge*>(h)->aec;options.enabled=processing!=0;options.noiseSuppression=ns!=0;options.automaticGain=agc!=0;
        options.noiseSuppressionStrength=static_cast<peerchat::NoiseSuppressionStrength>(nsStrength);options.automaticGainStrength=static_cast<peerchat::AutomaticGainStrength>(agcStrength);
        std::wstring error;auto& bridge=*static_cast<Bridge*>(h);
        bool ready=bridge.session.start(std::move(binding),std::move(ice),receiveDll,options,input,output,inputVolume/100.f,outputVolume/100.f,error);
        bridge.output=error;return ready?1:0;
    }catch(...){return 0;}
}
GROUP_API int pc_group_voice_reconcile(void* h,std::uint64_t revision,std::uint64_t roster,const wchar_t* rows){
    try{if(!h)return 0;std::vector<peerchat::AuthorizedVoiceSource> sources;std::istringstream in(ascii(rows));std::string row;
        // Rows use commas, not display names: account,device,participation;...
        while(std::getline(in,row,';')){if(row.empty()||sources.size()>=32)return 0;std::istringstream fields(row);std::string a,d,p,extra;
            if(!std::getline(fields,a,',')||!std::getline(fields,d,',')||!std::getline(fields,p,',')||std::getline(fields,extra,','))return 0;
            if(a.empty()||a.find_first_not_of("0123456789")!=std::string::npos)return 0;
            sources.push_back({std::stoull(a),d,p});}
        return static_cast<Bridge*>(h)->session.reconcile(revision,roster,sources)?1:0;
    }catch(...){return 0;}
}
GROUP_API int pc_group_voice_signal(void* h,const wchar_t* source,const wchar_t* route,std::uint64_t generation,const wchar_t* kind,const wchar_t* data,const wchar_t* mid){
    try{return h&&static_cast<Bridge*>(h)->session.signaling({ascii(source),ascii(route),ascii(kind),ascii(data),ascii(mid),generation});}catch(...){return 0;}
}
GROUP_API int pc_group_voice_configure(void* h,const wchar_t* host,unsigned short port,const wchar_t* username,const wchar_t* password,std::int64_t expiry,std::uint64_t profile,std::uint64_t discovery,std::uint64_t generation){
    try{if(!h)return 0;peerchat::MediaIceConfiguration ice;ice.host=ascii(host);ice.port=port;ice.username=ascii(username);ice.password=ascii(password);ice.expiresUtc=expiry;ice.profileSessionGeneration=profile;ice.discoveryGeneration=discovery;ice.generation=generation;
        if(!ice.valid(std::time(nullptr)))return 0;
        static_cast<Bridge*>(h)->session.configure(std::move(ice));return 1;
    }catch(...){return 0;}
}
GROUP_API const wchar_t* pc_group_voice_take_signals(void* h){
    if(!h)return L"[]";
    auto& b=*static_cast<Bridge*>(h);
    try{std::wostringstream o;o<<L'[';bool first=true;for(const auto& s:b.session.takeSignals()){
        if(!first)o<<L',';
        first=false;o<<L"{\"sourceKey\":"<<quote(wide(s.sourceKey))<<L",\"routeId\":"<<quote(wide(s.routeId))<<L",\"generation\":"<<s.generation
            <<L",\"kind\":"<<quote(wide(s.kind))<<L",\"data\":"<<quote(wide(s.data))<<L",\"mid\":"<<quote(wide(s.mid))<<L'}';}o<<L']';b.output=o.str();
    }catch(...){b.output=L"[]";}return b.output.c_str();
}
GROUP_API const wchar_t* pc_group_voice_snapshot(void* h){
    if(!h)return L"{}";
    auto& b=*static_cast<Bridge*>(h);
    try{auto s=b.session.snapshot();auto now=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());if(!s.active)b.rates.clear();std::wostringstream o;o<<L"{\"active\":"<<(s.active?L"true":L"false")<<L",\"muted\":"<<(s.muted?L"true":L"false")<<L",\"deafened\":"<<(s.deafened?L"true":L"false")
        <<L",\"encodedPackets\":"<<s.encodedPackets<<L",\"fanoutCopies\":"<<s.fanoutCopies<<L",\"apmActive\":"<<(s.apmActive?L"true":L"false")<<L",\"apmFailure\":"<<s.apmFailure
        <<L",\"aec_enabled\":"<<(s.aecEnabled?L"true":L"false")<<L",\"aec_render_reference_active\":"<<(s.referenceActive?L"true":L"false")<<L",\"aec_render_blocks\":"<<s.renderBlocks<<L",\"aec_reference_underruns\":"<<s.referenceUnderruns<<L",\"aec_resets\":"<<s.aecResets<<L",\"aec_delay_ms\":"<<s.aecDelayMs<<L",\"captureBlocks\":"<<s.captureBlocks<<L",\"acceptedPackets\":"<<s.receive.acceptedPackets<<L",\"mixedBlocks\":"<<s.receive.mixedBlocks<<L",\"underrunBlocks\":"<<s.receive.underrunBlocks
        <<L",\"receiveLastError\":"<<s.receive.lastError<<L",\"error\":"<<quote(s.error)<<L",\"routes\":[";bool first=true;
        for(const auto& r:s.routes){if(!first)o<<L',';first=false;o<<L"{\"sourceKey\":"<<quote(wide(r.sourceKey))<<L",\"connected\":"<<(r.connected?L"true":L"false")<<L",\"turn\":"<<(r.turn?L"true":L"false")
            <<L",\"state\":"<<quote(wide(r.state))<<L",\"reason\":"<<quote(wide(r.reason))<<L",\"sentPackets\":"<<r.sentPackets<<L",\"receivedPackets\":"<<r.receivedPackets<<L",\"sendPayloadBits\":"<<b.rates.sample(r.routeId+"s",r.sentBytes,now,r.connected)<<L",\"receivePayloadBits\":"<<b.rates.sample(r.routeId+"r",r.receivedBytes,now,r.connected)<<L'}';}o<<L"]}";b.output=o.str();
    }catch(...){b.output=L"{}";}return b.output.c_str();
}
GROUP_API int pc_group_voice_mute(void* h,int value){try{std::wstring error;return h&&static_cast<Bridge*>(h)->session.mute(value!=0,error);}catch(...){return 0;}}
GROUP_API void pc_group_voice_deafen(void* h,int value){if(h)static_cast<Bridge*>(h)->session.deafen(value!=0);}
GROUP_API void pc_group_voice_stop(void* h){if(h)static_cast<Bridge*>(h)->session.stop();}
