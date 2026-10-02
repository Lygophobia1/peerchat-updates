#include "screen_media_topology.hpp"
#include "screen_share_media.hpp"
#include <bcrypt.h>
#include <map>
#include <set>
namespace peerchat {
namespace {
std::string screenRouteId(){unsigned char bytes[16]{};if(BCryptGenRandom(nullptr,bytes,sizeof(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return {};const char hex[]="0123456789abcdef";std::string id;for(auto b:bytes){id+=hex[b>>4];id+=hex[b&15];}return id;}
bool relayDescription(const std::string& s){std::istringstream input(s);std::string line;while(std::getline(input,line))if(line.rfind("a=candidate:",0)==0&&!MediaIceConfiguration::relayCandidate(line))return false;return true;}
}
struct RelayMeshScreenShareTopology::Impl {
    struct Endpoint {std::unique_ptr<ScreenShareMedia> media;std::string id,reason;bool started=false,failed=false;std::uint64_t dropped=0;std::deque<ScreenTransportSignal> pending;};
    bool sender=false,active=false,keyframe=false;unsigned fps=30;std::uint64_t window=0;
    MediaIceConfiguration ice;std::map<std::string,Endpoint> routes;
    std::vector<ScreenTransportSignal> events;
    ScreenShareMedia::Options options()const{ScreenShareMedia::Options o;o.sender=sender;o.externallyEncoded=sender;o.backend=screen::Backend::WebRtcCompatibility;o.ice=ice;o.presentationWindow=window;o.fps=fps;
        // Same decoder bootstrap as the accepted 1:1 adapter. These are not
        // effective capture dimensions: the existing MFT stream-change path
        // selects the received SPS format before presenting decoded frames.
        o.width=1280;o.height=720;return o;}
    void poll(){for(auto& item:routes){auto& route=item.second;if(!route.started||route.failed)continue;
        for(auto& event:route.media->poll()){
            if(event.kind==ScreenShareMedia::EventKind::Description||event.kind==ScreenShareMedia::EventKind::Candidate){
                if(events.size()<128)events.push_back({item.first,route.id,event.kind==ScreenShareMedia::EventKind::Candidate?"candidate":event.detail,event.value,event.kind==ScreenShareMedia::EventKind::Candidate?event.detail:"0",1});
                else {route.reason="Bounded signaling queue exhausted";route.failed=true;}
            }else if(event.kind==ScreenShareMedia::EventKind::KeyframeRequested)keyframe=true;
            else if(event.kind==ScreenShareMedia::EventKind::Failed){route.reason=event.value;route.failed=true;}
        }
        if(route.failed)route.media->stop();
    }}
};
RelayMeshScreenShareTopology::RelayMeshScreenShareTopology():impl_(std::make_unique<Impl>()){}
RelayMeshScreenShareTopology::~RelayMeshScreenShareTopology(){stop();}
bool RelayMeshScreenShareTopology::start(bool sender,MediaIceConfiguration ice,std::uint64_t window,unsigned fps){
    stop();if(ice.policy!=MediaIceConfiguration::Policy::RelayOnly||!ice.valid(std::time(nullptr))||fps<1||fps>120||(!sender&&!window))return false;
    impl_->sender=sender;impl_->ice=std::move(ice);impl_->window=window;impl_->fps=fps;impl_->active=true;return true;
}
void RelayMeshScreenShareTopology::configure(MediaIceConfiguration ice){if(ice.policy==MediaIceConfiguration::Policy::RelayOnly&&ice.valid(std::time(nullptr)))impl_->ice=std::move(ice);}
bool RelayMeshScreenShareTopology::reconcile(const std::vector<AuthorizedScreenEndpoint>& peers){
    if(!impl_->active||peers.size()>32||(!impl_->sender&&peers.size()!=1))return false;
    std::set<std::string> keep;for(const auto& peer:peers){if(!peer.account||peer.device.size()!=64||peer.participation.size()!=32||!keep.insert(peer.key()).second)return false;}
    for(auto i=impl_->routes.begin();i!=impl_->routes.end();)if(!keep.count(i->first))i=impl_->routes.erase(i);else ++i;
    for(const auto& key:keep)if(!impl_->routes.count(key)){
        Impl::Endpoint route;route.media=std::make_unique<ScreenShareMedia>(impl_->options());
        if(impl_->sender){route.id=screenRouteId();route.started=!route.id.empty()&&route.media->startOffer(route.reason);route.failed=!route.started;impl_->keyframe=true;}
        impl_->routes.emplace(key,std::move(route));
    }
    return true;
}
bool RelayMeshScreenShareTopology::signaling(const ScreenTransportSignal& s){
    auto i=impl_->routes.find(s.sourceKey);if(!impl_->active||i==impl_->routes.end()||s.generation!=1||s.routeId.size()!=32)return false;
    auto& r=i->second;if(r.failed)return false;
    if((s.kind=="offer"||s.kind=="answer")&&!relayDescription(s.data))return false;
    if(s.kind=="offer"&&!impl_->sender&&!r.started){
        r.id=s.routeId;r.started=r.media->startAnswer(s.data,r.reason);r.failed=!r.started;
        if(r.started){for(const auto& p:r.pending)if(p.routeId==r.id)r.media->addCandidate(p.data,p.mid);r.pending.clear();}return r.started;
    }
    if(s.kind=="candidate"&&!r.started&&!impl_->sender){if(r.pending.size()>=32||!MediaIceConfiguration::relayCandidate(s.data))return false;r.pending.push_back(s);return true;}
    if(s.routeId!=r.id||!r.started)return false;
    if(s.kind=="answer"&&impl_->sender)return r.media->setAnswer(s.data);
    if(s.kind=="candidate")return r.media->addCandidate(s.data,s.mid);
    if(s.kind=="close"){r.failed=true;r.reason="Remote media route closed";r.media->stop();return true;}
    return false;
}
unsigned RelayMeshScreenShareTopology::sendEncoded(const std::uint8_t* bytes,const pc_screen_encoded_frame& info){unsigned accepted=0;for(auto& item:impl_->routes){auto& r=item.second;if(!r.failed&&r.started){if(r.media->sendEncoded(bytes,info))++accepted;else ++r.dropped;}}return accepted;}
std::vector<ScreenTransportSignal> RelayMeshScreenShareTopology::takeSignals(){impl_->poll();auto output=std::move(impl_->events);impl_->events.clear();return output;}
bool RelayMeshScreenShareTopology::takeKeyframeRequest(){impl_->poll();bool value=impl_->keyframe;impl_->keyframe=false;return value;}
std::vector<ScreenRouteSnapshot> RelayMeshScreenShareTopology::snapshot(){impl_->poll();std::vector<ScreenRouteSnapshot> result;for(auto& item:impl_->routes){auto& r=item.second;auto s=r.media->statistics();auto text=r.media->diagnostics();result.push_back({item.first,r.id,r.failed?"Failed":s.connected?"Connected":r.started?"Connecting":"Awaiting offer",r.reason,std::string(text.begin(),text.end()),s.connected,r.media->route()==ScreenShareMedia::Route::TurnRelay,s.encodedSubmitted,s.encodedBytesSubmitted,s.rtpReceived,s.mediaBytesReceived,r.dropped,s.decoded,s.presented});}return result;}
void RelayMeshScreenShareTopology::stop(){impl_->active=false;impl_->routes.clear();impl_->events.clear();impl_->keyframe=false;}
}
