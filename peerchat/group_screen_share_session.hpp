#pragma once
#include "screen_media_backend.hpp"
#include "screen_media_topology.hpp"
#include <map>
#include <chrono>
namespace peerchat {
class IScreenCaptureSource {
public:
    virtual ~IScreenCaptureSource()=default;
    virtual bool open(const pc_screen_encoder_options&)=0;
    virtual int pop(pc_screen_encoded_frame&)=0;
    virtual const std::uint8_t* data()const=0;
    virtual bool status(pc_screen_encoder_status&)const=0;
    virtual bool requestKeyframe()=0;
    virtual void close()=0;
};
class SharedScreenCaptureSource final:public IScreenCaptureSource {
    ScreenCaptureEncodeBackend backend_;
public:
    bool open(const pc_screen_encoder_options& o)override{return backend_.open(o);}
    int pop(pc_screen_encoded_frame& f)override{return backend_.pop(f);}
    const std::uint8_t* data()const override{return backend_.data();}
    bool status(pc_screen_encoder_status& s)const override{return backend_.status(s);}
    bool requestKeyframe()override{return backend_.requestKeyframe();}
    void close()override{backend_.close();}
};
struct ScreenRoomBinding {
    std::string group,room,epoch,localDevice,participation,shareGeneration;
    std::uint64_t localAccount=0,revision=0,rosterGeneration=0,generation=0;
    bool sender=false;
};
// Serialized application commands only. Authority is supplied by the existing
// room service. No reliable session owner or reconnect scheduler is added.
class GroupScreenShareSession {
public:
    GroupScreenShareSession(std::unique_ptr<IScreenShareMediaTopology> topology,
        std::unique_ptr<IScreenCaptureSource> source=std::make_unique<SharedScreenCaptureSource>())
        :topology_(std::move(topology)),source_(std::move(source)){}
    ~GroupScreenShareSession(){stop();}
    bool start(ScreenRoomBinding binding,MediaIceConfiguration ice,pc_screen_encoder_options capture,std::uint64_t window){
        stop();binding_=std::move(binding);error_.clear();copies_=0;nextKeyframeMs_=0;lastCapture_={};lastRoutes_.clear();encodedBytes_=0;width_=height_=0;startedMs_=now();
        if(binding_.group.size()!=32||binding_.room.size()!=32||binding_.epoch.size()!=32||binding_.localDevice.size()!=64||binding_.participation.size()!=32||binding_.shareGeneration.size()!=32||!binding_.localAccount||!binding_.generation||!topology_||!source_)return false;
        capture.backend=PC_SCREEN_BACKEND_WEBRTC_COMPATIBILITY;
        if(!topology_->start(binding_.sender,std::move(ice),window,capture.frames_per_second)){error_="TURN screen media unavailable";return false;}
        if(binding_.sender&&!source_->open(capture)){error_="Screen capture/software H.264 initialization failed";topology_->stop();return false;}
        active_=true;startupDeadlineMs_=now()+12000;return true;
    }
    bool reconcile(std::uint64_t revision,std::uint64_t roster,const std::vector<AuthorizedScreenEndpoint>& peers){
        if(!active_||revision<binding_.revision||roster<binding_.rosterGeneration||peers.size()>32)return false;
        std::map<std::string,AuthorizedScreenEndpoint> next;
        for(const auto& p:peers){if((p.account==binding_.localAccount&&p.device==binding_.localDevice)||!p.account||p.device.size()!=64||p.participation.size()!=32||!next.emplace(p.key(),p).second)return false;}
        if(!topology_->reconcile(peers))return false;
        authorized_=std::move(next);binding_.revision=revision;binding_.rosterGeneration=roster;return true;
    }
    bool signaling(const ScreenTransportSignal& s){return active_&&authorized_.count(s.sourceKey)&&topology_->signaling(s);}
    void configure(MediaIceConfiguration ice){if(active_)topology_->configure(std::move(ice));}
    void pump(){
        if(!active_)return;
        const auto time=now();
        if(topology_->takeKeyframeRequest()&&binding_.sender&&time>=nextKeyframeMs_){source_->requestKeyframe();nextKeyframeMs_=time+500;}
        if(!binding_.sender)return;
        for(unsigned i=0;i<4;++i){pc_screen_encoded_frame f{};f.structure_size=sizeof(f);int popped=source_->pop(f);if(!popped)break;if(popped<0){error_="Local screen encoder failed";stop();return;}encodedBytes_+=f.length;width_=f.width;height_=f.height;copies_+=topology_->sendEncoded(source_->data(),f);}
        pc_screen_encoder_status s{};s.structure_size=sizeof(s);
        if(source_->status(s)&&!s.captured_frames&&time>=startupDeadlineMs_){error_="Local screen capture startup timed out";stop();}
    }
    std::vector<ScreenTransportSignal> takeSignals(){return active_?topology_->takeSignals():std::vector<ScreenTransportSignal>{};}
    struct Snapshot {ScreenRoomBinding binding;bool active=false;std::uint64_t fanoutCopies=0,encodedBytes=0,elapsedMs=0;unsigned width=0,height=0;pc_screen_encoder_status capture{};std::vector<ScreenRouteSnapshot> routes;std::string error;};
    Snapshot snapshot(){Snapshot s;s.binding=binding_;s.active=active_;s.error=error_;s.fanoutCopies=copies_;s.encodedBytes=encodedBytes_;s.elapsedMs=now()-startedMs_;s.width=width_;s.height=height_;s.capture=lastCapture_;s.capture.structure_size=sizeof(s.capture);if(active_&&binding_.sender)source_->status(s.capture);s.routes=active_?topology_->snapshot():lastRoutes_;return s;}
    void stop(){if(active_){lastCapture_.structure_size=sizeof(lastCapture_);if(binding_.sender)source_->status(lastCapture_);lastRoutes_=topology_->snapshot();}active_=false;if(topology_)topology_->stop();if(source_)source_->close();authorized_.clear();}
private:
    static std::uint64_t now(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
    ScreenRoomBinding binding_;std::unique_ptr<IScreenShareMediaTopology> topology_;std::unique_ptr<IScreenCaptureSource> source_;
    std::map<std::string,AuthorizedScreenEndpoint> authorized_;bool active_=false;std::string error_;std::uint64_t copies_=0,nextKeyframeMs_=0,startupDeadlineMs_=0;
    pc_screen_encoder_status lastCapture_{};std::vector<ScreenRouteSnapshot> lastRoutes_;
    std::uint64_t encodedBytes_=0,startedMs_=0;unsigned width_=0,height_=0;
};
}
