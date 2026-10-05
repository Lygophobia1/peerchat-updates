#pragma once
#include "android_storage_provider.hpp"
#include "../platform/hpke_provider.hpp"
#include <atomic>
#include <mutex>

namespace peerchat::platform::android {
// Platform adapters only. No message ID, account, session or delivery policy.
// Used under the existing native profile/Instance gate. All private operations
// stay in Android Keystore; BC remains responsible for RFC 9180.
struct RecipientHandle final : RecipientKeyHandle {
    std::shared_ptr<Context> context;std::string alias;
    RecipientHandle(std::shared_ptr<Context> value,std::string name):context(std::move(value)),alias(std::move(name)){}
    Blob publicKey()override{return context->transform("recipientPublic",Blob(alias.begin(),alias.end()));}
    Blob agreeP256(const Blob& point)override{
        Context::Call call(context->vm);auto id=context->method(call.env,"recipientAgree","([B[B)[B");
        auto result=static_cast<jbyteArray>(call.env->CallObjectMethod(context->object,id,Context::input(call.env,alias),Context::input(call.env,point)));
        auto secret=Context::output(call.env,result);
        Blob zero(secret.size());if(!zero.empty())call.env->SetByteArrayRegion(result,0,static_cast<jsize>(zero.size()),reinterpret_cast<const jbyte*>(zero.data()));
        try{Context::checked(call.env);}catch(...){volatile unsigned char* p=secret.data();for(std::size_t i=0;i<secret.size();++i)p[i]=0;throw;}
        return secret;
    }
};
struct RecipientProvider final : RecipientKeyProvider {
    std::shared_ptr<Context> context;explicit RecipientProvider(std::shared_ptr<Context> value):context(std::move(value)){}
    bool supported()const override{Context::Call call(context->vm);auto value=call.env->CallBooleanMethod(context->object,context->method(call.env,"recipientSupported","()Z"));Context::checked(call.env);return value==JNI_TRUE;}
    bool exists(const std::string& alias)override{
        Context::Call call(context->vm);auto value=call.env->CallBooleanMethod(context->object,context->method(call.env,"recipientExists","([B)Z"),Context::input(call.env,alias));Context::checked(call.env);return value==JNI_TRUE;
    }
    std::shared_ptr<RecipientKeyHandle> open(const std::string& alias)override{
        auto key=std::make_shared<RecipientHandle>(context,alias);auto point=key->publicKey();
        if(point.size()!=65||point[0]!=4)throw std::runtime_error("Invalid opaque Android recipient key");
        return key;
    }
    std::shared_ptr<RecipientKeyHandle> create(const std::string& alias)override{
        auto created=context->transform("recipientCreate",Blob(alias.begin(),alias.end()));auto key=open(alias);
        if(created!=key->publicKey())throw std::runtime_error("Android recipient creation mismatch");
        return key;
    }
};
class AndroidHpkeProvider final : public HpkeProvider {
    std::shared_ptr<Context> context_;std::mutex gate_;std::atomic<bool> closing_{false};
    void check(const std::atomic<bool>* cancel)const{if(closing_.load()||(cancel&&cancel->load()))throw std::runtime_error("HPKE operation cancelled or closed");}
public:
    explicit AndroidHpkeProvider(std::shared_ptr<Context> context):context_(std::move(context)){}
    ~AndroidHpkeProvider()override{shutdown();}
    HpkeEnvelope seal(const Bytes& pub,const Bytes& info,const Bytes& aad,const Bytes& plain,const std::atomic<bool>* cancel=nullptr)override {
        check(cancel);std::lock_guard<std::mutex> lock(gate_);check(cancel);Context::Call call(context_->vm);
        auto result=static_cast<jbyteArray>(call.env->CallObjectMethod(context_->object,context_->method(call.env,"hpkeSeal","([B[B[B[B)[B"),
            Context::input(call.env,pub),Context::input(call.env,info),Context::input(call.env,aad),Context::input(call.env,plain)));
        auto bytes=Context::output(call.env,result);check(cancel);
        if(bytes.size()<81||bytes.size()>1048576||bytes[0]!=4)throw std::runtime_error("Invalid HPKE seal result");
        return {Blob(bytes.begin(),bytes.begin()+65),Blob(bytes.begin()+65,bytes.end())};
    }
    Bytes open(const std::shared_ptr<RecipientKeyHandle>& key,const Bytes& expected,const Bytes& info,const Bytes& aad,const HpkeEnvelope& envelope,const std::atomic<bool>* cancel=nullptr)override {
        check(cancel);std::lock_guard<std::mutex> lock(gate_);check(cancel);
        auto recipient=std::dynamic_pointer_cast<RecipientHandle>(key);
        if(!recipient||recipient->context!=context_||recipient->publicKey()!=expected)throw std::runtime_error("Android recipient capability mismatch");
        Context::Call call(context_->vm);
        auto result=static_cast<jbyteArray>(call.env->CallObjectMethod(context_->object,context_->method(call.env,"hpkeOpen","([B[B[B[B[B[B)[B"),
            Context::input(call.env,recipient->alias),Context::input(call.env,expected),Context::input(call.env,envelope.enc),Context::input(call.env,info),Context::input(call.env,aad),Context::input(call.env,envelope.ciphertext)));
        auto plain=Context::output(call.env,result);
        // Copy ownership to native and clear the Java result immediately.
        Blob zero(plain.size());if(!zero.empty())call.env->SetByteArrayRegion(result,0,static_cast<jsize>(zero.size()),reinterpret_cast<const jbyte*>(zero.data()));
        try{Context::checked(call.env);check(cancel);}catch(...){volatile unsigned char* p=plain.data();for(std::size_t i=0;i<plain.size();++i)p[i]=0;throw;}
        return plain;
    }
    void shutdown()noexcept override{closing_.store(true);std::lock_guard<std::mutex> lock(gate_);}
};
}
