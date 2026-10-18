#pragma once
#include "group_application.hpp"
#include "../group_voice_session.hpp"
#include "android_media_capabilities.hpp"
#include <deque>
#include <ctime>

namespace peerchat::portable {
// Profile-gated media execution only. GroupApplication remains the roster
// authority and the Android application remains the room authority.
class AndroidGroupVoiceApplication {
    GroupVoiceSession session_{std::make_unique<RelayMeshVoiceTopology>()};
    VoiceRoomBinding binding_{};
    std::vector<AuthorizedVoiceSource> sources_;
    std::deque<VoiceTransportSignal> outgoing_;
    std::uint64_t lastGeneration_=0;
    std::uint64_t lastCredentialGeneration_=0;
    std::uint64_t ownerAccount_=0,ownerAuthenticationGeneration_=0;
    bool active_=false;
    bool audioFailed_=false;
    std::wstring receiveDll_;
    static bool id(const std::string& s,std::size_t size){return protocol::id(s,size);}
    static bool member(const group_keys::RosterSnapshot& roster,const AuthorizedVoiceSource& source){
        return group_keys::GroupKeyManager::currentMember(roster,source.account,source.device);
    }
    static bool sourcesValid(const group_keys::RosterSnapshot& roster,const VoiceRoomBinding& binding,
                             const std::vector<AuthorizedVoiceSource>& sources){
        if(sources.size()>32)return false;
        std::set<std::string> seen;
        for(const auto& source:sources)if(!source.account||!id(source.device,64)||!id(source.participation,32)||
            source.device==binding.localDevice||!seen.insert(source.device).second||!member(roster,source))return false;
        return true;
    }
public:
    AndroidGroupVoiceApplication()=default;
#if !defined(__ANDROID__)
    // Host-only adapter test uses the genuine pinned Windows NetEq DLL.
    explicit AndroidGroupVoiceApplication(std::wstring receiveDll):receiveDll_(std::move(receiveDll)){}
#endif
    bool active()const{return active_;}
    bool audioFailed()const{return audioFailed_;}
    const VoiceRoomBinding& binding()const{return binding_;}
    GroupVoiceSession::Snapshot snapshot(){
        auto state=session_.snapshot();if(active_&&(!state.active||!state.error.empty())){
            audioFailed_=true;stop();return session_.snapshot();
        }
        return state;
    }
    void stop(){session_.stop();active_=false;binding_={};sources_.clear();outgoing_.clear();}
    void invalidate(){stop();audioFailed_=false;}
    bool current(const GroupApplication* groups,std::uint64_t account,std::uint64_t authenticationGeneration,
                 const std::string& localDevice){
        if(!active_)return false;
        snapshot();if(!active_)return false;
        auto roster=groups&&groups->generation()==authenticationGeneration&&groups->account()==account?
            groups->roster(binding_.group):nullptr;
        if(!roster||binding_.localAccount!=account||binding_.localDevice!=localDevice||
           roster->generation!=binding_.rosterGeneration||
           !group_keys::GroupKeyManager::currentMember(*roster,account,localDevice)||
           !sourcesValid(*roster,binding_,sources_)){stop();return false;}
        return true;
    }
    bool matches(const VoiceRoomBinding& value)const{
        return active_&&binding_.group==value.group&&binding_.room==value.room&&binding_.epoch==value.epoch&&
            binding_.localDevice==value.localDevice&&binding_.participation==value.participation&&
            binding_.localAccount==value.localAccount&&binding_.generation==value.generation;
    }
    bool start(VoiceRoomBinding value,std::uint64_t authenticationGeneration,const GroupApplication* groups,
               MediaIceConfiguration ice,const std::vector<AuthorizedVoiceSource>& sources){
        if(active_||!androidVoiceAvailable()||!groups||groups->generation()!=authenticationGeneration||
           groups->account()!=value.localAccount||!id(value.group,32)||!id(value.room,32)||!id(value.epoch,32)||
           !id(value.localDevice,64)||!id(value.participation,32)||!value.revision||!value.rosterGeneration||
           !value.generation||!ice.valid(std::time(nullptr))||
           ice.profileSessionGeneration!=authenticationGeneration||ice.discoveryGeneration!=authenticationGeneration)return false;
        if(ownerAccount_!=value.localAccount||ownerAuthenticationGeneration_!=authenticationGeneration){
            lastGeneration_=0;lastCredentialGeneration_=0;
            ownerAccount_=value.localAccount;ownerAuthenticationGeneration_=authenticationGeneration;
        }
        if(value.generation<=lastGeneration_)return false;
        auto roster=groups->roster(value.group);
        if(!roster||roster->generation!=value.rosterGeneration||
           !group_keys::GroupKeyManager::currentMember(*roster,value.localAccount,value.localDevice)||
           !sourcesValid(*roster,value,sources))return false;
        if(ice.generation<=lastCredentialGeneration_)return false;
        audioFailed_=false;lastGeneration_=value.generation;lastCredentialGeneration_=ice.generation;
        VoiceMedia::ProcessingOptions processing{};processing.enabled=false;processing.noiseSuppression=false;
        processing.automaticGain=false;processing.echoCancellation=false;
        std::wstring error;
        if(!session_.start(value,std::move(ice),receiveDll_,processing,-1,-1,1.f,1.f,error)){audioFailed_=true;stop();return false;}
        if(!session_.reconcile(value.revision,value.rosterGeneration,sources)){stop();return false;}
        binding_=std::move(value);sources_=sources;active_=true;return true;
    }
    bool reconcile(const VoiceRoomBinding& value,std::uint64_t revision,std::uint64_t rosterGeneration,
                   const std::vector<AuthorizedVoiceSource>& sources,const GroupApplication* groups,
                   std::uint64_t authenticationGeneration){
        if(!matches(value)||!groups||groups->generation()!=authenticationGeneration||revision<binding_.revision||
           rosterGeneration!=binding_.rosterGeneration)return false;
        auto roster=groups->roster(binding_.group);
        if(!roster||roster->generation!=rosterGeneration||!sourcesValid(*roster,binding_,sources))return false;
        if(!session_.reconcile(revision,rosterGeneration,sources)){stop();return false;}
        binding_.revision=revision;sources_=sources;return true;
    }
    bool configure(const VoiceRoomBinding& value,MediaIceConfiguration ice,std::uint64_t authenticationGeneration){
        if(!matches(value)||!ice.valid(std::time(nullptr))||ice.profileSessionGeneration!=authenticationGeneration||
           ice.discoveryGeneration!=authenticationGeneration||ice.generation<=lastCredentialGeneration_)return false;
        lastCredentialGeneration_=ice.generation;session_.configure(std::move(ice));return true;
    }
    bool signal(const VoiceRoomBinding& value,const VoiceTransportSignal& signal){return matches(value)&&session_.signaling(signal);}
    std::vector<VoiceTransportSignal> takeSignals(const VoiceRoomBinding& value){
        if(!matches(value))return {};
        for(auto& signal:session_.takeSignals())if(outgoing_.size()<128)outgoing_.push_back(std::move(signal));
        std::vector<VoiceTransportSignal> batch;std::size_t bytes=0;
        while(!outgoing_.empty()&&batch.size()<8){
            const auto& next=outgoing_.front();auto cost=next.sourceKey.size()+next.routeId.size()+next.kind.size()+next.data.size()+next.mid.size()+80;
            if(bytes+cost>60000)break;
            bytes+=cost;batch.push_back(std::move(outgoing_.front()));outgoing_.pop_front();
        }
        return batch;
    }
    bool mute(const VoiceRoomBinding& value,bool enabled){std::wstring error;return matches(value)&&session_.mute(enabled,error);}
    bool deafen(const VoiceRoomBinding& value,bool enabled){if(!matches(value))return false;session_.deafen(enabled);return true;}
    bool stop(const VoiceRoomBinding& value){if(!matches(value))return false;stop();return true;}
};
}
