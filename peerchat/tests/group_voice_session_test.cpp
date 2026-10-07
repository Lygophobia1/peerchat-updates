#define NOMINMAX
#include "../group_voice_session.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#define CHECK(v) do {if(!(v)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#v);std::exit(1);}}while(0)
using namespace peerchat;
struct TestDevice final:IVoiceAudioDevice {
    Capture capture;Playout playout;Render render;bool running=false,muted=false;unsigned stops=0;
    bool start(int,int,float,float,Capture c,Playout p,Render r,std::wstring&)override{capture=std::move(c);playout=std::move(p);render=std::move(r);running=true;return true;}
    bool mute(bool value,std::wstring&)override{muted=value;return running;}
    void stop()override{running=false;capture={};playout={};render={};++stops;}
    void frame(){CHECK(running);std::array<std::int16_t,320> output{};playout(output.data(),output.size());render(output.data(),output.size(),80);std::vector<unsigned char> pcm(640);capture(pcm);}
};
struct TestTopology final:IVoiceMediaTopology {
    Incoming incoming;std::vector<AuthorizedVoiceSource> sources;unsigned sent=0,stops=0;std::uint32_t timestamp=0;bool running=false;
    bool start(const std::string&,MediaIceConfiguration,Incoming callback)override{running=true;incoming=std::move(callback);sent=0;return true;}
    void configure(MediaIceConfiguration)override{}
    bool reconcile(const std::vector<AuthorizedVoiceSource>& next)override{sources=next;return running;}
    bool signaling(const VoiceTransportSignal&)override{return running;}
    unsigned sendEncoded(const LocalVoiceSource::Packet& packet)override{CHECK(running&&packet.size>0&&packet.size<=4000);timestamp=packet.timestamp;++sent;return sources.size();}
    std::vector<VoiceTransportSignal> takeSignals()override{return {};}
    std::vector<VoiceRouteSnapshot> snapshot()override{return {};}
    void stop()override{running=false;sources.clear();++stops;}
};
int main(int argc,char** argv){
    CHECK(argc==2);std::wstring dll;for(auto p=argv[1];*p;++p)dll.push_back(static_cast<unsigned char>(*p));
    auto t=std::make_unique<TestTopology>();auto d=std::make_unique<TestDevice>();auto* transport=t.get();auto* device=d.get();
    GroupVoiceSession session(std::move(t),std::move(d));
    VoiceRoomBinding binding{std::string(32,'a'),std::string(32,'b'),std::string(32,'c'),std::string(64,'d'),std::string(32,'e'),1,1,1,1};
    MediaIceConfiguration ice;VoiceMedia::ProcessingOptions processing;std::wstring error;
    processing.echoCancellation=true;
    AuthorizedVoiceSource b{2,std::string(64,'f'),std::string(32,'a')},c{3,std::string(64,'e'),std::string(32,'b')};
    CHECK(session.start(binding,ice,dll,processing,-1,-1,1,1,error));
    // A one-member room is an active participation, not a failed ringing call.
    CHECK(session.reconcile(1,1,{}));device->frame();CHECK(session.snapshot().active&&transport->sent==1);
    CHECK(session.reconcile(2,1,{b,c}));
    for(unsigned n=0;n<20;++n){device->frame();CHECK(transport->timestamp==(n+1)*960);}
    auto state=session.snapshot();CHECK(state.encodedPackets==21&&state.fanoutCopies==40&&state.apmActive);
    CHECK(state.aecEnabled&&state.referenceActive&&state.renderBlocks==42&&state.captureBlocks==42);
    CHECK(state.receive.authorizedSources==2);
    CHECK(!session.reconcile(1,1,{b}));CHECK(!session.reconcile(3,0,{b}));CHECK(!session.reconcile(3,1,{b,b}));
    CHECK(session.reconcile(3,1,{b}));CHECK(session.snapshot().receive.authorizedSources==1);
    CHECK(!session.signaling({c.key(),std::string(32,'a'),"offer","untrusted"}));
    CHECK(session.signaling({b.key(),std::string(32,'a'),"offer","test"}));
    CHECK(session.mute(true,error));device->frame();CHECK(session.snapshot().encodedPackets==21&&device->muted);
    CHECK(session.mute(false,error));device->frame();CHECK(session.snapshot().encodedPackets==22);
    CHECK(session.snapshot().aecResets>=2);
    session.deafen(true);std::array<std::int16_t,322> pcm{};pcm.front()=123;pcm.back()=321;
    CHECK(device->playout(pcm.data()+1,320));CHECK(pcm.front()==123&&pcm.back()==321);
    for(unsigned n=1;n<=320;++n)CHECK(pcm[n]==0);
    auto callback=transport->incoming;session.stop();CHECK(!session.snapshot().active&&!device->running&&!transport->running);
    std::array<unsigned char,13> packet{};callback(b.key(),1,packet.data(),packet.size()); // retired engine retained safely
    for(unsigned cycle=0;cycle<30;++cycle){
        binding.generation=cycle+2;binding.participation=std::string(32,cycle%2?'a':'b');
        CHECK(session.start(binding,ice,dll,processing,-1,-1,1,1,error));CHECK(session.reconcile(1,1,{b}));
        device->frame();CHECK(session.snapshot().encodedPackets==1&&transport->timestamp==0);
        session.stop();
    }
    binding.localAccount=0;CHECK(!session.start(binding,ice,dll,processing,-1,-1,1,1,error));
    std::puts("PASS: group session one-member room, one encode/fanout, framing, incremental members, authorization, mute/deafen, retired callback and 30 lifetimes");
}
