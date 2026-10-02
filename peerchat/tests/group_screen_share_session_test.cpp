#include "../group_screen_share_session.hpp"
#include <iostream>
#include <stdexcept>
using namespace peerchat;
void check(bool v,const char* s){if(!v)throw std::runtime_error(s);}
struct Source:IScreenCaptureSource {
    unsigned opens=0,closes=0,frames=0;bool running=false,ready=false,fail=false;
    std::uint8_t bytes[6]={0,0,0,1,0x65,0x11};
    bool open(const pc_screen_encoder_options& o)override{check(o.backend==PC_SCREEN_BACKEND_WEBRTC_COMPATIBILITY,"group native backend selected");++opens;running=true;frames=0;return true;}
    int pop(pc_screen_encoded_frame& f)override{if(fail)return -2;if(!ready)return 0;ready=false;++frames;f.length=6;f.idr=1;f.encoded_sequence=frames;return 1;}
    const std::uint8_t* data()const override{return bytes;}
    bool status(pc_screen_encoder_status& s)const override{s.captured_frames=frames;s.encoded_frames=frames;return running;}
    bool requestKeyframe()override{return running;}
    void close()override{if(running)++closes;running=false;}
};
struct Topology:IScreenShareMediaTopology {
    bool running=false;unsigned starts=0;std::map<std::string,unsigned> delivered;std::string failed;
    bool start(bool,MediaIceConfiguration,std::uint64_t,unsigned)override{running=true;++starts;return true;}
    void configure(MediaIceConfiguration)override{}
    bool reconcile(const std::vector<AuthorizedScreenEndpoint>& peers)override{
        std::map<std::string,unsigned> next;for(const auto& p:peers)next[p.key()]=delivered[p.key()];delivered=std::move(next);return running;
    }
    bool signaling(const ScreenTransportSignal&)override{return running;}
    unsigned sendEncoded(const std::uint8_t*,const pc_screen_encoded_frame&)override{unsigned count=0;for(auto& p:delivered)if(p.first!=failed){++p.second;++count;}return count;}
    std::vector<ScreenTransportSignal> takeSignals()override{return {};}
    bool takeKeyframeRequest()override{return false;}
    std::vector<ScreenRouteSnapshot> snapshot()override{return {};}
    void stop()override{running=false;delivered.clear();}
};
int main(){try{
    auto source=std::make_unique<Source>();auto* capture=source.get();auto topology=std::make_unique<Topology>();auto* routes=topology.get();
    GroupScreenShareSession session(std::move(topology),std::move(source));
    ScreenRoomBinding binding{std::string(32,'a'),std::string(32,'b'),std::string(32,'c'),std::string(64,'d'),std::string(32,'e'),std::string(32,'e'),1,1,1,1,true};
    pc_screen_encoder_options options{};options.structure_size=sizeof(options);options.frames_per_second=30;options.bitrate_bits_per_second=8000000;
    AuthorizedScreenEndpoint b{2,std::string(64,'b'),std::string(32,'b')},c{3,std::string(64,'c'),std::string(32,'c')},d{4,std::string(64,'f'),std::string(32,'f')};
    check(session.start(binding,{},options,0),"session start");check(session.reconcile(1,1,{}),"zero viewers invalid");
    auto frame=[&](){capture->ready=true;session.pump();};frame();
    check(session.reconcile(2,1,{b}),"first viewer");frame();check(routes->delivered[b.key()]==1,"one viewer fanout");
    check(session.reconcile(3,1,{b,c}),"second viewer");frame();check(routes->delivered[b.key()]==2&&routes->delivered[c.key()]==1,"join reset first viewer");
    check(session.reconcile(4,1,{b,c,d}),"third viewer");frame();check(capture->opens==1&&routes->starts==1,"viewer join restarted capture/encoder/topology");
    routes->failed=c.key();frame();check(routes->delivered[b.key()]==4&&routes->delivered[d.key()]==2,"failed viewer blocked another");
    check(session.reconcile(5,1,{b,d}),"leave reconciliation");frame();check(routes->delivered.count(c.key())==0,"left viewer retained");
    check(!session.signaling({c.key(),std::string(32,'a'),"offer","stale"}),"removed viewer signal accepted");
    check(!session.reconcile(4,1,{b}),"stale revision accepted");check(!session.reconcile(6,0,{b}),"stale roster accepted");
    session.stop();check(capture->closes==1&&!routes->running,"stop did not retire mechanics/routes");check(session.snapshot().capture.encoded_frames==6,"final capture diagnostics lost");
    check(!session.signaling({b.key(),std::string(32,'a'),"offer","late"}),"stopped generation accepted signaling");
    b.participation=std::string(32,'9');binding.generation=2;binding.shareGeneration=std::string(32,'8');
    check(session.start(binding,{},options,0)&&session.reconcile(1,1,{b}),"restart failed");
    check(!session.signaling({"2:"+b.device+":"+std::string(32,'b'),std::string(32,'a'),"offer","old"}),"old participation accepted");
    capture->fail=true;session.pump();auto failed=session.snapshot();check(!failed.active&&!failed.error.empty(),"local encoder failure not bounded");
    // Independent room/session owner and replaceable topology, not a global
    // room or voice state singleton. Viewer creates no encoder.
    auto secondSource=std::make_unique<Source>();auto* source2=secondSource.get();auto secondTopology=std::make_unique<Topology>();
    GroupScreenShareSession second(std::move(secondTopology),std::move(secondSource));binding.sender=false;binding.room=std::string(32,'7');
    check(second.start(binding,{},options,1)&&second.reconcile(1,1,{b}),"independent viewer room failed");check(source2->opens==0,"viewer created encoder");
    check(second.snapshot().active&&!session.snapshot().active,"rooms coupled");
    std::cout<<"PASS: 1/2/3 viewer capture-once encode-once, incremental routes, viewer failure/removal, stale participation/revision, stop/restart, capture failure, reusable topology and independent viewer room\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
