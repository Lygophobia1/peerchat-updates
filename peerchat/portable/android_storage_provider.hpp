#pragma once
#include "../platform/storage_backend.hpp"
#include <jni.h>
#include <map>

namespace peerchat::platform::android {
// Compiled inside peerchat_messaging, never passed as C++ across the DLL/JNI ABI.
struct Context {
    JavaVM* vm{};jobject object{};std::map<std::string,jmethodID> methods;
    struct Call {
        JNIEnv* env{};JavaVM* vm{};bool attached=false,frame=false;
        explicit Call(JavaVM* machine):vm(machine){
            if(vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK){
#if defined(_WIN32)
                auto result=vm->AttachCurrentThread(reinterpret_cast<void**>(&env),nullptr);
#else
                auto result=vm->AttachCurrentThread(&env,nullptr);
#endif
                if(result!=JNI_OK)throw std::runtime_error("Provider thread attachment failed");
                attached=true;
            }
            if(env->PushLocalFrame(32)!=JNI_OK){if(env->ExceptionCheck())env->ExceptionClear();if(attached)vm->DetachCurrentThread();throw std::runtime_error("Provider local frame failed");}frame=true;
        }
        ~Call(){if(frame)env->PopLocalFrame(nullptr);if(attached)vm->DetachCurrentThread();}
    };
    static void checked(JNIEnv* env){if(env->ExceptionCheck()){env->ExceptionClear();throw std::runtime_error("Android storage/identity provider failed");}}
    Context(JNIEnv* env,jobject facade){
        if(env->GetJavaVM(&vm)!=JNI_OK)throw std::runtime_error("Provider VM unavailable");
        object=env->NewGlobalRef(facade);checked(env);if(!object)throw std::runtime_error("Provider reference unavailable");
    }
    ~Context(){try{Call call(vm);auto close=call.env->GetMethodID(call.env->GetObjectClass(object),"close","()V");if(close)call.env->CallVoidMethod(object,close);if(call.env->ExceptionCheck())call.env->ExceptionClear();call.env->DeleteGlobalRef(object);}catch(...){}}
    jmethodID method(JNIEnv* env,const char* name,const char* signature){
        std::string key=std::string(name)+signature;auto found=methods.find(key);if(found!=methods.end())return found->second;
        auto type=env->GetObjectClass(object);checked(env);auto result=env->GetMethodID(type,name,signature);checked(env);if(!result)throw std::runtime_error("Provider method missing");methods.emplace(std::move(key),result);return result;
    }
    static jbyteArray input(JNIEnv* env,const Blob& bytes){if(bytes.size()>1048576)throw std::runtime_error("Provider input limit");auto result=env->NewByteArray(static_cast<jsize>(bytes.size()));checked(env);if(!result)throw std::bad_alloc();if(!bytes.empty())env->SetByteArrayRegion(result,0,static_cast<jsize>(bytes.size()),reinterpret_cast<const jbyte*>(bytes.data()));checked(env);return result;}
    static jbyteArray input(JNIEnv* env,const std::string& bytes){return input(env,Blob(bytes.begin(),bytes.end()));}
    static Blob output(JNIEnv* env,jbyteArray bytes){checked(env);if(!bytes)throw std::runtime_error("Provider result missing");auto size=env->GetArrayLength(bytes);if(size<0||size>1048576)throw std::runtime_error("Provider output limit");Blob out(static_cast<size_t>(size));if(size)env->GetByteArrayRegion(bytes,0,size,reinterpret_cast<jbyte*>(out.data()));checked(env);return out;}
    Blob bytes(const char* name,const char* signature){Call call(vm);return output(call.env,static_cast<jbyteArray>(call.env->CallObjectMethod(object,method(call.env,name,signature))));}
    Blob transform(const char* name,const Blob& bytes,const std::string* context=nullptr){Call call(vm);auto data=input(call.env,bytes);auto id=method(call.env,name,context?"([B[B)[B":"([B)[B");auto result=static_cast<jbyteArray>(context?call.env->CallObjectMethod(object,id,data,input(call.env,*context)):call.env->CallObjectMethod(object,id,data));return output(call.env,result);}
};
struct Identity final : IdentityProvider {
    std::shared_ptr<Context> context;explicit Identity(std::shared_ptr<Context> value):context(std::move(value)){}
    Blob publicKey() override{return context->bytes("publicKey","()[B");}
    Blob sign(const Blob& value) override{return context->transform("sign",value);}
};
struct Records final : RecordProtector {
    std::shared_ptr<Context> context;explicit Records(std::shared_ptr<Context> value):context(std::move(value)){}
    Blob protect(const Blob& value,const std::string& label) override{return context->transform("protect",value,&label);}
    Blob unprotect(const Blob& value,const std::string& label) override{return context->transform("unprotect",value,&label);}
};
struct Statement final : SqlStatement {
    std::shared_ptr<Context> context;jlong id;
    Statement(std::shared_ptr<Context> value,jlong handle):context(std::move(value)),id(handle){}
    ~Statement(){try{Context::Call c(context->vm);c.env->CallVoidMethod(context->object,context->method(c.env,"finish","(J)V"),id);Context::checked(c.env);}catch(...){}}
    void bind(const Blob& value,int index,bool blob){Context::Call c(context->vm);auto input=Context::input(c.env,value);c.env->CallVoidMethod(context->object,context->method(c.env,"bindBytes","(JI[BZ)V"),id,static_cast<jint>(index),input,static_cast<jboolean>(blob));Context::checked(c.env);}
    void bindText(int index,const std::string& value) override{bind(Blob(value.begin(),value.end()),index,false);}
    void bindBlob(int index,const Blob& value) override{bind(value,index,true);}
    void bindNumber(int index,std::int64_t value) override{Context::Call c(context->vm);c.env->CallVoidMethod(context->object,context->method(c.env,"bindNumber","(JIJ)V"),id,static_cast<jint>(index),static_cast<jlong>(value));Context::checked(c.env);}
    bool row() override{Context::Call c(context->vm);auto result=c.env->CallBooleanMethod(context->object,context->method(c.env,"row","(J)Z"),id);Context::checked(c.env);return result==JNI_TRUE;}
    Blob column(const char* name,int index){Context::Call c(context->vm);return Context::output(c.env,static_cast<jbyteArray>(c.env->CallObjectMethod(context->object,context->method(c.env,name,"(JI)[B"),id,static_cast<jint>(index))));}
    Blob bytes(int index) override{return column("bytes",index);}
    std::string text(int index) override{auto result=column("text",index);return {result.begin(),result.end()};}
    std::int64_t number(int index) override{Context::Call c(context->vm);auto result=c.env->CallLongMethod(context->object,context->method(c.env,"number","(JI)J"),id,static_cast<jint>(index));Context::checked(c.env);return result;}
};
struct Database final : SqlDatabase {
    std::shared_ptr<Context> context;explicit Database(std::shared_ptr<Context> value):context(std::move(value)){}
    void execute(const std::string& sql) override{Context::Call c(context->vm);auto input=Context::input(c.env,sql);c.env->CallVoidMethod(context->object,context->method(c.env,"execute","([B)V"),input);Context::checked(c.env);}
    std::unique_ptr<SqlStatement> prepare(const std::string& sql) override{Context::Call c(context->vm);auto input=Context::input(c.env,sql);auto id=c.env->CallLongMethod(context->object,context->method(c.env,"prepare","([B)J"),input);Context::checked(c.env);if(id<=0)throw std::runtime_error("Invalid provider statement");try{return std::make_unique<Statement>(context,id);}catch(...){c.env->CallVoidMethod(context->object,context->method(c.env,"finish","(J)V"),id);if(c.env->ExceptionCheck())c.env->ExceptionClear();throw;}}
};
inline std::shared_ptr<StoragePlatform> platform(JNIEnv* env,jobject facade){
    auto context=std::make_shared<Context>(env,facade);auto result=std::make_shared<StoragePlatform>();
    result->identity=std::make_shared<Identity>(context);result->records=std::make_shared<Records>(context);result->database=std::make_shared<Database>(context);return result;
}
}
