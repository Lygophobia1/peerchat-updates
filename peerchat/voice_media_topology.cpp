#define NOMINMAX
#define RTC_STATIC
#include "voice_media_topology.hpp"
#include <rtc/rtc.h>
#include <bcrypt.h>
#include <algorithm>
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
namespace peerchat {
namespace {
std::string randomId(){unsigned char bytes[16]{};if(BCryptGenRandom(nullptr,bytes,sizeof(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return {};const char hex[]="0123456789abcdef";std::string id;for(auto b:bytes){id+=hex[b>>4];id+=hex[b&15];}return id;}
struct Endpoint;
// Lifetime registry only. A callback acquires shared ownership before touching
// any endpoint. Registry does not own sessions, transport policy or audio state.
std::mutex registryMutex;std::map<int,std::weak_ptr<Endpoint>> registry;
std::shared_ptr<Endpoint> findEndpoint(int id){std::lock_guard<std::mutex> lock(registryMutex);auto it=registry.find(id);return it==registry.end()?nullptr:it->second.lock();}
struct Endpoint:std::enable_shared_from_this<Endpoint> {
    std::mutex control,eventsMutex,callbackRegistration;
    std::atomic<bool> retired{false},open{false};std::atomic<int> state{RTC_NEW};
    int pc=-1,track=-1,receiveTrack=-1;std::string key,route,reason;std::uint64_t generation=1;
    IVoiceMediaTopology::Incoming incoming;
    std::deque<VoiceTransportSignal> events;
    std::atomic<std::uint64_t> sent{0},sentBytes{0},received{0},receivedBytes{0},dropped{0};
    ~Endpoint(){stop();}
    void push(std::string kind,std::string data,std::string mid="0"){
        if(retired)return;
        std::lock_guard<std::mutex> lock(eventsMutex);
        if(events.size()<128)events.push_back({key,route,std::move(kind),std::move(data),std::move(mid),generation});
    }
    static void description(int id,const char* data,const char* kind,void*){auto e=findEndpoint(id);if(e&&data&&kind)e->push(kind,data);}
    static void candidate(int id,const char* data,const char* mid,void*){auto e=findEndpoint(id);if(e&&data&&MediaIceConfiguration::relayCandidate(data))e->push("candidate",data,mid?mid:"0");}
    static void changed(int id,rtcState value,void*){auto e=findEndpoint(id);if(e&&!e->retired)e->state=value;}
    static void opened(int id,void*){auto e=findEndpoint(id);if(e&&!e->retired)e->open=true;}
    static void closed(int id,void*){auto e=findEndpoint(id);if(e)e->open=false;}
    static void incomingTrack(int id,int remote,void*){
        auto e=findEndpoint(id);if(!e||remote<0)return;
        std::lock_guard<std::mutex> guard(e->callbackRegistration);
        if(e->retired||remote==e->track)return;
        char mid[32]{};
        if(rtcGetTrackMid(remote,mid,sizeof(mid))<0||std::string(mid)!="0"||e->receiveTrack>=0){rtcDeleteTrack(remote);return;}
        e->receiveTrack=remote;
        {std::lock_guard<std::mutex> lock(registryMutex);registry[remote]=e;}
        rtcSetMessageCallback(remote,packet);rtcChainRtcpReceivingSession(remote);
        rtcSetOpenCallback(remote,opened);rtcSetClosedCallback(remote,closed);
    }
    static void packet(int id,const char* data,int count,void*){
        auto e=findEndpoint(id);if(!e||e->retired||!data||count<=0||count>8192)return;
        ++e->received;e->receivedBytes+=count;
        e->incoming(e->key,e->generation,reinterpret_cast<const unsigned char*>(data),count);
    }
    bool create(const MediaIceConfiguration& ice){
        if(ice.policy!=MediaIceConfiguration::Policy::RelayOnly||!ice.valid(std::time(nullptr))){reason="TURN credentials unavailable or expiring";return false;}
        std::string uri=ice.turnUdpUri();const char* servers[]={uri.c_str()};rtcConfiguration config{};
        config.iceServers=servers;config.iceServersCount=1;config.certificateType=RTC_CERTIFICATE_ECDSA;
        config.iceTransportPolicy=RTC_TRANSPORT_POLICY_RELAY;config.disableAutoNegotiation=true;config.forceMediaTransport=true;
        pc=rtcCreatePeerConnection(&config);if(pc<0){reason="Media endpoint creation failed";return false;}
        {std::lock_guard<std::mutex> lock(registryMutex);registry[pc]=shared_from_this();}
        rtcSetLocalDescriptionCallback(pc,description);rtcSetLocalCandidateCallback(pc,candidate);rtcSetStateChangeCallback(pc,changed);rtcSetTrackCallback(pc,incomingTrack);
        std::uint32_t ssrc=0;if(BCryptGenRandom(nullptr,reinterpret_cast<unsigned char*>(&ssrc),sizeof(ssrc),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0){reason="RTP identity allocation failed";return false;}if(!ssrc)ssrc=1;
        rtcTrackInit init{};init.direction=RTC_DIRECTION_SENDRECV;init.codec=RTC_CODEC_OPUS;init.payloadType=111;init.ssrc=ssrc;init.mid="0";init.name="PeerChat voice";init.msid="peerchat";init.trackId="voice";
        track=rtcAddTrackEx(pc,&init);if(track<0){reason="Audio track creation failed";return false;}
        {std::lock_guard<std::mutex> lock(registryMutex);registry[track]=shared_from_this();}
        rtcPacketizerInit packetizer{};packetizer.ssrc=ssrc;packetizer.cname="peerchat";packetizer.payloadType=111;packetizer.clockRate=48000;
        if(rtcSetOpusPacketizer(track,&packetizer)<0||rtcChainRtcpSrReporter(track)<0||rtcChainRtcpReceivingSession(track)<0){reason="Opus RTP setup failed";return false;}
        rtcSetMessageCallback(track,packet);rtcSetOpenCallback(track,opened);rtcSetClosedCallback(track,closed);return true;
    }
    bool signal(const VoiceTransportSignal& signal){
        std::lock_guard<std::mutex> lock(control);
        if(retired||pc<0||signal.routeId!=route||signal.generation!=generation)return false;
        if(signal.kind=="offer"||signal.kind=="answer"){
            std::istringstream description(signal.data);std::string line;
            while(std::getline(description,line))if(line.rfind("a=candidate:",0)==0&&!MediaIceConfiguration::relayCandidate(line))return false;
        }
        if(signal.kind=="candidate")return MediaIceConfiguration::relayCandidate(signal.data)&&rtcAddRemoteCandidate(pc,signal.data.c_str(),signal.mid.c_str())>=0;
        if(signal.kind=="offer")return rtcSetRemoteDescription(pc,signal.data.c_str(),"offer")>=0&&rtcSetLocalDescription(pc,"answer")>=0;
        if(signal.kind=="answer")return rtcSetRemoteDescription(pc,signal.data.c_str(),"answer")>=0;
        return false;
    }
    bool send(const LocalVoiceSource::Packet& packet){
        std::unique_lock<std::mutex> lock(control,std::try_to_lock);
        if(!lock.owns_lock()||retired||!open||state!=RTC_CONNECTED||track<0)return false;
        if(rtcGetBufferedAmount(track)>65536){++dropped;return false;}
        if(rtcSetTrackRtpTimestamp(track,packet.timestamp)<0||rtcSendMessage(track,reinterpret_cast<const char*>(packet.bytes.data()),packet.size)<0){++dropped;return false;}
        ++sent;sentBytes+=packet.size;return true;
    }
    void stop(){
        retired=true;open=false;std::lock_guard<std::mutex> lock(control);
        int outgoing=-1,incomingTrackId=-1;
        {std::lock_guard<std::mutex> registration(callbackRegistration);
            {std::lock_guard<std::mutex> guard(registryMutex);if(pc>=0)registry.erase(pc);if(track>=0)registry.erase(track);if(receiveTrack>=0)registry.erase(receiveTrack);}
            outgoing=track;incomingTrackId=receiveTrack;
            track=receiveTrack=-1;
        }
        // Never hold a callback-registration lock while releasing RTC objects.
        for(int id:{outgoing,incomingTrackId})if(id>=0){rtcSetMessageCallback(id,nullptr);rtcSetOpenCallback(id,nullptr);rtcSetClosedCallback(id,nullptr);rtcDeleteTrack(id);}
        if(pc>=0){rtcSetTrackCallback(pc,nullptr);rtcSetLocalDescriptionCallback(pc,nullptr);rtcSetLocalCandidateCallback(pc,nullptr);rtcSetStateChangeCallback(pc,nullptr);rtcClosePeerConnection(pc);rtcDeletePeerConnection(pc);pc=-1;}
    }
};
}
struct RelayMeshVoiceTopology::Impl {
    std::mutex mutex;std::string local;MediaIceConfiguration ice;Incoming incoming;
    std::map<std::string,AuthorizedVoiceSource> authorized;
    std::map<std::string,std::shared_ptr<Endpoint>> endpoints;
    std::shared_ptr<Endpoint> add(const std::string& key,const std::string& route){
        auto endpoint=std::make_shared<Endpoint>();endpoint->key=key;endpoint->route=route;endpoint->incoming=incoming;
        if(!endpoint->create(ice)){endpoint->stop();endpoint->state=RTC_FAILED;}
        endpoints[key]=endpoint;return endpoint;
    }
};
RelayMeshVoiceTopology::RelayMeshVoiceTopology():impl_(std::make_unique<Impl>()){}
RelayMeshVoiceTopology::~RelayMeshVoiceTopology(){stop();}
bool RelayMeshVoiceTopology::start(const std::string& local,MediaIceConfiguration ice,Incoming incoming){
    stop();if(local.size()!=64||ice.policy!=MediaIceConfiguration::Policy::RelayOnly||!ice.valid(std::time(nullptr))||!incoming)return false;
    impl_->local=local;impl_->ice=std::move(ice);impl_->incoming=std::move(incoming);return true;
}
void RelayMeshVoiceTopology::configure(MediaIceConfiguration ice){std::lock_guard<std::mutex> lock(impl_->mutex);impl_->ice=std::move(ice);}
bool RelayMeshVoiceTopology::reconcile(const std::vector<AuthorizedVoiceSource>& sources){
    std::lock_guard<std::mutex> lock(impl_->mutex);if(sources.size()>32)return false;
    std::map<std::string,AuthorizedVoiceSource> next;
    for(const auto& source:sources){if(!source.account||source.device.size()!=64||source.participation.size()!=32||source.device==impl_->local)return false;next.emplace(source.key(),source);}
    for(auto it=impl_->endpoints.begin();it!=impl_->endpoints.end();){if(!next.count(it->first)){it->second->stop();it=impl_->endpoints.erase(it);}else ++it;}
    impl_->authorized=std::move(next);
    for(const auto& source:impl_->authorized)if(!impl_->endpoints.count(source.first)&&impl_->local<source.second.device){
        auto id=randomId();if(id.empty())return false;auto endpoint=impl_->add(source.first,id);
        if(endpoint->pc>=0&&rtcSetLocalDescription(endpoint->pc,"offer")<0){endpoint->reason="Media offer creation failed";endpoint->state=RTC_FAILED;}
    }
    return true;
}
bool RelayMeshVoiceTopology::signaling(const VoiceTransportSignal& signal){
    std::lock_guard<std::mutex> lock(impl_->mutex);auto permitted=impl_->authorized.find(signal.sourceKey);
    if(permitted==impl_->authorized.end()||signal.routeId.size()!=32||signal.generation!=1||signal.data.size()>32768)return false;
    auto found=impl_->endpoints.find(signal.sourceKey);
    if(found==impl_->endpoints.end()){
        if(signal.kind!="offer"||impl_->local<permitted->second.device)return false;
        impl_->add(signal.sourceKey,signal.routeId);found=impl_->endpoints.find(signal.sourceKey);
    }
    if(signal.kind=="close"){if(found->second->route!=signal.routeId)return false;found->second->stop();return true;}
    return found->second->signal(signal);
}
unsigned RelayMeshVoiceTopology::sendEncoded(const LocalVoiceSource::Packet& packet){
    if(packet.size<=0||packet.size>4000)return 0;
    std::unique_lock<std::mutex> lock(impl_->mutex,std::try_to_lock);if(!lock.owns_lock())return 0;
    unsigned copies=0;for(auto& endpoint:impl_->endpoints)if(endpoint.second->send(packet))++copies;return copies;
}
std::vector<VoiceTransportSignal> RelayMeshVoiceTopology::takeSignals(){
    std::lock_guard<std::mutex> lock(impl_->mutex);std::vector<VoiceTransportSignal> result;
    for(auto& item:impl_->endpoints){auto& endpoint=*item.second;std::lock_guard<std::mutex> guard(endpoint.eventsMutex);
        while(!endpoint.events.empty()&&result.size()<128){result.push_back(std::move(endpoint.events.front()));endpoint.events.pop_front();}}
    return result;
}
std::vector<VoiceRouteSnapshot> RelayMeshVoiceTopology::snapshot(){
    std::lock_guard<std::mutex> lock(impl_->mutex);std::vector<VoiceRouteSnapshot> result;
    for(auto& item:impl_->endpoints){auto& e=*item.second;bool turn=false;
        if(e.pc>=0&&e.state==RTC_CONNECTED){char local[512]{},remote[512]{};
            if(rtcGetSelectedCandidatePair(e.pc,local,sizeof(local),remote,sizeof(remote))>=0){
                turn=MediaIceConfiguration::relayCandidate(local)&&MediaIceConfiguration::relayCandidate(remote);
                if(!turn){e.reason="Non-relay selected media route rejected";e.stop();e.state=RTC_FAILED;}
            }}
        result.push_back({e.key,e.route,std::to_string(e.state.load()),e.reason,e.generation,e.sent.load(),e.sentBytes.load(),e.received.load(),e.receivedBytes.load(),e.dropped.load(),e.open&&e.state==RTC_CONNECTED,turn});
    }
    return result;
}
void RelayMeshVoiceTopology::stop(){std::lock_guard<std::mutex> lock(impl_->mutex);for(auto& item:impl_->endpoints)item.second->stop();impl_->endpoints.clear();impl_->authorized.clear();impl_->incoming={};}
}
