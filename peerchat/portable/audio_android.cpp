#include "../audio.hpp"
#include "audio_android_buffer.hpp"
#include <jni.h>
#include <array>
#include <atomic>
#include <mutex>
#include <sstream>
namespace peerchat {
namespace {
struct State {
    std::mutex mutex;
    std::mutex callbackMutex;
    bool active=false,muted=false;
    int input=-1,output=-1;
    float inputGain=1,outputGain=1;
    MediaTelemetry telemetry;
    std::wstring error;
    std::function<void(const std::vector<unsigned char>&)> capture;
    std::function<bool(std::int16_t*,std::size_t)> playout;
    Audio::RenderReference render;
    android_audio::Buffers buffers;
    std::uint64_t captures=0,pulls=0;
};
std::mutex registryMutex;
std::map<jlong,std::shared_ptr<State>> registry;
std::atomic<jlong> nextToken{1};
JavaVM* vm=nullptr;jclass deviceClass=nullptr;
jmethodID startMethod=nullptr,stopMethod=nullptr,muteMethod=nullptr;
struct Env {
    JNIEnv* env=nullptr;bool attached=false;
    Env(){if(vm&&vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)==JNI_EDETACHED){attached=vm->AttachCurrentThread(&env,nullptr)==JNI_OK;}}
    ~Env(){if(attached)vm->DetachCurrentThread();}
};
bool exception(JNIEnv* env){if(!env->ExceptionCheck())return false;env->ExceptionClear();return true;}
std::shared_ptr<State> find(jlong token){std::lock_guard<std::mutex> lock(registryMutex);auto found=registry.find(token);return found==registry.end()?nullptr:found->second;}
float level(const std::int16_t* pcm){int peak=0;for(unsigned i=0;i<320;++i)peak=std::max(peak,std::abs(int(pcm[i])));return float(peak)/32768.0f;}
}
struct Audio::Impl {std::shared_ptr<State> state=std::make_shared<State>();jlong token=0;};
Audio::Audio():impl_(std::make_unique<Impl>()){}
Audio::~Audio(){stop();}
bool Audio::start(std::wstring& error){
    stop();Env access;
    if(!access.env||!deviceClass){error=L"Android audio is not prepared by an explicit foreground call.";return false;}
    auto state=impl_->state;
    {std::lock_guard<std::mutex> lock(state->mutex);state->active=true;state->buffers.clear();state->error.clear();}
    impl_->token=nextToken++;
    {std::lock_guard<std::mutex> lock(registryMutex);registry.emplace(impl_->token,state);}
    bool started=access.env->CallStaticBooleanMethod(deviceClass,startMethod,impl_->token,state->input,state->output,state->muted)==JNI_TRUE;
    if(exception(access.env)||!started){stop();error=L"Android audio permission, focus, or device initialization failed.";return false;}
    {std::lock_guard<std::mutex> lock(state->mutex);state->telemetry={0,0,!state->muted,true,true};}
    if(state->muted&&!setMuted(true,error)){stop();return false;}
    return true;
}
void Audio::stop(){
    auto state=impl_->state;
    {std::lock_guard<std::mutex> lock(state->mutex);state->active=false;state->buffers.clear();state->telemetry={};}
    {std::lock_guard<std::mutex> drain(state->callbackMutex);}
    auto token=impl_->token;impl_->token=0;
    if(token){
        {std::lock_guard<std::mutex> lock(registryMutex);registry.erase(token);}
        Env access;if(access.env&&deviceClass){access.env->CallStaticVoidMethod(deviceClass,stopMethod,token);exception(access.env);}
    }
}
bool Audio::setMuted(bool muted,std::wstring& error){
    auto state=impl_->state;
    {std::lock_guard<std::mutex> lock(state->mutex);state->muted=muted;state->buffers.captured.clear();state->telemetry.inputLevel=0;state->telemetry.inputAvailable=state->active&&!muted;}
    {std::lock_guard<std::mutex> drain(state->callbackMutex);}
    if(!impl_->token)return true;
    Env access;if(!access.env)return false;
    bool ok=access.env->CallStaticBooleanMethod(deviceClass,muteMethod,impl_->token,muted)==JNI_TRUE;
    if(exception(access.env)||!ok){error=L"Android microphone state change failed.";return false;}return true;
}
void Audio::setDevices(int input,int output){std::lock_guard<std::mutex> lock(impl_->state->mutex);if(!impl_->state->active){impl_->state->input=input;impl_->state->output=output;}}
std::vector<Audio::Device> Audio::inputDevices(){return {{-1,L"Android communication input"}};}
std::vector<Audio::Device> Audio::outputDevices(){return {{-1,L"Android communication output"}};}
void Audio::setVolumes(float input,float output){std::lock_guard<std::mutex> lock(impl_->state->mutex);impl_->state->inputGain=android_audio::gain(input);impl_->state->outputGain=android_audio::gain(output);}
void Audio::play(int sender,const std::vector<unsigned char>& pcm){std::lock_guard<std::mutex> lock(impl_->state->mutex);if(impl_->state->active)impl_->state->buffers.play(sender,pcm);}
std::vector<std::vector<unsigned char>> Audio::takeCaptured(){std::lock_guard<std::mutex> lock(impl_->state->mutex);auto& queue=impl_->state->buffers.captured;std::vector<std::vector<unsigned char>> result(queue.begin(),queue.end());queue.clear();return result;}
void Audio::setCaptureHandler(std::function<void(const std::vector<unsigned char>&)> handler){std::lock_guard<std::mutex> lock(impl_->state->mutex);if(!impl_->state->active)impl_->state->capture=std::move(handler);}
bool Audio::setPlayoutHandler(std::function<bool(std::int16_t*,std::size_t)> handler){std::lock_guard<std::mutex> lock(impl_->state->mutex);if(impl_->state->active)return false;impl_->state->playout=std::move(handler);return true;}
bool Audio::setRenderReference(RenderReference handler){std::lock_guard<std::mutex> lock(impl_->state->mutex);if(impl_->state->active)return false;impl_->state->render=std::move(handler);return true;}
std::wstring Audio::takeError(){std::lock_guard<std::mutex> lock(impl_->state->mutex);auto result=impl_->state->error;impl_->state->error.clear();return result;}
MediaTelemetry Audio::telemetry(){std::lock_guard<std::mutex> lock(impl_->state->mutex);return impl_->state->telemetry;}
std::wstring Audio::diagnostics(){std::lock_guard<std::mutex> lock(impl_->state->mutex);std::wostringstream out;out<<L"audio_platform=Android AudioRecord/AudioTrack\naudio_processing=Android session effects; WebRTC APM unavailable\naudio_capture_frames="<<impl_->state->captures<<L"\naudio_playout_frames="<<impl_->state->pulls<<L"\naudio_queue_drops="<<impl_->state->buffers.dropped;return out.str();}
// These callbacks carry PCM only and cannot authorize or start a call.
extern "C" JNIEXPORT jboolean JNICALL Java_com_peerchat_android_platform_audio_AndroidAudioDevice_install(JNIEnv* env,jclass type){
    std::lock_guard<std::mutex> lock(registryMutex);
    if(deviceClass)return JNI_TRUE;
    env->GetJavaVM(&vm);deviceClass=static_cast<jclass>(env->NewGlobalRef(type));
    startMethod=env->GetStaticMethodID(type,"start","(JIIZ)Z");stopMethod=env->GetStaticMethodID(type,"stop","(J)V");muteMethod=env->GetStaticMethodID(type,"setMuted","(JZ)Z");
    if(exception(env)||!deviceClass||!startMethod||!stopMethod||!muteMethod){if(deviceClass)env->DeleteGlobalRef(deviceClass);deviceClass=nullptr;return JNI_FALSE;}return JNI_TRUE;
}
extern "C" JNIEXPORT void JNICALL Java_com_peerchat_android_platform_audio_AndroidAudioDevice_capture(JNIEnv* env,jclass,jlong token,jshortArray samples){
    auto state=find(token);if(!state||!samples||env->GetArrayLength(samples)!=320)return;
    std::array<std::int16_t,320> pcm{};env->GetShortArrayRegion(samples,0,320,pcm.data());if(exception(env))return;
    std::lock_guard<std::mutex> callbackLock(state->callbackMutex);
    std::unique_lock<std::mutex> lock(state->mutex);if(!state->active||state->muted)return;
    std::vector<unsigned char> bytes(640);
    for(unsigned i=0;i<320;++i){pcm[i]=android_audio::scale(pcm[i],state->inputGain);bytes[2*i]=static_cast<unsigned char>(pcm[i]);bytes[2*i+1]=static_cast<unsigned char>(static_cast<unsigned short>(pcm[i])>>8);}
    state->telemetry.inputLevel=level(pcm.data());++state->captures;
    auto handler=state->capture;
    if(handler){lock.unlock();handler(bytes);}else state->buffers.capture(std::move(bytes));
}
extern "C" JNIEXPORT void JNICALL Java_com_peerchat_android_platform_audio_AndroidAudioDevice_playout(JNIEnv* env,jclass,jlong token,jshortArray samples){
    if(!samples||env->GetArrayLength(samples)!=320)return;
    std::array<std::int16_t,320> pcm{};auto state=find(token);
    if(state){std::lock_guard<std::mutex> callbackLock(state->callbackMutex);std::unique_lock<std::mutex> lock(state->mutex);if(state->active){
        auto handler=state->playout;const auto gain=state->outputGain;
        if(handler){lock.unlock();handler(pcm.data(),320);for(auto& sample:pcm)sample=android_audio::scale(sample,gain);lock.lock();}else state->buffers.pull(pcm.data(),gain);
        state->telemetry.outputLevel=level(pcm.data());++state->pulls;
    }}
    env->SetShortArrayRegion(samples,0,320,pcm.data());exception(env);
}
extern "C" JNIEXPORT void JNICALL Java_com_peerchat_android_platform_audio_AndroidAudioDevice_rendered(JNIEnv* env,jclass,jlong token,jshortArray samples,jint delay){
    auto state=find(token);if(!state||!samples||env->GetArrayLength(samples)!=320)return;
    std::array<std::int16_t,320> pcm{};env->GetShortArrayRegion(samples,0,320,pcm.data());if(exception(env))return;
    std::lock_guard<std::mutex> callbackLock(state->callbackMutex);
    std::unique_lock<std::mutex> lock(state->mutex);auto handler=state->active?state->render:Audio::RenderReference{};
    lock.unlock();if(handler)handler(pcm.data(),320,static_cast<unsigned>(std::clamp(delay,0,2000)));
}
extern "C" JNIEXPORT void JNICALL Java_com_peerchat_android_platform_audio_AndroidAudioDevice_failed(JNIEnv*,jclass,jlong token){auto state=find(token);if(state){std::lock_guard<std::mutex> lock(state->mutex);state->active=false;state->telemetry={};state->error=L"Android audio device disconnected or permission revoked.";}}
}
