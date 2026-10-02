#include "group_voice_session.hpp"
namespace peerchat {
namespace {
class WindowsVoiceAudioDevice final:public IVoiceAudioDevice {
    Audio audio_;
public:
    bool start(int input,int output,float inputGain,float outputGain,Capture capture,Playout playout,std::wstring& error)override{
        audio_.setDevices(input,output);audio_.setVolumes(inputGain,outputGain);audio_.setCaptureHandler(std::move(capture));
        return audio_.setPlayoutHandler(std::move(playout))&&audio_.start(error);
    }
    bool mute(bool value,std::wstring& error)override{return audio_.setMuted(value,error);}
    void stop()override{audio_.stop();}
    std::wstring takeError()override{return audio_.takeError();}
};
}
GroupVoiceSession::GroupVoiceSession(std::unique_ptr<IVoiceMediaTopology> topology,std::unique_ptr<IVoiceAudioDevice> device):
    topology_(std::move(topology)),device_(device?std::move(device):std::make_unique<WindowsVoiceAudioDevice>()){}
GroupVoiceSession::~GroupVoiceSession(){stop();}
bool GroupVoiceSession::start(VoiceRoomBinding binding,MediaIceConfiguration ice,const std::wstring& dll,
    VoiceMedia::ProcessingOptions processing,int input,int output,float inputGain,float outputGain,std::wstring& error){
    stop();error.clear();error_.clear();
    if(!topology_||binding.group.size()!=32||binding.room.size()!=32||binding.epoch.size()!=32||binding.localDevice.size()!=64||binding.participation.size()!=32||!binding.localAccount||!binding.rosterGeneration||!binding.generation){error=L"Invalid authorized voice-room binding";return false;}
    binding_=std::move(binding);receive_=std::make_shared<VoiceReceiveEngine>();
    if(!receive_->open(dll)||!source_.open(processing)){error=L"Voice audio engine unavailable";stop();return false;}
    auto receive=receive_;
    if(!topology_->start(binding_.localDevice,std::move(ice),[receive](const std::string& key,std::uint64_t generation,const unsigned char* bytes,std::size_t size){receive->insertRtp(key,generation,bytes,size);})){error=L"TURN voice topology unavailable";stop();return false;}
    copies_=0;muted_=false;deafened_=false;active_=true;
    if(!device_->start(input,output,inputGain,outputGain,[this](const std::vector<unsigned char>& pcm){
        if(!active_||muted_)return;
        LocalVoiceSource::Packet packet;
        if(source_.process20ms(pcm.data(),pcm.size(),packet)&&active_&&!muted_)copies_+=topology_->sendEncoded(packet);
    },[this,receive](std::int16_t* pcm,std::size_t samples){return active_&&receive->pull20ms(pcm,samples,deafened_);},error)){
        error_=error;stop();return false;
    }
    error_=error;return true;
}
bool GroupVoiceSession::reconcile(std::uint64_t revision,std::uint64_t rosterGeneration,const std::vector<AuthorizedVoiceSource>& sources){
    if(!active_||revision<binding_.revision||rosterGeneration<binding_.rosterGeneration||sources.size()>32)return false;
    std::map<std::string,AuthorizedVoiceSource> next;
    for(const auto& source:sources){if(!source.account||source.device.size()!=64||source.participation.size()!=32||source.device==binding_.localDevice||!next.emplace(source.key(),source).second)return false;}
    for(const auto& old:authorized_)if(!next.count(old.first))receive_->retire(old.first);
    for(const auto& source:next)if(!authorized_.count(source.first)&&!receive_->authorize(source.first,1))return false;
    if(!topology_->reconcile(sources)){error_=L"Voice topology rejected authorized source set";return false;}
    authorized_=std::move(next);binding_.revision=revision;binding_.rosterGeneration=rosterGeneration;return true;
}
void GroupVoiceSession::configure(MediaIceConfiguration ice){topology_->configure(std::move(ice));}
bool GroupVoiceSession::signaling(const VoiceTransportSignal& signal){return active_&&authorized_.count(signal.sourceKey)&&topology_->signaling(signal);}
std::vector<VoiceTransportSignal> GroupVoiceSession::takeSignals(){return active_?topology_->takeSignals():std::vector<VoiceTransportSignal>{};}
bool GroupVoiceSession::mute(bool value,std::wstring& error){
    muted_=value;source_.setMuted(value);
    if(!active_)return false;
    bool result=device_->mute(value,error);if(!result){muted_=true;source_.setMuted(true);error_=error;}return result;
}
void GroupVoiceSession::deafen(bool value){deafened_=value;}
void GroupVoiceSession::stop(){
    active_=false;if(device_)device_->stop();if(topology_)topology_->stop();
    if(receive_)receive_->close();
    receive_.reset();source_.close();authorized_.clear();
}
GroupVoiceSession::Snapshot GroupVoiceSession::snapshot(){
    if(active_){auto error=device_->takeError();if(!error.empty())error_=std::move(error);}
    Snapshot result;result.binding=binding_;result.active=active_;result.muted=muted_;result.deafened=deafened_;
    result.encodedPackets=source_.encodedPackets();result.fanoutCopies=copies_;result.captureBlocks=source_.processing().captureCalls();result.apmActive=source_.processing().active();result.apmFailure=source_.processing().failure();result.error=error_;
    if(receive_)result.receive=receive_->snapshot();
    if(active_)result.routes=topology_->snapshot();
    return result;
}
}
