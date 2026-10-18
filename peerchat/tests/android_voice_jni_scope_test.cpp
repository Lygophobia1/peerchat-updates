// Boundary unit test: the included function is extracted verbatim from the
// production JNI entry point. Only JNI/environment/application ports are fake;
// record parsing and call lifetime matching use their production definitions.
#include "../protocol.hpp"
#include "../call_manager.hpp"
#include <iostream>
#include <memory>
#include <mutex>

using jlong=std::int64_t;
using jint=int;
using jobject=void*;
using jbyteArray=peerchat::Blob*;
using pc_core_handle=std::uint64_t;
#define JNIEXPORT
#define JNICALL
struct JNIEnv {bool ExceptionCheck(){return false;}void ExceptionClear(){}};

namespace peerchat {
struct MediaIceConfiguration {
    std::string host,username,password;
    std::uint16_t port=0;
    std::int64_t expiresUtc=0;
    std::uint64_t generation=0,profileSessionGeneration=0,discoveryGeneration=0;
};
namespace platform::android {
struct Context {
    static jbyteArray input(JNIEnv*,const Blob& bytes){static Blob result;result=bytes;return &result;}
};
}
}
using namespace peerchat;
struct FakeApp {
    calls::CallManager& calls;
    int actions=0,lastOperation=0;
    bool lastValue=false;
    bool available()const{return true;}
    const char* diagnostic()const{return "JNI unit port";}
    const calls::CallSession* active()const{return calls.active();}
    bool action(int op){++actions;lastOperation=op;return true;}
    bool start(const std::string&,const std::string&,std::uint64_t){return action(1);}
    bool answer(const std::string&,std::uint64_t){return action(2);}
    bool decline(const std::string&){return action(3);}
    bool end(const std::string&){calls.finish(calls::State::Ended);return action(4);}
    bool setMuted(bool value){lastValue=value;calls.setMuted(value);return action(5);}
    bool setDeafened(bool value){lastValue=value;calls.setDeafened(value);return action(6);}
};
struct FakeVoice {
    FakeApp app;
    int ticks=0;
    bool expireOnTick=false;
    explicit FakeVoice(calls::CallManager& calls):app{calls}{}
    void tick(std::uint64_t){++ticks;if(expireOnTick)app.calls.finish(calls::State::Ended);}
    bool installIce(MediaIceConfiguration){return true;}
};
struct FakeFriends {
    std::uint64_t localAccount()const{return 71;}
    std::uint64_t generation()const{return 4;}
};
struct FakeInstance {
    std::mutex gate;
    FakeFriends friends;
    calls::CallManager calls;
    std::unique_ptr<FakeVoice> voice=std::make_unique<FakeVoice>(calls);
};
static std::shared_ptr<FakeInstance> current;
static std::shared_ptr<FakeInstance> lookup(pc_core_handle handle){return handle==17?current:nullptr;}
static Blob mailboxArgument(JNIEnv*,jbyteArray input,std::size_t maximum){protocol::check(input&&input->size()<=maximum);return *input;}
static std::uint64_t runtimeNow(){return 999999;}
static const char* androidVoiceUnavailableReason(){return "Unavailable";}

#include "android_voice_jni_request_under_test.inc"

static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static const std::string peer(64,'a'),id(32,'1');
static void fixture(){
    current=std::make_shared<FakeInstance>();
    check(current->calls.beginOutgoing({std::string(32,'0')},calls::CallType::Voice,{peer},1),"Old fixture failed");
    current->calls.finish(calls::State::Ended);
    check(current->calls.beginOutgoing({id},calls::CallType::Voice,{peer},1),"Replacement fixture failed");
    current->voice->expireOnTick=true;
}
static protocol::Writer header(int op,std::uint64_t account=71,std::uint64_t generation=4){
    protocol::Writer w(protocol::CallOffer);w.string("PeerChat/AndroidVoice/v1");w.number(op);w.number(account);w.number(generation);return w;
}
static Blob command(int op,const std::string& targetPeer=peer,const std::string& targetId=id,std::uint64_t lifetime=2,std::uint64_t value=1){
    auto w=header(op);w.string(targetPeer);w.string(targetId);w.number(lifetime);if(op>=5)w.number(value);return w.data;
}
static jbyteArray request(int op,Blob bytes,jlong handle=17){
    JNIEnv env;return Java_com_peerchat_android_VoiceBridge_request(&env,nullptr,handle,op,&bytes);
}
static void rejectedWithoutMutation(int op,const Blob& bytes,jlong handle=17){
    fixture();check(request(op,bytes,handle)==nullptr,"Invalid command was accepted");
    check(current->calls.matches(2,id,peer),"Rejected command released or replaced current call");
    check(!current->calls.active()->muted&&!current->calls.active()->deafened,"Rejected command changed current controls");
    check(current->voice->app.actions==0&&current->voice->ticks==0,"Rejected command dispatched action or tick");
}
int main(){try{
    for(int op=2;op<=6;++op){
        rejectedWithoutMutation(op,command(op,std::string(64,'b')));
        rejectedWithoutMutation(op,command(op,peer,std::string(32,'2')));
        rejectedWithoutMutation(op,command(op,peer,id,1));
        rejectedWithoutMutation(op,command(op,"bad"));
        rejectedWithoutMutation(op,command(op,peer,"bad"));
        rejectedWithoutMutation(op,command(op,peer,id,0));
        rejectedWithoutMutation(op,command(op,peer,id,UINT64_MAX));
        auto missing=header(op);missing.string(peer);missing.string(id);rejectedWithoutMutation(op,missing.data);
        auto legacy=header(op);if(op>=5)legacy.number(1);else legacy.string(id);rejectedWithoutMutation(op,legacy.data);
        auto trailing=command(op);trailing.push_back(0);rejectedWithoutMutation(op,trailing);
        if(op>=5)rejectedWithoutMutation(op,command(op,peer,id,2,2));
        auto wrongAccount=header(op,72);wrongAccount.string(peer);wrongAccount.string(id);wrongAccount.number(2);if(op>=5)wrongAccount.number(1);
        rejectedWithoutMutation(op,wrongAccount.data);
        auto staleGeneration=header(op,71,3);staleGeneration.string(peer);staleGeneration.string(id);staleGeneration.number(2);if(op>=5)staleGeneration.number(1);
        rejectedWithoutMutation(op,staleGeneration.data);
        rejectedWithoutMutation(op,command(op),99);
        fixture();current->voice->expireOnTick=false;
        auto bytes=request(op,command(op));check(bytes!=nullptr,"Matching target was rejected");
        protocol::Reader r(*bytes);check(r.type()==protocol::CallOffer&&r.string()=="PeerChat/AndroidVoice/v1"&&r.number()==static_cast<unsigned>(op)&&r.number()==1,"Reply header or acceptance changed");
        check(current->voice->app.actions==1&&current->voice->app.lastOperation==op&&current->voice->ticks==1,"Matching target dispatch changed");
        if(op>=5)check(current->voice->app.lastValue,"Control value was lost");
    }
    fixture();current->voice->expireOnTick=false;auto start=header(1);start.string(peer);start.string(id);
    check(request(1,start.data)!=nullptr&&current->voice->app.lastOperation==1,"Start record changed");
    check(request(0,header(0).data)!=nullptr,"Snapshot record changed");
    auto ice=header(7);ice.string("turn.invalid");ice.number(3478);ice.string("user");ice.string("password");ice.number(2000000000);ice.number(19);
    check(request(7,ice.data)!=nullptr,"ICE record changed");
    std::cout<<"PASS: production Android Voice JNI request boundary: stale peer/id/lifetime, malformed records, account/generation, no rejected-command tick or replacement mutation, matching dispatch. JNI/app ports are fake; no audio/device proof.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
