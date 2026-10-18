// TEST ONLY: deterministic PCM device port. The production VoiceMedia, codec,
// ICE/DTLS-SRTP and NetEq implementations are linked unchanged. No OS devices.
#include "android_voice_media_integration_audio.hpp"
#include "audio.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace voice_integration_audio {
namespace {
thread_local std::string owner;
struct State {
    std::mutex gate;
    Snapshot value;
    float inputGain=1,outputGain=1;
    std::function<void(const std::vector<unsigned char>&)> capture;
    std::function<bool(std::int16_t*,std::size_t)> playout;
    peerchat::Audio::RenderReference rendered;
};
std::mutex registryGate;
std::vector<std::shared_ptr<State>> states;
std::shared_ptr<State> create(){
    auto state=std::make_shared<State>();std::lock_guard<std::mutex> lock(registryGate);
    if(states.size()>=64)throw std::runtime_error("Synthetic device lifetime capacity");
    state->value.id=states.size()+1;state->value.owner=owner;states.push_back(state);return state;
}
}
OwnerScope::OwnerScope(const std::string& value):previous_(owner){owner=value;}
OwnerScope::~OwnerScope(){owner=previous_;}
std::vector<Snapshot> snapshots(){
    std::lock_guard<std::mutex> registryLock(registryGate);std::vector<Snapshot> result;
    for(const auto& state:states){std::lock_guard<std::mutex> lock(state->gate);result.push_back(state->value);}return result;
}
void step(){
    std::vector<std::shared_ptr<State>> active;
    {std::lock_guard<std::mutex> lock(registryGate);active=states;}
    for(const auto& state:active){
        std::function<void(const std::vector<unsigned char>&)> capture;
        std::function<bool(std::int16_t*,std::size_t)> playout;
        peerchat::Audio::RenderReference rendered;
        Snapshot before;float input=1,output=1;
        {std::lock_guard<std::mutex> lock(state->gate);before=state->value;if(!before.active)continue;
            capture=state->capture;playout=state->playout;rendered=state->rendered;input=state->inputGain;output=state->outputGain;}
        if(!before.muted&&capture){
            std::vector<unsigned char> bytes(640);
            const double frequency=before.id%2?330.:660.;
            for(unsigned i=0;i<320;++i){const double t=(before.captures*320+i)/16000.;
                const auto sample=static_cast<std::int16_t>(input*9000*std::sin(2*3.141592653589793*frequency*t));
                bytes[2*i]=static_cast<unsigned char>(sample);bytes[2*i+1]=static_cast<unsigned char>(static_cast<std::uint16_t>(sample)>>8);}
            capture(bytes);std::lock_guard<std::mutex> lock(state->gate);++state->value.captures;
        }
        std::array<std::int16_t,320> pcm{};const bool supplied=playout&&playout(pcm.data(),pcm.size());
        std::uint64_t energy=0;double real330=0,imag330=0,real660=0,imag660=0;
        for(unsigned i=0;i<pcm.size();++i){auto& sample=pcm[i];sample=static_cast<std::int16_t>(std::clamp(float(sample)*output,-32768.f,32767.f));energy+=std::abs(int(sample));
            const double phase=2*3.141592653589793*330*i/16000.;
            real330+=sample*std::cos(phase);imag330+=sample*std::sin(phase);
            real660+=sample*std::cos(2*phase);imag660+=sample*std::sin(2*phase);}
        if(rendered)rendered(pcm.data(),pcm.size(),20);
        {std::lock_guard<std::mutex> lock(state->gate);++state->value.pulls;if(supplied)++state->value.supplied;state->value.energy+=energy;
            state->value.tone330+=real330*real330+imag330*imag330;state->value.tone660+=real660*real660+imag660*imag660;}
    }
}
}

namespace peerchat {
struct Audio::Impl {std::shared_ptr<voice_integration_audio::State> state=voice_integration_audio::create();};
Audio::Audio():impl_(std::make_unique<Impl>()){}
Audio::~Audio(){stop();}
bool Audio::start(std::wstring& error){
    auto& state=*impl_->state;std::lock_guard<std::mutex> lock(state.gate);
    if(!state.capture||!state.playout||state.value.owner.empty()){error=L"Synthetic port requires real media callbacks and owner";return false;}
    state.value.active=true;++state.value.starts;return true;
}
void Audio::stop(){auto& state=*impl_->state;std::lock_guard<std::mutex> lock(state.gate);state.value.active=false;state.capture={};state.playout={};state.rendered={};}
void Audio::setDevices(int,int){}
std::vector<Audio::Device> Audio::inputDevices(){return {{-1,L"TEST synthetic PCM input"}};}
std::vector<Audio::Device> Audio::outputDevices(){return {{-1,L"TEST synthetic PCM sink"}};}
bool Audio::setMuted(bool muted,std::wstring&){auto& state=*impl_->state;std::lock_guard<std::mutex> lock(state.gate);state.value.muted=muted;return true;}
void Audio::setVolumes(float input,float output){auto& state=*impl_->state;std::lock_guard<std::mutex> lock(state.gate);state.inputGain=input;state.outputGain=output;}
void Audio::play(int,const std::vector<unsigned char>&){throw std::runtime_error("Expected real device-paced receive handler");}
std::vector<std::vector<unsigned char>> Audio::takeCaptured(){return {};}
void Audio::setCaptureHandler(std::function<void(const std::vector<unsigned char>&)> callback){auto& state=*impl_->state;std::lock_guard<std::mutex> lock(state.gate);state.capture=std::move(callback);}
bool Audio::setPlayoutHandler(std::function<bool(std::int16_t*,std::size_t)> callback){auto& state=*impl_->state;std::lock_guard<std::mutex> lock(state.gate);if(state.value.active)return false;state.playout=std::move(callback);return true;}
bool Audio::setRenderReference(RenderReference callback){auto& state=*impl_->state;std::lock_guard<std::mutex> lock(state.gate);if(state.value.active)return false;state.rendered=std::move(callback);return true;}
std::wstring Audio::takeError(){return {};}
std::wstring Audio::diagnostics(){return L"audio_platform=TEST synthetic device; no physical capture/playback";}
MediaTelemetry Audio::telemetry(){auto& state=*impl_->state;std::lock_guard<std::mutex> lock(state.gate);return {0,0,state.value.active&&!state.value.muted,state.value.active,state.value.active};}
}
