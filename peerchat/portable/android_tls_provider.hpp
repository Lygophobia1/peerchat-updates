#pragma once
#include "android_storage_provider.hpp"
#include "../platform/peer_tls_provider.hpp"

namespace peerchat::platform::android {
// All calls are serialized by the existing native instance gate. Context obtains
// a thread-local JNIEnv and bounds/copies buffers; no Java object crosses the ABI.
class TlsProvider final : public PeerTlsProvider {
    std::shared_ptr<Context> context_;
    void write(const char* name,const Bytes& bytes){
        if(bytes.size()>262144)throw std::runtime_error("TLS byte limit");
        Context::Call call(context_->vm);auto input=Context::input(call.env,bytes);
        call.env->CallVoidMethod(context_->object,context_->method(call.env,name,"([B)V"),input);
        Context::checked(call.env);
    }
    bool flag(const char* name) const{
        Context::Call call(context_->vm);
        auto result=call.env->CallBooleanMethod(context_->object,context_->method(call.env,name,"()Z"));
        Context::checked(call.env);return result==JNI_TRUE;
    }
    Bytes read(const char* name) const{
        auto bytes=context_->bytes(name,"()[B");
        if(bytes.size()>262144)throw std::runtime_error("TLS result limit");
        return bytes;
    }
public:
    explicit TlsProvider(std::shared_ptr<Context> context):context_(std::move(context)){}
    void receive(const Bytes& bytes) override{write("receive",bytes);}
    Bytes takeOutput() override{return read("takeOutput");}
    Bytes takePlaintext() override{return read("takePlaintext");}
    bool ready() const override{return flag("ready");}
    Bytes remotePublicKey() const override{return read("remotePublicKey");}
    void send(const Bytes& bytes) override{write("send",bytes);}
    void shutdown() override{Context::Call call(context_->vm);call.env->CallVoidMethod(context_->object,context_->method(call.env,"close","()V"));Context::checked(call.env);}
    bool closed() const override{return flag("closed");}
};
class TlsFactory final : public PeerTlsFactory {
    using Bytes=PeerTlsProvider::Bytes;
    std::shared_ptr<Context> context_;
public:
    explicit TlsFactory(std::shared_ptr<Context> context):context_(std::move(context)){}
    Bytes publicKey() const override{return context_->bytes("publicKey","()[B");}
    std::unique_ptr<PeerTlsProvider> create(bool incoming,const std::string& expected) override{
        Context::Call call(context_->vm);auto input=Context::input(call.env,expected);
        auto object=call.env->CallObjectMethod(context_->object,context_->method(call.env,"createPeerTls","(Z[B)Lcom/peerchat/android/platform/tls/PeerTls;"),static_cast<jboolean>(incoming),input);
        Context::checked(call.env);if(!object)throw std::runtime_error("Missing TLS provider");
        auto child=std::make_shared<Context>(call.env,object);
        return std::make_unique<TlsProvider>(std::move(child));
    }
};
}
