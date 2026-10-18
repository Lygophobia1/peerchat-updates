#include "../portable/voice_application.hpp"
#include <deque>
#include <iostream>
using namespace peerchat;
using App=portable::VoiceApplication;
static void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
template<class F>static void rejects(F f){bool rejected=false;try{f();}catch(...){rejected=true;}check(rejected,"Expected admission rejection");}
struct FakeState {
    VoiceMedia::ReceiveBinding binding;
    std::vector<VoiceMedia::Event> events;
    unsigned stopped=0,candidates=0,answers=0,unmutedDeviceStarts=0;
    bool muted=false,deafened=false,muteFails=false;
};
// Explicit UNIT seam only. It never produces audio, ICE, DTLS, RTP or E2E proof.
class FakeMedia final:public App::Media {
    std::shared_ptr<FakeState> state_;
public:
    explicit FakeMedia(std::shared_ptr<FakeState> state):state_(std::move(state)){}
    bool startOffer()override{state_->events.push_back({VoiceMedia::EventKind::Description,"unit-offer","offer"});return true;}
    bool startAnswer(const std::string&)override{state_->events.push_back({VoiceMedia::EventKind::Description,"unit-answer","answer"});return true;}
    bool setAnswer(const std::string&)override{++state_->answers;return true;}
    bool addCandidate(const std::string&,const std::string&)override{++state_->candidates;return true;}
    bool setMuted(bool value)override{if(state_->muteFails)return false;state_->muted=value;return true;}
    void setDeafened(bool value)override{state_->deafened=value;}
    // Real VoiceMedia starts its device inside poll before returning Connected.
    std::vector<VoiceMedia::Event> poll()override{
        auto events=std::move(state_->events);state_->events.clear();
        for(const auto& event:events)if(event.kind==VoiceMedia::EventKind::Connected&&!state_->muted)++state_->unmutedDeviceStarts;
        return events;
    }
    VoiceMedia::Route route()override{return VoiceMedia::Route::TurnRelay;}
    void stop()override{++state_->stopped;}
};
struct Signal {std::string peer;Blob wire;};
struct Harness {
    calls::CallManager calls;
    std::string local,peer;
    bool enabled=true,authorized=true,connected=true,recovery=true,createFails=false,muteFails=false;
    unsigned created=0;
    std::vector<std::shared_ptr<FakeState>> states;
    std::deque<Signal> signals;
    App app;
    Harness(char me,char remote):local(64,me),peer(64,remote),app(calls,local,{
        [this]{return enabled;},[this](const std::string& id){return authorized&&id==peer;},
        [this](const std::string& id){return App::Control{connected&&id==peer,true,recovery};},
        [this](const std::string& id,const Blob& bytes){signals.push_back({id,bytes});return connected;},
        [this](const VoiceMedia::ReceiveBinding& binding)->std::unique_ptr<App::Media>{
            ++created;if(createFails)return {};
            auto state=std::make_shared<FakeState>();state->binding=binding;state->muteFails=muteFails;states.push_back(state);return std::make_unique<FakeMedia>(state);
        }}){}
    void event(VoiceMedia::EventKind kind){check(!states.empty(),"No fake media instance");states.back()->events.push_back({kind,{},{}});}
};
static Blob packet(protocol::Type type,const std::string& id,const std::string& text={},std::uint64_t generation=0){
    protocol::Writer w(type);w.string(id);if(generation)w.number(generation);if(!text.empty())w.string(text);return w.data;
}
static void deliver(Harness& from,Harness& to,std::uint64_t now){
    unsigned count=0;
    while(!from.signals.empty()){
        check(++count<256,"Unbounded signaling loop");auto signal=std::move(from.signals.front());from.signals.pop_front();check(signal.peer==to.local,"Signal addressed to unrelated peer");
        to.app.receive(from.local,signal.wire,now);
    }
}
static void exchange(Harness& a,Harness& b,std::uint64_t now){a.app.tick(now);b.app.tick(now);deliver(a,b,now);deliver(b,a,now);}
static void connect(Harness& a,Harness& b,const std::string& id,std::uint64_t now){
    check(a.app.start(a.peer,id,now),"Outgoing start rejected");exchange(a,b,now+1);
    check(b.calls.state()==calls::State::Incoming&&b.created==0,"Incoming signaling opened media before answer");
    check(b.app.answer(id,now+2),"Answer rejected");exchange(a,b,now+3);
    check(a.calls.state()==calls::State::ConnectingMedia&&b.calls.state()==calls::State::ConnectingMedia,"Signaling falsely claimed media connected");
    a.event(VoiceMedia::EventKind::Connected);b.event(VoiceMedia::EventKind::Connected);exchange(a,b,now+4);
    check(a.calls.state()==calls::State::Connected&&b.calls.state()==calls::State::Connected,"Current media events did not connect call");
}
int main(){try{
    const std::string one(32,'1'),two(32,'2'),three(32,'3');
    {
        Harness a('a','b');a.enabled=false;
        check(!a.app.start(a.peer,one,1)&&a.created==0&&a.calls.idle(),"Unavailable production gate created call/media");
        rejects([&]{a.app.receive(a.peer,packet(protocol::CallOffer,one,"offer"),1);});
        a.enabled=true;a.authorized=false;check(!a.app.start(a.peer,one,1)&&a.created==0,"Unverified friend started call");
        a.authorized=true;a.connected=false;check(!a.app.start(a.peer,one,1),"Offline signaling started call");
        a.connected=true;a.createFails=true;check(!a.app.start(a.peer,one,1)&&a.calls.idle(),"Factory failure left active call");
    }
    {
        Harness a('a','b'),b('b','a');check(a.app.start(a.peer,one,1),"Start failed");exchange(a,b,2);
        b.app.receive(b.peer,packet(protocol::CallOffer,one,"offer"),3);check(b.calls.state()==calls::State::Incoming&&b.created==0,"Duplicate offer started media");
        b.app.receive(b.peer,packet(protocol::CallMediaRestart,one,"restart",2),4);
        check(b.created==0&&b.calls.active()->mediaGeneration==1,"Recovery bypassed local incoming-call acceptance");
        for(unsigned n=0;n<100;++n){protocol::Writer w(protocol::CallCandidate);w.string(one);w.string("unit-candidate");w.string("0");b.app.receive(b.peer,w.data,4);}
        check(b.calls.active()->participants.at(b.peer).candidates.size()==64,"Candidate queue was unbounded");
        check(b.app.answer(one,5),"Answer failed");check(b.states.back()->candidates==64&&b.calls.active()->participants.at(b.peer).candidates.empty(),"Buffered candidates did not drain");exchange(a,b,6);
        a.app.receive(a.peer,packet(protocol::CallConnected,one),7);check(a.calls.state()==calls::State::ConnectingMedia,"Remote CallConnected claimed local media connected");
        check(a.app.end(one),"Hangup failed");deliver(a,b,8);check(a.calls.idle()&&b.calls.idle()&&a.states.front()->stopped==1&&b.states.front()->stopped==1,"Hangup leaked media");
        b.app.receive(b.peer,packet(protocol::CallOffer,one,"old-offer"),9);check(b.calls.idle(),"Completed call replay rang again");
    }
    {
        // A local network transition must not turn an unanswered incoming call
        // into a recovery offer. Exercise both deterministic-offerer orderings.
        for(const auto local:std::string("ab")){
            Harness incoming(local,local=='a'?'b':'a');
            incoming.app.receive(incoming.peer,packet(protocol::CallOffer,one,"offer"),100);
            for(const auto now:{200ULL,1200ULL,2200ULL}){
                incoming.app.networkChanged(now);incoming.app.tick(now+1);
                check(incoming.created==0&&incoming.states.empty(),"Local network recovery bypassed incoming-call acceptance");
                check(incoming.calls.state()==calls::State::Incoming&&incoming.calls.active()->mediaGeneration==1,
                      "Local network recovery mutated unanswered incoming call");
                check(incoming.signals.empty(),"Unanswered incoming call emitted recovery signaling");
            }
            check(incoming.app.setMuted(true),"Could not preserve mute intent while ringing");
            check(incoming.app.answer(one,2300)&&incoming.created==1,"Explicit answer no longer creates incoming media");
            incoming.event(VoiceMedia::EventKind::Connected);incoming.app.tick(2301);
            check(incoming.states.back()->unmutedDeviceStarts==0,"Incoming answer started microphone before restoring mute");
            check(incoming.app.end(one),"Answered incoming fixture did not end");
            Harness retry(local,local=='a'?'b':'a');
            retry.app.receive(retry.peer,packet(protocol::CallOffer,two,"offer"),100);
            // Defense at the recovery consumer, even if a caller has already
            // placed CallManager in retry state while consent is still pending.
            check(retry.calls.interruptMedia(200,30200),"Retry-state fixture failed");
            retry.app.tick(201);
            check(retry.created==0&&retry.signals.empty()&&retry.calls.state()==calls::State::Incoming&&retry.calls.active()->mediaGeneration==1,
                  "Existing retry state bypassed incoming-call acceptance");
            check(retry.app.decline(two),"Incoming retry fixture did not decline");
        }
    }
    {
        Harness incoming('a','b');incoming.muteFails=true;
        incoming.app.receive(incoming.peer,packet(protocol::CallOffer,one,"offer"),100);
        check(incoming.app.setMuted(true),"Incoming mute intent rejected");
        check(!incoming.app.answer(one,101)&&incoming.calls.idle(),"Failed initial mute preparation retained call");
        check(incoming.states.back()->stopped==1&&incoming.states.back()->unmutedDeviceStarts==0,
              "Failed mute preparation did not stop media before device start");
    }
    {
        Harness a('a','b'),b('b','a');connect(a,b,one,100);
        check(a.app.setMuted(true)&&b.app.setMuted(true)&&b.app.setDeafened(true),"Voice controls failed");
        const auto lifetime=a.calls.lifetime();auto old=a.states.back();
        a.app.networkChanged(200);check(old->stopped==1&&a.calls.active()->mediaState==calls::MediaState::Interrupted,"Network change did not stop old media");
        exchange(a,b,201);exchange(a,b,202);
        check(a.calls.active()->mediaGeneration==2&&b.calls.active()->mediaGeneration==2&&a.calls.lifetime()==lifetime,"Recovery replaced call lifetime or lost generation");
        check(a.states.back()->binding.callLifetime==lifetime&&a.states.back()->binding.mediaGeneration==2,"Media factory received wrong lifetime binding");
        old->events.push_back({VoiceMedia::EventKind::Connected,{},{}});exchange(a,b,203);
        check(a.calls.active()->mediaState!=calls::MediaState::Connected,"Stopped media completion connected replacement");
        protocol::Writer stale(protocol::CallMediaCandidate);stale.string(one);stale.number(3);stale.string("future-candidate");stale.string("0");
        auto accepted=a.states.back()->candidates;a.app.receive(a.peer,stale.data,204);check(a.states.back()->candidates==accepted,"Wrong media generation candidate accepted");
        a.event(VoiceMedia::EventKind::Connected);b.event(VoiceMedia::EventKind::Connected);exchange(a,b,205);
        check(a.states.back()->unmutedDeviceStarts==0,"Media recovery started microphone before restoring mute");
        check(b.states.back()->unmutedDeviceStarts==0,"Recovery answer started microphone before restoring mute");
        check(a.states.back()->muted&&b.states.back()->deafened,"Media recovery lost mute/deafen");
        a.connected=false;a.app.tick(300);a.app.tick(30400);
        check(a.calls.active()&&a.calls.active()->mediaState==calls::MediaState::Connected&&!a.calls.active()->controlAvailable,"Control loss killed healthy media");
        a.event(VoiceMedia::EventKind::Disconnected);a.app.tick(30500);a.app.tick(60501);check(a.calls.idle(),"Unavailable media recovery was not bounded");
        b.authorized=false;b.app.tick(60502);check(b.calls.idle()&&b.states.back()->stopped==1,"Revoked friendship retained audio");
    }
    {
        Harness a('a','b'),b('b','a');check(a.app.start(a.peer,one,1)&&b.app.start(b.peer,two,1),"Glare fixture failed");exchange(a,b,2);exchange(a,b,3);
        check(a.calls.active()&&a.calls.active()->id.value==one&&a.calls.state()==calls::State::Outgoing&&b.calls.active()&&b.calls.active()->id.value==one&&b.calls.state()==calls::State::Incoming,"Deterministic glare did not converge");
        check(b.states.front()->stopped==1,"Glare replacement leaked outgoing engine");
        check(b.app.decline(one),"Decline failed");deliver(b,a,4);check(a.calls.idle()&&b.calls.idle(),"Decline did not terminate both calls");
        check(a.app.start(a.peer,three,10),"Second lifetime start failed");a.app.tick(11);deliver(a,b,11);
        a.app.receive(a.peer,packet(protocol::CallEnd,one),12);check(a.calls.active()&&a.calls.active()->id.value==three,"Old call end terminated new lifetime");
        a.app.tick(30011);b.app.tick(30012);check(a.calls.idle()&&b.calls.idle(),"Ring timeout not bounded");
    }
    {
        Harness a('a','b');a.app.receive(a.peer,packet(protocol::CallOffer,one,"offer"),1);
        rejects([&]{a.app.receive(a.peer,packet(protocol::CallOffer,two,std::string(32769,'x')),2);});
        check(a.calls.active()->id.value==one&&a.created==0,"Malformed offer mutated current call");
        a.app.invalidate();check(a.calls.idle(),"Profile invalidation retained call");
    }
    std::cout<<"PASS: voice application UNIT controller semantics with explicit deterministic fake media; availability/auth gates, unchanged signaling, bounded candidates, consent, glare/replay, lifetime/generation fencing, recovery, mute/deafen, timeouts and teardown. NOT AUDIO OR E2E VALIDATION\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
