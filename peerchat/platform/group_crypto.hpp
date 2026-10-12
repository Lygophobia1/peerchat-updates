#pragma once
#include "../storage.hpp"
#if defined(_WIN32) && !defined(PEERCHAT_PORTABLE_CRYPTO)
#include <bcrypt.h>
#else
#include "portable_crypto.hpp"
#include <mbedtls/gcm.h>
#endif

namespace peerchat::platform::group_crypto {
// Platform primitives only. PCGE framing, AAD, signature, membership, nonce
// reservation and durable import policy remain in the shared group owners.
inline void check(bool ok){if(!ok)throw std::runtime_error("Group cryptographic operation failed");}
inline void wipe(void* bytes,size_t length){
#if defined(_WIN32) && !defined(PEERCHAT_PORTABLE_CRYPTO)
    SecureZeroMemory(bytes,length);
#else
    portable_crypto::wipe(bytes,length);
#endif
}
inline Blob random(size_t length){
    check(length>0&&length<=32);
#if defined(_WIN32) && !defined(PEERCHAT_PORTABLE_CRYPTO)
    Blob bytes(length);check(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0);return bytes;
#else
    return portable_crypto::random(length);
#endif
}
inline bool gcm(bool encrypt,const Blob& keyBytes,const Blob& aad,const Blob& nonce,const Blob& input,Blob& output,Blob& tag){
    check(keyBytes.size()==32&&nonce.size()==12&&tag.size()==16&&input.size()<=65536&&aad.size()<=65536);
    output.resize(input.size());unsigned char empty=0;
#if defined(_WIN32) && !defined(PEERCHAT_PORTABLE_CRYPTO)
    struct Algorithm {BCRYPT_ALG_HANDLE handle{};~Algorithm(){if(handle)BCryptCloseAlgorithmProvider(handle,0);}} algorithm;
    struct Key {BCRYPT_KEY_HANDLE handle{};~Key(){if(handle)BCryptDestroyKey(handle);}} key;
    check(BCryptOpenAlgorithmProvider(&algorithm.handle,BCRYPT_AES_ALGORITHM,nullptr,0)>=0);
    check(BCryptSetProperty(algorithm.handle,BCRYPT_CHAINING_MODE,reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),sizeof(BCRYPT_CHAIN_MODE_GCM),0)>=0);
    check(BCryptGenerateSymmetricKey(algorithm.handle,&key.handle,nullptr,0,const_cast<PUCHAR>(keyBytes.data()),32,0)>=0);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce=const_cast<PUCHAR>(nonce.data());info.cbNonce=12;
    info.pbAuthData=const_cast<PUCHAR>(aad.data());info.cbAuthData=static_cast<ULONG>(aad.size());info.pbTag=tag.data();info.cbTag=16;
    ULONG written=0;auto operation=encrypt?BCryptEncrypt:BCryptDecrypt;
    auto status=operation(key.handle,input.empty()?&empty:const_cast<PUCHAR>(input.data()),static_cast<ULONG>(input.size()),&info,nullptr,0,output.empty()?&empty:output.data(),static_cast<ULONG>(output.size()),&written,0);
    if(status<0){wipe(output.data(),output.size());output.clear();return false;}check(written==input.size());return true;
#else
    struct Gcm {mbedtls_gcm_context value;Gcm(){mbedtls_gcm_init(&value);}~Gcm(){mbedtls_gcm_free(&value);}} context;
    check(mbedtls_gcm_setkey(&context.value,MBEDTLS_CIPHER_ID_AES,keyBytes.data(),256)==0);
    const auto* source=input.empty()?&empty:input.data();auto* destination=output.empty()?&empty:output.data();
    int result=encrypt?mbedtls_gcm_crypt_and_tag(&context.value,MBEDTLS_GCM_ENCRYPT,input.size(),nonce.data(),nonce.size(),aad.data(),aad.size(),source,destination,tag.size(),tag.data())
        :mbedtls_gcm_auth_decrypt(&context.value,input.size(),nonce.data(),nonce.size(),aad.data(),aad.size(),tag.data(),tag.size(),source,destination);
    if(result!=0){wipe(output.data(),output.size());output.clear();return false;}return true;
#endif
}
}
