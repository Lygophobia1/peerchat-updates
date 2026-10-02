#include <jni.h>
#include "peerchat_core.h"
#include <vector>

// Copying/binding only: no protocol, identity, retries or authorization here.
extern "C" JNIEXPORT jlong JNICALL Java_com_peerchat_android_NativeBridge_create(JNIEnv*,jobject) {
    pc_core_handle handle=0;return pc_core_create(&handle)==PC_CORE_OK?static_cast<jlong>(handle):0;
}
extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_NativeBridge_destroy(JNIEnv*,jobject,jlong handle) {
    return pc_core_destroy(static_cast<pc_core_handle>(handle));
}
extern "C" JNIEXPORT jintArray JNICALL Java_com_peerchat_android_NativeBridge_info(JNIEnv* env,jobject,jlong handle) {
    pc_core_info info{};info.struct_size=sizeof(info);
    if(pc_core_get_info(static_cast<pc_core_handle>(handle),&info)!=PC_CORE_OK)return nullptr;
    jint values[]={static_cast<jint>(info.abi_version),static_cast<jint>(info.session_policy_available),static_cast<jint>(info.identity_available),static_cast<jint>(info.storage_available),static_cast<jint>(info.peer_tls_available)};
    auto out=env->NewIntArray(5);if(out)env->SetIntArrayRegion(out,0,5,values);return out;
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_NativeBridge_exchange(JNIEnv* env,jobject,jlong handle,jbyteArray data) {
    try {
        if(!data)return nullptr;
        auto size=env->GetArrayLength(data);
        if(size>PC_CORE_MAX_EXCHANGE_BYTES)return nullptr;
        std::vector<uint8_t> input(static_cast<size_t>(size));
        if(size)env->GetByteArrayRegion(data,0,size,reinterpret_cast<jbyte*>(input.data()));
        if(env->ExceptionCheck())return nullptr;
        uint32_t required=0;auto id=static_cast<pc_core_handle>(handle);
        auto status=pc_core_exchange_utf8(id,input.data(),static_cast<uint32_t>(size),nullptr,0,&required);
        if(status!=PC_CORE_OK&&status!=PC_CORE_BUFFER_TOO_SMALL)return nullptr;
        std::vector<uint8_t> output(required);
        if(pc_core_exchange_utf8(id,input.data(),static_cast<uint32_t>(size),output.data(),required,&required)!=PC_CORE_OK)return nullptr;
        auto result=env->NewByteArray(static_cast<jsize>(required));
        if(result&&required)env->SetByteArrayRegion(result,0,static_cast<jsize>(required),reinterpret_cast<const jbyte*>(output.data()));
        return result;
    }catch(...){return nullptr;}
}
