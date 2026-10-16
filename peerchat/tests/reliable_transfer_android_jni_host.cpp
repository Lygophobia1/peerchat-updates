#define PEERCHAT_JAVA_TLS_FIXTURE
#include "reliable_transfer_interop_fixture.hpp"
static std::unique_ptr<transfer_interop::Side> side;
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_platform_tls_TransferInteropProof_step(JNIEnv* env,jclass,jobject facade,jbyteArray path,jbyteArray peer,jbyteArray encrypted,jboolean verify){
    using namespace peerchat;using platform::android::Context;
    try{
        if(verify){protocol::check(bool(side));side->verify();side.reset();return Context::input(env,Blob{});}
        if(!side){auto directory=Context::output(env,path),remote=Context::output(env,peer);side=std::make_unique<transfer_interop::Side>(unicode::decode(std::string(directory.begin(),directory.end())),std::string(remote.begin(),remote.end()),std::make_shared<Context>(env,facade));}
        auto bytes=side->exchange(Context::output(env,encrypted));bytes.insert(bytes.begin(),side->complete()?1:0);return Context::input(env,bytes);
    }catch(const std::exception& e){
        std::cerr<<"FAIL Android stage="<<(side?side->stage:"fixture_init")<<" endpoint="<<(side?side->endpoint->diagnosticStage():"none")<<" reason="<<e.what()<<'\n';
        if(env->ExceptionCheck())env->ExceptionClear();
        return nullptr;
    }
}
