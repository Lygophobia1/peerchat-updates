#include "../platform/windows_recipient_key.hpp"
#include <jni.h>
#include <map>
#include <mutex>
using namespace peerchat::platform;
namespace {
struct Fixture {
    std::string alias;
    std::shared_ptr<RecipientKeyHandle> key;
    explicit Fixture(std::string name):alias(std::move(name)) {
        recipient_cng::require(alias.rfind("PeerChat.Recipient.test.",0)==0);
        WindowsRecipientKeyProvider provider; key=provider.create(alias);
    }
    ~Fixture() {
        key.reset(); // Destroy only the exact TEST container created above.
        recipient_cng::Object provider,handle;
        if(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0)!=ERROR_SUCCESS) return;
        auto name=recipient_cng::alias(alias);
        if(NCryptOpenKey(provider.value,&handle.value,name.c_str(),0,NCRYPT_SILENT_FLAG)==ERROR_SUCCESS &&
           NCryptDeleteKey(handle.value,0)==ERROR_SUCCESS) handle.value=0;
    }
};
std::mutex mutex;
std::map<jlong,std::unique_ptr<Fixture>> fixtures;
jlong next=1;
void failed(JNIEnv* env) { env->ThrowNew(env->FindClass("java/lang/IllegalStateException"),"Opaque CNG fixture rejected operation"); }
jbyteArray output(JNIEnv* env,const std::vector<unsigned char>& data) {
    auto result=env->NewByteArray(static_cast<jsize>(data.size()));
    if(result) env->SetByteArrayRegion(result,0,static_cast<jsize>(data.size()),reinterpret_cast<const jbyte*>(data.data()));
    return result;
}
Fixture& get(jlong id) { auto found=fixtures.find(id); if(found==fixtures.end()) throw std::runtime_error("Invalid fixture handle"); return *found->second; }
}
extern "C" JNIEXPORT jlong JNICALL Java_NativeCngHpkeFixture_create(JNIEnv* env,jclass,jstring alias) {
    try {
        if(!alias || env->GetStringUTFLength(alias)>200) throw std::runtime_error("Invalid test alias");
        const char* bytes=env->GetStringUTFChars(alias,nullptr); if(!bytes) return 0;
        std::string name(bytes); env->ReleaseStringUTFChars(alias,bytes);
        std::lock_guard<std::mutex> guard(mutex); const auto id=next++;
        fixtures.emplace(id,std::make_unique<Fixture>(name)); return id;
    } catch(...) { failed(env); return 0; }
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_NativeCngHpkeFixture_publicKey(JNIEnv* env,jclass,jlong id) {
    try { std::lock_guard<std::mutex> guard(mutex); return output(env,get(id).key->publicKey()); }
    catch(...) { failed(env); return nullptr; }
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_NativeCngHpkeFixture_agree(JNIEnv* env,jclass,jlong id,jbyteArray encoded) {
    try {
        if(!encoded || env->GetArrayLength(encoded)!=65) throw std::runtime_error("Invalid point length");
        std::vector<unsigned char> point(65);
        env->GetByteArrayRegion(encoded,0,65,reinterpret_cast<jbyte*>(point.data())); if(env->ExceptionCheck()) return nullptr;
        std::lock_guard<std::mutex> guard(mutex); auto dh=get(id).key->agreeP256(point);
        auto result=output(env,dh); SecureZeroMemory(dh.data(),dh.size()); return result;
    } catch(...) { failed(env); return nullptr; }
}
extern "C" JNIEXPORT void JNICALL Java_NativeCngHpkeFixture_destroy(JNIEnv* env,jclass,jlong id) {
    try { std::lock_guard<std::mutex> guard(mutex); get(id); fixtures.erase(id); }
    catch(...) { failed(env); }
}
