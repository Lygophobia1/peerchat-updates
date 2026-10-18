// Host boundary test: compile the production GroupVoiceBridge JNI entry point
// with fake profile/media ports. No Android device or audio claim is made.
#include "../protocol.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <mutex>
#include <vector>
using jlong=std::int64_t;using jint=int;using jobject=void*;using jbyteArray=peerchat::Blob*;
using pc_core_handle=std::uint64_t;
#define JNIEXPORT
#define JNICALL
struct JNIEnv {bool ExceptionCheck(){return false;}void ExceptionClear(){}};
namespace peerchat {
struct VoiceRoomBinding {std::string group,room,epoch,localDevice,participation;
    std::uint64_t localAccount=0,revision=0,rosterGeneration=0,generation=0;};
struct AuthorizedVoiceSource {std::uint64_t account=0;std::string device,participation;};
struct VoiceTransportSignal {std::string sourceKey,routeId,kind,data,mid;std::uint64_t generation=0;};
struct MediaIceConfiguration {std::string host,username,password;std::uint16_t port=0;std::int64_t expiresUtc=0;
    std::uint64_t generation=0,profileSessionGeneration=0,discoveryGeneration=0;};
namespace platform::android {struct Context {static jbyteArray input(JNIEnv*,const Blob& bytes){static Blob result;result=bytes;return &result;}};}
}
struct FakeGroupVoice {
    struct Route {bool connected=false;};struct Receive {std::uint64_t acceptedPackets=0,mixedBlocks=0,underrunBlocks=0;};
    struct Snapshot {bool muted=false,deafened=false;std::uint64_t encodedPackets=0;Receive receive;std::vector<Route> routes;};
    peerchat::VoiceRoomBinding bound{std::string(32,'a'),std::string(32,'b'),std::string(32,'c'),std::string(64,'e'),std::string(32,'d'),71,4,3,9};
    bool live=true;int mutations=0,drains=0,starts=0;
    bool current(const void*,std::uint64_t,std::uint64_t,const std::string&){return live;}
    bool active()const{return live;}bool audioFailed()const{return false;}
    const peerchat::VoiceRoomBinding& binding()const{return bound;}
    Snapshot snapshot()const{return {};}
    bool matches(const peerchat::VoiceRoomBinding& b)const{return live&&b.group==bound.group&&b.room==bound.room&&
        b.epoch==bound.epoch&&b.participation==bound.participation&&b.generation==bound.generation&&
        b.localAccount==bound.localAccount&&b.localDevice==bound.localDevice;}
    bool start(peerchat::VoiceRoomBinding,std::uint64_t,const void*,peerchat::MediaIceConfiguration,
               const std::vector<peerchat::AuthorizedVoiceSource>&){++starts;return true;}
    bool reconcile(const peerchat::VoiceRoomBinding&,std::uint64_t,std::uint64_t,
                   const std::vector<peerchat::AuthorizedVoiceSource>&,const void*,std::uint64_t){++mutations;return true;}
    bool configure(const peerchat::VoiceRoomBinding&,peerchat::MediaIceConfiguration,std::uint64_t){++mutations;return true;}
    bool signal(const peerchat::VoiceRoomBinding&,const peerchat::VoiceTransportSignal&){++mutations;return true;}
    std::vector<peerchat::VoiceTransportSignal> takeSignals(const peerchat::VoiceRoomBinding&){++drains;return {};}
    bool mute(const peerchat::VoiceRoomBinding&,bool){++mutations;return true;}
    bool deafen(const peerchat::VoiceRoomBinding&,bool){++mutations;return true;}
    bool stop(const peerchat::VoiceRoomBinding&){++mutations;live=false;return true;}
};
struct FakeFriends {std::uint64_t localAccount()const{return 71;}std::uint64_t generation()const{return 4;}};
struct FakeDm {bool live=false;const void* active()const{return live?this:nullptr;}};
struct FakeInstance {std::mutex gate;bool storage=true;FakeFriends friends;struct {std::string peerId=std::string(64,'e');}profile;
    std::unique_ptr<int> groups=std::make_unique<int>(1);std::unique_ptr<FakeGroupVoice> groupVoice=std::make_unique<FakeGroupVoice>();
    struct {FakeDm app;} dm;decltype(&dm) voice=&dm;};
static std::shared_ptr<FakeInstance> current;
static std::shared_ptr<FakeInstance> lookup(pc_core_handle handle){return handle==17?current:nullptr;}
static peerchat::Blob mailboxArgument(JNIEnv*,jbyteArray bytes,std::size_t maximum){peerchat::protocol::check(bytes&&bytes->size()<=maximum);return *bytes;}
static bool androidVoiceAvailable(){return true;}
#include "android_group_voice_jni_under_test.inc"
static void check(bool condition,const char* reason){if(!condition)throw std::runtime_error(reason);}
static peerchat::protocol::Writer header(int op,std::uint64_t account=71,std::uint64_t generation=4){
    peerchat::protocol::Writer w(peerchat::protocol::CallOffer);w.string("PeerChat/AndroidGroupVoice/v1");w.number(op);
    w.number(account);w.number(generation);return w;}
static peerchat::Blob command(int op,const peerchat::VoiceRoomBinding& b){auto w=header(op);w.string(b.group);w.string(b.room);
    w.string(b.epoch);w.string(b.participation);w.number(b.generation);
    if(op==2){w.number(5);w.number(3);w.number(0);}else if(op==3){w.string("turn.invalid");w.number(3478);w.string("user");
        w.string("password");w.number(2000000000);w.number(2);}else if(op==4){w.string("23:"+std::string(64,'f')+":"+std::string(32,'1'));
        w.string(std::string(32,'2'));w.number(1);w.string("offer");w.string("sdp");w.string("0");}
    else if(op==6||op==7)w.number(1);
    return w.data;}
static jbyteArray request(int op,peerchat::Blob bytes,jlong handle=17){JNIEnv env;return Java_com_peerchat_android_GroupVoiceBridge_request(&env,nullptr,handle,op,&bytes);}
int main(){try{
    for(int op=2;op<=8;++op)for(int field=0;field<5;++field){
        current=std::make_shared<FakeInstance>();auto stale=current->groupVoice->bound;
        if(field==0)stale.group=std::string(32,'2');
        if(field==1)stale.room=std::string(32,'2');
        if(field==2)stale.epoch=std::string(32,'2');
        if(field==3)stale.participation=std::string(32,'2');
        if(field==4)stale.generation=8;
        auto reply=request(op,command(op,stale));check(reply!=nullptr,"Stale scope failed to return bounded rejection");
        peerchat::protocol::Reader r(*reply);check(r.string()=="PeerChat/AndroidGroupVoice/v1"&&r.number()==static_cast<unsigned>(op)&&r.number()==0,
            "Stale scope was accepted");check(current->groupVoice->mutations==0&&current->groupVoice->drains==0,
            "Stale scope changed media or drained signals");
    }
    current=std::make_shared<FakeInstance>();auto valid=current->groupVoice->bound;
    check(request(6,command(6,valid))!=nullptr&&current->groupVoice->mutations==1,"Matching scope was not dispatched");
    auto bad=command(6,valid);bad.push_back(0);check(request(6,bad)==nullptr,"Trailing data was accepted");
    check(current->groupVoice->mutations==1,"Malformed command changed media");
    auto wrong=header(6,72);wrong.string(valid.group);wrong.string(valid.room);wrong.string(valid.epoch);
    wrong.string(valid.participation);wrong.number(valid.generation);wrong.number(1);
    check(request(6,wrong.data)==nullptr&&current->groupVoice->mutations==1,"Wrong account changed media");
    current=std::make_shared<FakeInstance>();current->groupVoice->live=false;current->dm.app.live=true;
    auto start=command(1,valid);peerchat::protocol::Reader unused(start);(void)unused;
    auto w=header(1);w.string(valid.group);w.string(valid.room);w.string(valid.epoch);w.string(valid.participation);w.number(10);
    w.number(4);w.number(3);w.number(0);w.string("turn.invalid");w.number(3478);w.string("user");w.string("password");
    w.number(2000000000);w.number(2);
    check(request(1,w.data)!=nullptr&&current->groupVoice->starts==0,"DM active allowed group start");
    std::cout<<"PASS: production group voice JNI rejects stale scope/account/malformed commands before media mutation and prevents DM/group overlap.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
