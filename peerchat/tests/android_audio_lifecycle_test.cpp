#include "../audio.hpp"
#include <jni.h>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>
extern "C" {
jboolean Java_com_peerchat_android_platform_audio_AndroidAudioDevice_install(JNIEnv*,jclass);
void Java_com_peerchat_android_platform_audio_AndroidAudioDevice_capture(JNIEnv*,jclass,jlong,jshortArray);
void Java_com_peerchat_android_platform_audio_AndroidAudioDevice_playout(JNIEnv*,jclass,jlong,jshortArray);
void Java_com_peerchat_android_platform_audio_AndroidAudioDevice_rendered(JNIEnv*,jclass,jlong,jshortArray,jint);
void Java_com_peerchat_android_platform_audio_AndroidAudioDevice_failed(JNIEnv*,jclass,jlong);
}
// VM/device fixture only: shipping Audio implementation remains unchanged.
namespace {
JNINativeInterface api{};JNIInvokeInterface invoke{};JNIEnv env{&api};JavaVM vm{&invoke};
struct Shorts : _jshortArray {std::array<jshort,320> samples{};int length=320;};
bool permitted=true,lastStartedMuted=false;std::atomic<jlong> token{0};std::atomic<unsigned> stops{0};
jclass type=reinterpret_cast<jclass>(1);
jint getVm(JNIEnv*,JavaVM** out){*out=&vm;return JNI_OK;}
jint getEnv(JavaVM*,void** out,jint){*out=&env;return JNI_OK;}
jobject global(JNIEnv*,jobject obj){return obj;}
jboolean exception(JNIEnv*){return JNI_FALSE;}
jmethodID method(JNIEnv*,jclass,const char* name,const char*){return reinterpret_cast<jmethodID>(!std::strcmp(name,"start")?1:!std::strcmp(name,"stop")?2:3);}
jboolean callBoolean(JNIEnv*,jclass,jmethodID id,va_list args){
    auto value=va_arg(args,jlong);
    if(id==reinterpret_cast<jmethodID>(1)){token=value;(void)va_arg(args,jint);(void)va_arg(args,jint);lastStartedMuted=va_arg(args,jint)!=0;return permitted?JNI_TRUE:JNI_FALSE;}
    return JNI_TRUE;
}
void callVoid(JNIEnv*,jclass,jmethodID,va_list){++stops;}
jsize length(JNIEnv*,jarray data){return static_cast<Shorts*>(data)->length;}
void read(JNIEnv*,jshortArray data,jsize start,jsize count,jshort* out){std::copy_n(static_cast<Shorts*>(data)->samples.data()+start,count,out);}
void write(JNIEnv*,jshortArray data,jsize start,jsize count,const jshort* input){std::copy_n(input,count,static_cast<Shorts*>(data)->samples.data()+start);}
void fixture(){api.GetJavaVM=getVm;api.NewGlobalRef=global;api.ExceptionCheck=exception;api.GetStaticMethodID=method;api.CallStaticBooleanMethodV=callBoolean;api.CallStaticVoidMethodV=callVoid;api.GetArrayLength=length;api.GetShortArrayRegion=read;api.SetShortArrayRegion=write;invoke.GetEnv=getEnv;}
void capture(jlong id,Shorts& data){Java_com_peerchat_android_platform_audio_AndroidAudioDevice_capture(&env,type,id,&data);}
}
int main(){
    using namespace std::chrono_literals;
    peerchat::Audio audio;std::wstring error;assert(!audio.start(error));assert(!error.empty());
    fixture();assert(Java_com_peerchat_android_platform_audio_AndroidAudioDevice_install(&env,type));
    permitted=false;assert(!audio.start(error));auto rejected=token.load();permitted=true;
    Shorts pcm;pcm.samples.fill(1000);capture(rejected,pcm);assert(audio.takeCaptured().empty());
    assert(audio.setMuted(true,error));assert(audio.start(error));assert(lastStartedMuted);
    capture(token,pcm);assert(audio.takeCaptured().empty());assert(audio.setMuted(false,error));
    capture(token,pcm);assert(audio.takeCaptured().size()==1);assert(audio.telemetry().inputLevel>0);
    pcm.length=319;capture(token,pcm);assert(audio.takeCaptured().empty());pcm.length=320;
    capture(token,pcm);assert(audio.setMuted(true,error));assert(audio.takeCaptured().empty());
    audio.stop();assert(!audio.telemetry().active);
    auto old=token.load();assert(audio.setMuted(false,error));assert(audio.start(error));assert(old!=token);
    capture(old,pcm);assert(audio.takeCaptured().empty());audio.stop();
    unsigned rendered=0;
    assert(audio.setPlayoutHandler([](std::int16_t* out,std::size_t count){std::fill_n(out,count,30000);return true;}));
    assert(audio.setRenderReference([&](const std::int16_t* data,std::size_t count,unsigned delay){assert(data[0]==32767&&count==320&&delay==2000);++rendered;}));
    audio.setVolumes(1,4);assert(audio.start(error));
    Java_com_peerchat_android_platform_audio_AndroidAudioDevice_playout(&env,type,token,&pcm);assert(pcm.samples[0]==32767);assert(rendered==0);
    Java_com_peerchat_android_platform_audio_AndroidAudioDevice_rendered(&env,type,token,&pcm,3000);assert(rendered==1);audio.stop();
    std::mutex mutex;std::condition_variable ready;bool entered=false,release=false;
    audio.setCaptureHandler([&](const auto&){std::unique_lock<std::mutex> lock(mutex);entered=true;ready.notify_all();ready.wait(lock,[&]{return release;});});
    assert(audio.start(error));auto current=token.load();
    std::thread worker([&]{capture(current,pcm);});
    {std::unique_lock<std::mutex> lock(mutex);assert(ready.wait_for(lock,2s,[&]{return entered;}));}
    auto diagnostics=std::async(std::launch::async,[&]{return audio.diagnostics();});
    assert(diagnostics.wait_for(2s)==std::future_status::ready);assert(!diagnostics.get().empty());
    std::atomic<bool> stopped{false};std::thread closer([&]{audio.stop();stopped=true;});
    std::this_thread::sleep_for(20ms);assert(!stopped);
    {std::lock_guard<std::mutex> lock(mutex);release=true;}ready.notify_all();worker.join();closer.join();assert(stopped);
    capture(current,pcm);assert(audio.takeCaptured().empty());
    assert(audio.start(error));Java_com_peerchat_android_platform_audio_AndroidAudioDevice_failed(&env,type,token);
    assert(!audio.telemetry().active&&!audio.takeError().empty()&&audio.takeError().empty());audio.stop();
    std::cout<<"PASS Android Audio JNI lifecycle: unavailable/denied, pre-muted start, exact frames, stale generations, clipping, successful render reference, callback drain, concurrent diagnostics and device failure\n";
}
