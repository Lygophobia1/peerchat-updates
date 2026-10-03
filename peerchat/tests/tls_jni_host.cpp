#include "portable/android_tls_provider.hpp"
#include "tls.hpp"

// Host-only proof library. Never included by Android/Windows product builds.
extern "C" JNIEXPORT jboolean JNICALL Java_com_peerchat_android_platform_tls_NativeTlsProof_run(
    JNIEnv* env,jclass,jobject left,jobject right,jbyteArray leftPin,jbyteArray rightPin,jbyteArray input){
    using namespace peerchat;using namespace peerchat::platform::android;
    try{
        auto a=std::make_shared<Context>(env,left),b=std::make_shared<Context>(env,right);
        auto af=std::make_shared<TlsFactory>(a),bf=std::make_shared<TlsFactory>(b);
        Profile ap,bp;ap.identity=std::make_shared<Identity>(a);bp.identity=std::make_shared<Identity>(b);
        ap.publicKey=af->publicKey();bp.publicKey=bf->publicKey();
        auto aid=Context::output(env,leftPin),bid=Context::output(env,rightPin);
        TlsCredentials ac(ap,af),bc(bp,bf);
        TlsSession client(ac,false,std::string(bid.begin(),bid.end())),server(bc,true,std::string(aid.begin(),aid.end()));
        for(int i=0;i<20 && (!client.ready() || !server.ready());++i){server.receive(client.takeOutput());client.receive(server.takeOutput());}
        if(!client.ready() || !server.ready() || client.remotePublicKey()!=bp.publicKey || server.remotePublicKey()!=ap.publicKey)return JNI_FALSE;
        auto payload=Context::output(env,input);client.send(payload);server.receive(client.takeOutput());
        if(server.takePlaintext()!=payload)return JNI_FALSE;
        server.send(payload);client.receive(server.takeOutput());if(client.takePlaintext()!=payload)return JNI_FALSE;
        client.shutdown();server.receive(client.takeOutput());return server.closed()?JNI_TRUE:JNI_FALSE;
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return JNI_FALSE;}
}
