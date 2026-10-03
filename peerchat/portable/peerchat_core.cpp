#include "peerchat_core.h"
#include "../session_transport_manager.hpp"
#include "../group.hpp"
#include "../delivery_manager.hpp"
#include "../protocol.hpp"
#include "reliable_endpoint.hpp"
#include <chrono>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#if defined(__ANDROID__)
#include "android_storage_provider.hpp"
#include "android_tls_provider.hpp"
#include "../tls.hpp"
#endif

namespace {
struct Instance {
    // Existing owner, not a mobile session implementation. No profile/device is
    // authenticated and no route can be opened by this foundation ABI.
    peerchat::session::SessionTransportManager sessions{"portability-proof","",{1}};
    std::mutex gate;
#if defined(__ANDROID__)
    std::unique_ptr<peerchat::Storage> storage;
    peerchat::Profile profile;
    std::unique_ptr<peerchat::delivery::DeliveryManager> deliveries;
    // Provider objects only; existing SessionTransportManager will own route
    // association when networking is ported. No second reconnect/session policy.
    uint64_t nextTls=1;
    std::map<uint64_t,std::unique_ptr<peerchat::TlsSession>> tls;
    peerchat::reliable::AuthenticatedFriendSnapshot friends;
    // Concrete platform executions only; the one existing sessions/deliveries
    // above remain route/winner/ACK/retry/history authorities.
    std::map<uint64_t,std::unique_ptr<peerchat::portable::ReliableEndpoint>> peers;
#if defined(PEERCHAT_STORAGE_TEST_HOOKS)
    bool testProfile=false;
    std::shared_ptr<peerchat::platform::SqlDatabase> testDatabase;
#endif
#endif
};
struct Registry {
    std::mutex gate;uint64_t next=1;
    std::map<pc_core_handle,std::shared_ptr<Instance>> instances;
};
Registry& registry(){static Registry value;return value;}
std::shared_ptr<Instance> lookup(pc_core_handle handle){auto& r=registry();std::lock_guard<std::mutex> lock(r.gate);auto it=r.instances.find(handle);return it==r.instances.end()?nullptr:it->second;}
template<class F> pc_core_status guarded(F operation) noexcept {
    try{return operation();}catch(const std::bad_alloc&){return PC_CORE_OUT_OF_MEMORY;}catch(...){return PC_CORE_INTERNAL_ERROR;}
}
}
extern "C" {
pc_core_status pc_core_create(pc_core_handle* result){return guarded([&]()->pc_core_status{
    if(!result)return PC_CORE_INVALID_ARGUMENT;
    *result=0;auto instance=std::make_shared<Instance>();auto& r=registry();std::lock_guard<std::mutex> lock(r.gate);
    if(r.next==std::numeric_limits<uint64_t>::max())return PC_CORE_INTERNAL_ERROR;
    auto id=r.next++;r.instances.emplace(id,std::move(instance));*result=id;return PC_CORE_OK;
});}
pc_core_status pc_core_destroy(pc_core_handle handle){return guarded([&]()->pc_core_status{
    auto& r=registry();std::lock_guard<std::mutex> lock(r.gate);return r.instances.erase(handle)?PC_CORE_OK:PC_CORE_INVALID_HANDLE;
});}
}
#if defined(__ANDROID__)
namespace {
std::uint64_t runtimeNow(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
std::string peerArgument(JNIEnv* env,jbyteArray bytes){
    if(!bytes||env->GetArrayLength(bytes)!=64)throw std::invalid_argument("Invalid peer identity");
    auto copy=peerchat::platform::android::Context::output(env,bytes);std::string peer(copy.begin(),copy.end());
    peerchat::protocol::check(peerchat::protocol::id(peer));return peer;
}
void retireEndpoints(Instance& instance,peerchat::portable::ReliableEndpoint& endpoint){
    for(auto retired:endpoint.takeRetired()){
        auto found=instance.peers.find(retired.value);
        if(found!=instance.peers.end())found->second->close(false);
    }
}
void messageRecord(peerchat::protocol::Writer& value,const peerchat::StoredMessage& message){
    value.string(message.sender);value.string(message.recipient);value.number(message.utcMilliseconds);value.number(message.logicalOrder);
    value.number(static_cast<std::uint64_t>(message.sequence));value.number(static_cast<std::uint64_t>(message.delivery));value.text(message.text);
}
}
extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_ReliableBridge_friends(JNIEnv* env,jobject,jlong handle,jlong account,jlong generation,jbyteArray snapshot){
    return guarded([&]()->pc_core_status{
        if(!snapshot||account<0||generation<=0||env->GetArrayLength(snapshot)>262144)return PC_CORE_INVALID_ARGUMENT;
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return PC_CORE_INVALID_HANDLE;
        std::lock_guard<std::mutex> lock(instance->gate);if(!instance->storage)return PC_CORE_INVALID_ARGUMENT;
        auto bytes=peerchat::platform::android::Context::output(env,snapshot);peerchat::protocol::Reader reader(bytes);
        peerchat::protocol::check(reader.type()==peerchat::protocol::Hello);auto count=reader.number();peerchat::protocol::check(count<=1024);
        std::vector<peerchat::reliable::FriendIdentity> values;
        for(std::uint64_t i=0;i<count;++i){
            peerchat::reliable::FriendIdentity value;value.accountId=reader.number();value.expectedPeerId=reader.string();
            peerchat::protocol::check(value.accountId&&peerchat::protocol::id(value.expectedPeerId));
            auto known=reader.number();peerchat::protocol::check(known<=32);
            for(std::uint64_t j=0;j<known;++j){auto peer=reader.string();peerchat::protocol::check(peerchat::protocol::id(peer));value.knownPeerIds.push_back(std::move(peer));}
            values.push_back(std::move(value));
        }
        reader.end();peerchat::protocol::check(account||values.empty());
        instance->friends.replace(static_cast<std::uint64_t>(account),static_cast<std::uint64_t>(generation),values);
        for(auto& peer:instance->peers)if(!instance->friends.contains(peer.second->expected())){
            peer.second->close(false);instance->sessions.removePeer(peer.second->expected());
        }
        return PC_CORE_OK;
    });
}
extern "C" JNIEXPORT jlong JNICALL Java_com_peerchat_android_ReliableBridge_open(JNIEnv* env,jobject,jlong handle,jboolean incoming,jbyteArray pin){
    try{
        auto expected=peerArgument(env,pin);auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return 0;
        std::lock_guard<std::mutex> lock(instance->gate);
        if(!instance->storage||!instance->friends.contains(expected)||instance->tls.size()+instance->peers.size()>=32||instance->nextTls==std::numeric_limits<uint64_t>::max())return 0;
        auto identity=std::dynamic_pointer_cast<peerchat::platform::android::Identity>(instance->profile.identity);if(!identity)return 0;
        auto factory=std::make_shared<peerchat::platform::android::TlsFactory>(identity->context);
        peerchat::TlsCredentials credentials(instance->profile,std::move(factory));
        auto tls=std::make_unique<peerchat::TlsSession>(credentials,incoming==JNI_TRUE,expected);
        auto id=instance->nextTls++;
        peerchat::session::RouteObservation route{{id},"relay:"+expected,"authenticated server","opaque relay",incoming==JNI_TRUE,1,
            peerchat::session::TransportKind::Relay,peerchat::session::RouteClass::ServerRelay};
        auto endpoint=std::make_unique<peerchat::portable::ReliableEndpoint>(instance->profile,*instance->storage,instance->sessions,*instance->deliveries,
            instance->friends,peerchat::session::SessionEpoch{1},peerchat::session::TransportInstanceId{id},std::move(tls),expected,route,runtimeNow());
        instance->peers.emplace(id,std::move(endpoint));return static_cast<jlong>(id);
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return 0;}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_ReliableBridge_exchange(JNIEnv* env,jobject,jlong handle,jlong token,jint operation,jbyteArray input){
    try{
        if(!input||env->GetArrayLength(input)>65536)return nullptr;
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;
        std::lock_guard<std::mutex> lock(instance->gate);auto found=instance->peers.find(static_cast<std::uint64_t>(token));if(found==instance->peers.end())return nullptr;
        auto& endpoint=*found->second;peerchat::Blob output;
        try{
            if(operation==0){endpoint.receive(peerchat::platform::android::Context::output(env,input),runtimeNow());retireEndpoints(*instance,endpoint);}
            else{
                if(env->GetArrayLength(input)!=0)return nullptr;
                if(operation==1)output=endpoint.takeOutput();
                else if(operation==2)output={static_cast<unsigned char>(endpoint.ready()),static_cast<unsigned char>(endpoint.closed())};
                else if(operation==3){
                    output={static_cast<unsigned char>(endpoint.initialPin(instance->friends.generation(),runtimeNow()))};retireEndpoints(*instance,endpoint);
                }else if(operation==4){
                    if(auto event=endpoint.poll()){
                        peerchat::protocol::Writer value(peerchat::protocol::Ack);value.number(static_cast<std::uint64_t>(event->kind));value.string(event->peer);value.string(event->id);value.number(event->message?1:0);
                        if(event->message)messageRecord(value,*event->message);
                        output=std::move(value.data);
                    }
                }else return nullptr;
            }
        }catch(...){endpoint.close(false);throw;}
        return peerchat::platform::android::Context::input(env,output);
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_ReliableBridge_close(JNIEnv*,jobject,jlong handle,jlong token,jboolean recoverable){
    return guarded([&]()->pc_core_status{
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return PC_CORE_INVALID_HANDLE;
        std::lock_guard<std::mutex> lock(instance->gate);auto found=instance->peers.find(static_cast<std::uint64_t>(token));if(found==instance->peers.end())return PC_CORE_INVALID_HANDLE;
        found->second->close(recoverable==JNI_TRUE);instance->peers.erase(found);return PC_CORE_OK;
    });
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_ReliableBridge_send(JNIEnv* env,jobject,jlong handle,jbyteArray destination,jbyteArray text){
    try{
        if(!text||env->GetArrayLength(text)>16384)return nullptr;
        auto peer=peerArgument(env,destination);auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;
        std::lock_guard<std::mutex> lock(instance->gate);
        if(!instance->storage||!instance->friends.contains(peer))return nullptr;
        auto bytes=peerchat::platform::android::Context::output(env,text);
        auto content=peerchat::unicode::decode(std::string(bytes.begin(),bytes.end()));
        auto contacts=instance->storage->contacts();auto known=std::find_if(contacts.begin(),contacts.end(),[&](const auto& value){return value.peerId==peer;});
        auto id=peerchat::Storage::newMessageId();
        peerchat::reliable::queueText(*instance->storage,instance->profile,known==contacts.end()?nullptr:&*known,peer,id,content,
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()));
        // A committed message remains a successful durable send even if this
        // particular route fails. Retire it; the same message ID stays pending.
        for(auto& endpoint:instance->peers)if(endpoint.second->expected()==peer){
            try{endpoint.second->pump();}catch(...){endpoint.second->close(false);}
        }
        return peerchat::platform::android::Context::input(env,peerchat::Blob(id.begin(),id.end()));
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_ReliableBridge_history(JNIEnv* env,jobject,jlong handle,jbyteArray destination,jlong before){
    try{
        if(before<=0)return nullptr;
        auto peer=peerArgument(env,destination);auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;
        std::lock_guard<std::mutex> lock(instance->gate);if(!instance->storage)return nullptr;
        auto history=instance->storage->history(peer,8,static_cast<std::int64_t>(before));
        peerchat::protocol::Writer record(peerchat::protocol::Text);record.number(history.size());
        for(const auto& message:history){record.string(message.id);messageRecord(record,message);}
        return peerchat::platform::android::Context::input(env,record.data);
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
#endif
extern "C" {
pc_core_status pc_core_get_info(pc_core_handle handle,pc_core_info* info){return guarded([&]()->pc_core_status{
    if(!info||info->struct_size!=sizeof(pc_core_info))return PC_CORE_INVALID_ARGUMENT;
    auto instance=lookup(handle);if(!instance)return PC_CORE_INVALID_HANDLE;
    std::lock_guard<std::mutex> lock(instance->gate);
    uint32_t loaded=0;
#if defined(__ANDROID__)
    loaded=instance->storage?1:0;
#endif
    *info={sizeof(pc_core_info),PC_CORE_ABI_VERSION,1,loaded,loaded,loaded};return PC_CORE_OK;
});}
pc_core_status pc_core_exchange_utf8(pc_core_handle handle,const uint8_t* input,uint32_t length,uint8_t* output,uint32_t capacity,uint32_t* required){return guarded([&]()->pc_core_status{
    if(!required)return PC_CORE_INVALID_ARGUMENT;
    *required=0;if((!input&&length)||(!output&&capacity)||length>PC_CORE_MAX_EXCHANGE_BYTES)return PC_CORE_INVALID_ARGUMENT;
    auto instance=lookup(handle);if(!instance)return PC_CORE_INVALID_HANDLE;
    std::lock_guard<std::mutex> lock(instance->gate);
    std::string text;if(length)text.assign(reinterpret_cast<const char*>(input),length);
    try{(void)peerchat::unicode::decode(text);}catch(const std::invalid_argument&){return PC_CORE_INVALID_UTF8;}
    peerchat::protocol::Writer wire(peerchat::protocol::Text);wire.string(text);
    peerchat::protocol::Reader reader(wire.data);auto copy=reader.string();reader.end();
    *required=static_cast<uint32_t>(copy.size());if(capacity<copy.size())return PC_CORE_BUFFER_TOO_SMALL;
    if(!copy.empty())std::memmove(output,copy.data(),copy.size());
    return PC_CORE_OK;
});}
pc_core_status pc_core_open_profile(pc_core_handle handle,const uint8_t* path,uint32_t length){return guarded([&]()->pc_core_status{
    if(!path||!length||length>32768)return PC_CORE_INVALID_ARGUMENT;
    if(!lookup(handle))return PC_CORE_INVALID_HANDLE;
    return PC_CORE_PLATFORM_UNAVAILABLE;
});}
}
#if defined(__ANDROID__)
extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_ProfileBridge_bind(JNIEnv* env,jobject,jlong handle,jobject facade){
    return guarded([&]()->pc_core_status{
        if(!facade)return PC_CORE_INVALID_ARGUMENT;
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return PC_CORE_INVALID_HANDLE;
        std::lock_guard<std::mutex> lock(instance->gate);if(instance->storage)return PC_CORE_INVALID_ARGUMENT;
        auto providers=peerchat::platform::android::platform(env,facade);
#if defined(PEERCHAT_STORAGE_TEST_HOOKS)
        auto context=std::static_pointer_cast<peerchat::platform::android::Identity>(providers->identity)->context;
        auto scope=context->bytes("scope","()[B");std::string name(scope.begin(),scope.end());
        instance->testProfile=name.rfind("test_",0)==0;
        if(instance->testProfile)instance->testDatabase=providers->database;
#endif
        auto store=std::make_unique<peerchat::Storage>(providers);auto profile=store->profile();
        auto delivery=std::make_unique<peerchat::delivery::DeliveryManager>(*store,profile.peerId);
        // Rebind the existing session owner; no second session/retry owner.
        instance->sessions=peerchat::session::SessionTransportManager(profile.peerId,profile.peerId,{1});
        instance->profile=std::move(profile);instance->storage=std::move(store);instance->deliveries=std::move(delivery);
        return PC_CORE_OK;
    });
}
extern "C" JNIEXPORT jlong JNICALL Java_com_peerchat_android_PeerTlsBridge_open(JNIEnv* env,jobject,jlong handle,jboolean incoming,jbyteArray pin){
    try{
        if(!pin || env->GetArrayLength(pin)!=64)return 0;
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return 0;
        std::lock_guard<std::mutex> lock(instance->gate);
        if(!instance->storage || instance->tls.size()+instance->peers.size()>=32 || instance->nextTls==std::numeric_limits<uint64_t>::max())return 0;
        auto bytes=peerchat::platform::android::Context::output(env,pin);
        auto identity=std::dynamic_pointer_cast<peerchat::platform::android::Identity>(instance->profile.identity);
        if(!identity)return 0;
        auto factory=std::make_shared<peerchat::platform::android::TlsFactory>(identity->context);
        peerchat::TlsCredentials credentials(instance->profile,std::move(factory));
        auto session=std::make_unique<peerchat::TlsSession>(credentials,incoming==JNI_TRUE,std::string(bytes.begin(),bytes.end()));
        auto id=instance->nextTls++;instance->tls.emplace(id,std::move(session));return static_cast<jlong>(id);
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return 0;}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_PeerTlsBridge_exchange(JNIEnv* env,jobject,jlong handle,jlong token,jint operation,jbyteArray input){
    try{
        if(!input || env->GetArrayLength(input)>262144)return nullptr;
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;
        std::lock_guard<std::mutex> lock(instance->gate);
        auto found=instance->tls.find(static_cast<uint64_t>(token));if(found==instance->tls.end())return nullptr;
        auto& session=*found->second;peerchat::Blob output;
        try{
            if(operation==0 || operation==1){
                auto bytes=peerchat::platform::android::Context::output(env,input);
                if(operation==0)session.receive(bytes);else session.send(bytes);
            }else{
                if(env->GetArrayLength(input)!=0)return nullptr;
                switch(operation){
                    case 2:output=session.takeOutput();break;
                    case 3:output=session.takePlaintext();break;
                    case 4:output=session.remotePublicKey();break;
                    case 5:output={static_cast<unsigned char>(session.ready()),static_cast<unsigned char>(session.closed())};break;
                    case 6:session.shutdown();break;
                    default:return nullptr;
                }
            }
        }catch(...){instance->tls.erase(found);throw;}
        return peerchat::platform::android::Context::input(env,output);
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_PeerTlsBridge_close(JNIEnv*,jobject,jlong handle,jlong token){
    return guarded([&]()->pc_core_status{
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return PC_CORE_INVALID_HANDLE;
        std::lock_guard<std::mutex> lock(instance->gate);
        return instance->tls.erase(static_cast<uint64_t>(token))?PC_CORE_OK:PC_CORE_INVALID_HANDLE;
    });
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_ProfileBridge_publicKey(JNIEnv* env,jobject,jlong handle){
    try{auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;std::lock_guard<std::mutex> lock(instance->gate);
        if(!instance->storage)return nullptr;
        return peerchat::platform::android::Context::input(env,instance->profile.publicKey);
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_ProfileBridge_sign(JNIEnv* env,jobject,jlong handle,jbyteArray bytes){
    try{if(!bytes||env->GetArrayLength(bytes)>70000)return nullptr;auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;
        std::lock_guard<std::mutex> lock(instance->gate);if(!instance->storage)return nullptr;
        auto value=peerchat::platform::android::Context::output(env,bytes);
        return peerchat::platform::android::Context::input(env,peerchat::signDevicePayload(value,instance->profile));
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
#if defined(PEERCHAT_STORAGE_TEST_HOOKS)
// Debug-only synthetic fixture. Never available for the normal profile or release.
extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_ProfileBridge_storageTest(JNIEnv*,jobject,jlong handle,jint mode){
    return guarded([&]()->pc_core_status{
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return PC_CORE_INVALID_HANDLE;
        std::lock_guard<std::mutex> lock(instance->gate);
        if(!instance->storage||!instance->testProfile||mode<0||mode>3)return PC_CORE_INVALID_ARGUMENT;
        auto& store=*instance->storage;auto& delivery=*instance->deliveries;
        const std::string peer(64,'b'),id(32,'c');
        const std::string utf8=u8"Native Android profile proof / 日本語 / 😀";
        peerchat::StoredMessage message{id,instance->profile.peerId,peer,peerchat::unicode::decode(utf8)+std::wstring(1,L'\0')+L"tail",42,peerchat::Delivery::Queued,0,1};
        auto check=[](bool value){if(!value)throw std::runtime_error("Native storage proof failed");};
        if(mode==0){store.append(peer,message);check(!store.append(peer,message));check(store.pending(peer).size()==1);}
        if(mode==1){check(delivery.authorizeTransport({77},peer)&&delivery.submittedMessage(peer,id,{77})&&delivery.acknowledgeMessage(peer,{77},id));}
        if(mode==2){check(store.pending(peer).empty());}
        auto history=store.history(peer);check(history.size()==1&&history[0].text==message.text&&history[0].logicalOrder==1);
        if(mode==1||mode==2)check(history[0].delivery==peerchat::Delivery::Delivered);
        if(mode==3){
            auto query=instance->testDatabase->prepare("SELECT b FROM messages WHERE id=?");query->bindText(1,id);check(query->row());auto bad=query->bytes(0);query.reset();bad[0]^=255;
            auto update=instance->testDatabase->prepare("UPDATE messages SET b=? WHERE id=?");update->bindBlob(1,bad);update->bindText(2,id);update->row();update.reset();
            bool rejected=false;try{store.history(peer);}catch(...){rejected=true;}check(rejected);
        }
        return PC_CORE_OK;
    });
}
#endif
#endif
