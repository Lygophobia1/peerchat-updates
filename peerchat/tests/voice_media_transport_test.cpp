#include "voice_media.hpp"
#define RTC_STATIC
#include <rtc/rtc.h>
#include <chrono>
#include <atomic>
#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool countAtLeast(const std::wstring& diagnostics,const wchar_t* key,std::uint64_t minimum){auto start=diagnostics.find(key);if(start==std::wstring::npos)return false;start+=std::wcslen(key);auto end=diagnostics.find(L'\n',start);return std::stoull(diagnostics.substr(start,end-start))>=minimum;}

struct TurnPeer {
    int pc=-1,dc=-1,track=-1,remoteTrack=-1;
    TurnPeer* remote=nullptr;
    std::atomic<bool> connected{false},dataOpen{false};
    std::atomic<unsigned> dataMessages{0},mediaPackets{0};
    std::mutex gate;
};
void turnOpen(int id,void* value){auto* peer=static_cast<TurnPeer*>(value);if(peer&&id==peer->dc)peer->dataOpen=true;}
void turnData(int,const char*,int,void* value){if(value)++static_cast<TurnPeer*>(value)->dataMessages;}
void turnMedia(int,const char*,int,void* value){if(value)++static_cast<TurnPeer*>(value)->mediaPackets;}
void configureTurnData(TurnPeer& peer,int dc){peer.dc=dc;rtcSetUserPointer(dc,&peer);rtcSetOpenCallback(dc,turnOpen);rtcSetMessageCallback(dc,turnData);}
void turnDataChannel(int,int dc,void* value){if(value)configureTurnData(*static_cast<TurnPeer*>(value),dc);}
void turnTrack(int,int track,void* value){if(!value)return;auto& peer=*static_cast<TurnPeer*>(value);peer.remoteTrack=track;rtcSetUserPointer(track,&peer);rtcSetMessageCallback(track,turnMedia);}
void turnState(int,rtcState state,void* value){if(value&&state==RTC_CONNECTED)static_cast<TurnPeer*>(value)->connected=true;}
void turnDescription(int,const char* sdp,const char* type,void* value){auto* peer=static_cast<TurnPeer*>(value);if(!peer||!peer->remote||!sdp||!type)return;if(std::string(type)=="offer"){rtcSetRemoteDescription(peer->remote->pc,sdp,"offer");rtcSetLocalDescription(peer->remote->pc,"answer");}else if(std::string(type)=="answer")rtcSetRemoteDescription(peer->remote->pc,sdp,"answer");}
void turnCandidate(int,const char* candidate,const char* mid,void* value){auto* peer=static_cast<TurnPeer*>(value);if(peer&&peer->remote&&candidate)rtcAddRemoteCandidate(peer->remote->pc,candidate,mid?mid:"0");}
void destroyTurnPeer(TurnPeer& peer){if(peer.pc>=0){rtcSetLocalDescriptionCallback(peer.pc,nullptr);rtcSetLocalCandidateCallback(peer.pc,nullptr);rtcSetStateChangeCallback(peer.pc,nullptr);rtcSetDataChannelCallback(peer.pc,nullptr);rtcSetTrackCallback(peer.pc,nullptr);}for(int id:{peer.dc,peer.track,peer.remoteTrack})if(id>=0){rtcSetOpenCallback(id,nullptr);rtcSetMessageCallback(id,nullptr);rtcSetUserPointer(id,nullptr);}if(peer.dc>=0)rtcDeleteDataChannel(peer.dc);if(peer.remoteTrack>=0&&peer.remoteTrack!=peer.track)rtcDeleteTrack(peer.remoteTrack);if(peer.track>=0)rtcDeleteTrack(peer.track);if(peer.pc>=0){rtcClosePeerConnection(peer.pc);rtcDeletePeerConnection(peer.pc);}peer.pc=peer.dc=peer.track=peer.remoteTrack=-1;peer.remote=nullptr;}
bool selectedRelayPair(int pc){char local[512]{},remote[512]{};return rtcGetSelectedCandidatePair(pc,local,sizeof(local),remote,sizeof(remote))>=0&&std::string(local).find(" typ relay")!=std::string::npos&&std::string(remote).find(" typ relay")!=std::string::npos;}
bool validTurnUdpUri(const char* uri){if(!uri)return false;std::string value(uri);if(value.rfind("turn:",0)!=0||value.find_first_of(" \t\r\n")!=std::string::npos)return false;auto at=value.rfind('@'),query=value.find('?');if(at==std::string::npos||at<=5||query==std::string::npos||query<=at+1||value.substr(query)!="?transport=udp")return false;auto authority=value.substr(at+1,query-at-1);auto colon=authority.rfind(':');if(colon==std::string::npos||colon==0||colon+1>=authority.size())return false;for(char digit:authority.substr(colon+1))if(digit<'0'||digit>'9')return false;int port=0;try{port=std::stoi(authority.substr(colon+1));}catch(...){return false;}return port>0&&port<=65535;}
void runConfiguredTurnProbe(){
    const char* uri=std::getenv("PEERCHAT_TURN_TEST_URI");
    require(uri&&*uri,"TURN infrastructure probe requires PEERCHAT_TURN_TEST_URI.");
    require(validTurnUdpUri(uri),"PEERCHAT_TURN_TEST_URI must be a valid turn:user:password@host:port?transport=udp URI.");
    std::cout<<"turn_probe_configuration=TURN configured; transport=udp; relay-only\n";
    TurnPeer caller,answerer;caller.remote=&answerer;answerer.remote=&caller;
    const char* servers[]={uri};rtcConfiguration config{};config.iceServers=servers;config.iceServersCount=1;config.certificateType=RTC_CERTIFICATE_ECDSA;config.iceTransportPolicy=RTC_TRANSPORT_POLICY_RELAY;config.disableAutoNegotiation=true;config.forceMediaTransport=true;
    require(config.iceServers&&config.iceServersCount==1&&config.iceServers[0]==uri&&config.iceTransportPolicy==RTC_TRANSPORT_POLICY_RELAY,"TURN URI or relay-only transport policy was not installed in the tested PeerConnection configuration");
    auto create=[&](TurnPeer& peer){peer.pc=rtcCreatePeerConnection(&config);require(peer.pc>=0,"Could not create TURN relay probe peer connection");rtcSetUserPointer(peer.pc,&peer);rtcSetLocalDescriptionCallback(peer.pc,turnDescription);rtcSetLocalCandidateCallback(peer.pc,turnCandidate);rtcSetStateChangeCallback(peer.pc,turnState);rtcSetDataChannelCallback(peer.pc,turnDataChannel);rtcSetTrackCallback(peer.pc,turnTrack);};
    try{
        create(caller);create(answerer);configureTurnData(caller,rtcCreateDataChannel(caller.pc,"peerchat-turn-probe"));require(caller.dc>=0,"Could not create TURN relay probe data channel");
        rtcTrackInit track{};track.direction=RTC_DIRECTION_SENDRECV;track.codec=RTC_CODEC_OPUS;track.payloadType=111;track.ssrc=0xdecafbad;track.mid="0";track.name="PeerChat TURN probe";track.msid="peerchat";track.trackId="turn-probe";
        caller.track=rtcAddTrackEx(caller.pc,&track);require(caller.track>=0,"Could not create TURN relay probe media track");rtcSetUserPointer(caller.track,&caller);rtcSetMessageCallback(caller.track,turnMedia);rtcPacketizerInit packetizer{};packetizer.ssrc=track.ssrc;packetizer.cname="peerchat-turn-probe";packetizer.payloadType=111;packetizer.clockRate=48000;require(rtcSetOpusPacketizer(caller.track,&packetizer)>=0&&rtcChainRtcpSrReporter(caller.track)>=0&&rtcChainRtcpReceivingSession(caller.track)>=0,"Could not configure TURN relay probe RTP packetizer");
        require(rtcSetLocalDescription(caller.pc,"offer")>=0,"Could not start TURN relay probe offer");
        auto deadline=std::chrono::steady_clock::now()+20s;while(std::chrono::steady_clock::now()<deadline&&!(caller.connected&&answerer.connected&&caller.dataOpen&&answerer.dataOpen))std::this_thread::sleep_for(20ms);
        require(caller.connected&&answerer.connected&&caller.dataOpen&&answerer.dataOpen,"TURN/UDP relay probe did not establish DTLS data connectivity");require(selectedRelayPair(caller.pc)&&selectedRelayPair(answerer.pc),"TURN/UDP relay probe did not select relay candidate pairs");
        const char data[]="peerchat-turn-data";const char media[]={0x12,0x34,0x56};require(rtcSendMessage(caller.dc,data,static_cast<int>(sizeof(data)-1))>=0,"TURN relay data send was rejected");require(rtcSendMessage(caller.track,media,static_cast<int>(sizeof(media)))>=0,"TURN relay media send was rejected");deadline=std::chrono::steady_clock::now()+5s;while(std::chrono::steady_clock::now()<deadline&&(answerer.dataMessages.load()<1||answerer.mediaPackets.load()<1))std::this_thread::sleep_for(20ms);require(answerer.dataMessages.load()>=1,"TURN relay did not carry the DTLS data probe");require(answerer.mediaPackets.load()>=1,"TURN relay did not carry the DTLS-SRTP media probe");std::cout<<"PASS: configured TURN/UDP selected relay pairs and carried DTLS data plus DTLS-SRTP media probes.\n";destroyTurnPeer(caller);destroyTurnPeer(answerer);
    }catch(...){destroyTurnPeer(caller);destroyTurnPeer(answerer);throw;}
}
}

int main(){
    try{
        peerchat::MediaIceConfiguration relayOnly;
        relayOnly.host="turn.example.test";relayOnly.port=3478;relayOnly.username="temporary-user";relayOnly.password="temporary-password";
        relayOnly.expiresUtc=static_cast<std::int64_t>(std::time(nullptr))+120;
        relayOnly.profileSessionGeneration=7;relayOnly.discoveryGeneration=11;relayOnly.generation=13;
        require(relayOnly.valid(static_cast<std::int64_t>(std::time(nullptr))),"Valid relay-only media configuration was rejected");
        require(relayOnly.turnUdpUri()=="turn:temporary-user:temporary-password@turn.example.test:3478?transport=udp","Relay-only TURN/UDP URI was not built from the immutable configuration");
        require(peerchat::MediaIceConfiguration::relayCandidate("candidate:1 1 UDP 1 203.0.113.1 5000 typ relay"),"Relay candidate classification rejected typ relay");
        require(!peerchat::MediaIceConfiguration::relayCandidate("candidate:1 1 UDP 1 192.168.1.10 5000 typ host"),"Relay candidate classification accepted a host route");
        relayOnly.expiresUtc=static_cast<std::int64_t>(std::time(nullptr))+20;
        require(!relayOnly.valid(static_cast<std::int64_t>(std::time(nullptr))),"Expiring media credential was accepted for a new generation");
        const char* configuredTurnUri=std::getenv("PEERCHAT_TURN_TEST_URI");
        const char* infrastructureMode=std::getenv("PEERCHAT_TURN_INFRASTRUCTURE");
        if((infrastructureMode&&std::string(infrastructureMode)=="1")||(configuredTurnUri&&*configuredTurnUri)){rtcInitLogger(RTC_LOG_NONE,nullptr);runConfiguredTurnProbe();return 0;}
        for(int cycle=0;cycle<4;++cycle){
        peerchat::VoiceMedia::ProcessingOptions processing;
        if(cycle==1)processing.enabled=false;
        if(cycle==2)processing.automaticGain=false;
        if(cycle==3)processing.noiseSuppression=false;
        peerchat::VoiceMedia caller(peerchat::MediaIceConfiguration::directForTests(),-1,-1,static_cast<std::uint64_t>(cycle+1),cycle==0?"192.168.1.14/24":"10.211.241.175/24",processing);
        peerchat::VoiceMedia answerer(peerchat::MediaIceConfiguration::directForTests(),-1,-1,static_cast<std::uint64_t>(cycle+1),"192.168.1.17/24");
        std::wstring error;
        require(caller.startOffer(error),"Could not create caller offer");
        bool answerStarted=false,callerConnected=false,answererConnected=false;
        auto deadline=std::chrono::steady_clock::now()+10s;
        while(std::chrono::steady_clock::now()<deadline&&(!callerConnected||!answererConnected)){
            for(auto& event:caller.poll()){
                if(event.kind==peerchat::VoiceMedia::EventKind::Description&&event.detail=="offer"){
                    require(answerer.startAnswer(event.value,error),"Could not apply offer/create answer");answerStarted=true;
                }else if(event.kind==peerchat::VoiceMedia::EventKind::Candidate&&answerStarted)require(answerer.addCandidate(event.value,event.detail,error),"Answerer rejected caller candidate");
                else if(event.kind==peerchat::VoiceMedia::EventKind::Connected)callerConnected=true;
            }
            for(auto& event:answerer.poll()){
                if(event.kind==peerchat::VoiceMedia::EventKind::Description&&event.detail=="answer")require(caller.setAnswer(event.value,error),"Caller rejected answer");
                else if(event.kind==peerchat::VoiceMedia::EventKind::Candidate)require(caller.addCandidate(event.value,event.detail,error),"Caller rejected answerer candidate");
                else if(event.kind==peerchat::VoiceMedia::EventKind::Connected)answererConnected=true;
            }
            std::this_thread::sleep_for(10ms);
        }
        require(callerConnected&&answererConnected,"Direct ICE/DTLS-SRTP media did not connect");
        std::vector<unsigned char> pcm(640);
        for(std::size_t i=0;i<pcm.size();i+=2){pcm[i]=0x34;pcm[i+1]=0x12;}
        for(int frame=0;frame<12;++frame){caller.testInjectPcm(pcm);answerer.testInjectPcm(pcm);std::this_thread::sleep_for(20ms);}
        deadline=std::chrono::steady_clock::now()+5s;
        while(std::chrono::steady_clock::now()<deadline){caller.poll();answerer.poll();auto a=caller.diagnostics(),b=answerer.diagnostics();if(countAtLeast(a,L"opus_submitted=",12)&&countAtLeast(a,L"received_packets=",12)&&countAtLeast(a,L"decoded_frames=",12)&&countAtLeast(b,L"opus_submitted=",12)&&countAtLeast(b,L"received_packets=",12)&&countAtLeast(b,L"decoded_frames=",12))break;std::this_thread::sleep_for(20ms);}
        auto a=caller.diagnostics(),b=answerer.diagnostics();
        std::wcerr<<L"Caller diagnostics:\n"<<a<<L"\nAnswerer diagnostics:\n"<<b<<L'\n';
        require(countAtLeast(a,L"capture_frames=",12)&&countAtLeast(a,L"encoded_frames=",12)&&countAtLeast(a,L"opus_submitted=",12),"Caller did not capture/encode/submit injected PCM");
        require(a.find(L"echo_cancellation=false")!=std::wstring::npos,"Echo cancellation must stay disabled");
        if(processing.enabled)require(countAtLeast(a,L"apm_capture_10ms_calls=",24)&&countAtLeast(a,L"apm_processed_20ms_frames=",12)&&a.find(L"apm_enabled=true")!=std::wstring::npos&&a.find(std::wstring(L"noise_suppression=")+(processing.noiseSuppression?L"true":L"false"))!=std::wstring::npos&&a.find(std::wstring(L"automatic_gain_control=")+(processing.automaticGain?L"true":L"false"))!=std::wstring::npos&&countAtLeast(a,L"apm_level_frames=",12),"Capture-side APM configuration or two-block cadence did not match requested options");
        else require(a.find(L"apm_enabled=false")!=std::wstring::npos&&countAtLeast(a,L"apm_raw_bypass_frames=",12)&&a.find(L"apm_capture_10ms_calls=0")!=std::wstring::npos,"Disabled processing did not preserve raw 20 ms PCM-to-Opus path");
        require(countAtLeast(b,L"capture_frames=",12)&&countAtLeast(b,L"encoded_frames=",12)&&countAtLeast(b,L"opus_submitted=",12),"Answerer did not capture/encode/submit injected PCM");
        require(countAtLeast(a,L"received_packets=",12)&&countAtLeast(a,L"decoded_frames=",12)&&countAtLeast(a,L"playback_frames=",12),"Caller did not receive/decode/queue answerer Opus");
        require(countAtLeast(b,L"received_packets=",12)&&countAtLeast(b,L"decoded_frames=",12)&&countAtLeast(b,L"playback_frames=",12),"Answerer did not receive/decode/queue caller Opus");
        require(a.find(L"fresh_peer_connection=true")!=std::wstring::npos&&a.find(L"ice_gathering_state=new")==std::wstring::npos&&a.find(L"local_ice_candidates_generated=0")==std::wstring::npos&&a.find(L"ice_servers=test direct ICE")!=std::wstring::npos,"Fresh media session omitted ICE gathering evidence");
        require(a.find(L"network_generation_at_media_creation="+std::to_wstring(cycle+1))!=std::wstring::npos,"Media session retained an earlier network generation");
        const auto callerLevel=caller.telemetry(),answererLevel=answerer.telemetry();
        require(!callerLevel.outputAvailable||callerLevel.outputLevel>0,"Caller decoded non-silent PCM but output telemetry stayed at zero");
        require(!answererLevel.outputAvailable||answererLevel.outputLevel>0,"Answerer decoded non-silent PCM but output telemetry stayed at zero");
        if(processing.enabled){caller.testFailApmNextFrame();caller.testInjectPcm(pcm);
            require(countAtLeast(caller.diagnostics(),L"apm_raw_bypass_frames=",1)&&countAtLeast(caller.diagnostics(),L"encoded_frames=",13),"APM failure did not bypass to the original 20 ms Opus frame");
            bool apmWarning=false;for(const auto& event:caller.poll())if(event.kind==peerchat::VoiceMedia::EventKind::AudioProcessingError)apmWarning=true;
            require(apmWarning,"APM bypass did not report a nonfatal diagnostic event");}
        caller.stop();answerer.stop();
        }
        std::cout<<"PASS: injected PCM crossed direct ICE/DTLS-SRTP as timestamped Opus and reached both playback queues.\n";
        return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL: "<<failure.what()<<'\n';return 1;}
}
