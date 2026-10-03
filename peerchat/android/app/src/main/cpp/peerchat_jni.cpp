#include <jni.h>
#include "peerchat_core.h"
#include <vector>
#include "identity_format.hpp"

// Copying/binding only: no protocol, identity, retries or authorization here.
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_NativeBridge_normalizeIdentity(JNIEnv* env,jobject,jbyteArray publicKey,jbyteArray signature) {
    try {
        if(!publicKey||!signature||env->GetArrayLength(publicKey)!=72||env->GetArrayLength(signature)!=64)return nullptr;
        uint8_t publicBytes[72]{},signatureBytes[64]{};
        env->GetByteArrayRegion(publicKey,0,72,reinterpret_cast<jbyte*>(publicBytes));
        env->GetByteArrayRegion(signature,0,64,reinterpret_cast<jbyte*>(signatureBytes));
        if(env->ExceptionCheck()||!peerchat::identity_format::validPublicRepresentation(publicBytes,72))return nullptr;
        auto normalizedPublic=peerchat::identity_format::publicKey(publicBytes+8,publicBytes+40);
        auto normalizedSignature=peerchat::identity_format::signature(signatureBytes,signatureBytes+32);
        auto output=env->NewByteArray(136);if(!output)return nullptr;
        env->SetByteArrayRegion(output,0,72,reinterpret_cast<const jbyte*>(normalizedPublic.data()));
        env->SetByteArrayRegion(output,72,64,reinterpret_cast<const jbyte*>(normalizedSignature.data()));return output;
    }catch(...){return nullptr;}
}
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
