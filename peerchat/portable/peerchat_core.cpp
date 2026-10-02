#include "peerchat_core.h"
#include "../session_transport_manager.hpp"
#include "../group.hpp"
#include "../delivery_manager.hpp"
#include "../protocol.hpp"
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
        if(!instance->storage || instance->tls.size()>=32 || instance->nextTls==std::numeric_limits<uint64_t>::max())return 0;
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
