#pragma once
#include "voice_application.hpp"
#include "android_media_capabilities.hpp"
#include "reliable_endpoint.hpp"
#include <ctime>

namespace peerchat::portable {
// Android execution wiring only. Reuses the profile's existing friend snapshot,
// session winner, CallManager and real VoiceMedia. Never supplies fallback audio.
class AndroidVoiceApplication {
    Storage& store_;
    reliable::AuthenticatedFriendSnapshot& friends_;
    session::SessionTransportManager& sessions_;
    std::map<std::uint64_t,std::unique_ptr<ReliableEndpoint>>& peers_;
    std::function<std::uint64_t()> network_;
    std::optional<MediaIceConfiguration> ice_;
    std::uint64_t networkObserved_=0;
    bool authorized(const std::string& peer){
        if(!friends_.localAccount()||!friends_.contains(peer))return false;
        for(const auto& contact:store_.contacts())if(contact.peerId==peer&&contact.verified&&Storage::publicKeyId(contact.publicKey)==peer)return true;
        return false;
    }
    ReliableEndpoint* endpoint(const std::string& peer){
        auto state=sessions_.snapshot(peer);if(!state||!state->connected)return nullptr;
        auto found=peers_.find(state->activeTransport.value);return found!=peers_.end()&&found->second->ready()?found->second.get():nullptr;
    }
    VoiceApplication::Control control(const std::string& peer){
        auto target=endpoint(peer);auto capabilities=target?target->voiceCapabilities():0;
        return {target!=nullptr,(capabilities&protocol::CapabilityVoiceCalls)!=0,(capabilities&protocol::CapabilityMediaRecovery)!=0};
    }
    std::unique_ptr<VoiceApplication::Media> media(const VoiceMedia::ReceiveBinding& binding){
        if(!androidVoiceAvailable()||!ice_||!ice_->valid(static_cast<std::int64_t>(std::time(nullptr)))||
            ice_->profileSessionGeneration!=friends_.generation()||ice_->discoveryGeneration!=friends_.generation()||!authorized(binding.remotePeerId))return {};
        auto engine=std::make_unique<VoiceMedia>(*ice_,-1,-1,network_(),std::string{},VoiceMedia::ProcessingOptions{},binding);
        return std::make_unique<VoiceApplication::RealMedia>(std::move(engine));
    }
public:
    VoiceApplication app;
    AndroidVoiceApplication(calls::CallManager& calls,Storage& store,const Profile& profile,reliable::AuthenticatedFriendSnapshot& friends,
                            session::SessionTransportManager& sessions,std::map<std::uint64_t,std::unique_ptr<ReliableEndpoint>>& peers,
                            std::function<std::uint64_t()> network)
        :store_(store),friends_(friends),sessions_(sessions),peers_(peers),network_(std::move(network)),
         app(calls,profile.peerId,{[]{return androidVoiceAvailable();},[this](const std::string& peer){return authorized(peer);},
             [this](const std::string& peer){return control(peer);},
             [this](const std::string& peer,const Blob& wire){auto target=endpoint(peer);return target&&target->sendVoiceControl(wire);},
             [this](const VoiceMedia::ReceiveBinding& binding){return media(binding);}}){}
    bool installIce(MediaIceConfiguration value){
        if(!androidVoiceAvailable()||!friends_.localAccount()||value.profileSessionGeneration!=friends_.generation()||
            value.discoveryGeneration!=friends_.generation()||!value.valid(static_cast<std::int64_t>(std::time(nullptr))))return false;
        if(ice_&&value.generation<ice_->generation)return false;
        clearIce();ice_=std::move(value);return true;
    }
    void clearIce(){if(ice_){std::fill(ice_->password.begin(),ice_->password.end(),'\0');std::fill(ice_->username.begin(),ice_->username.end(),'\0');ice_.reset();}}
    void invalidate(){app.invalidate();clearIce();networkObserved_=0;}
    void tick(std::uint64_t now){
        auto network=network_();if(networkObserved_&&network!=networkObserved_)app.networkChanged(now);networkObserved_=network;
        app.tick(now);
        if(auto active=app.active())sessions_.demandLiveRoute(active->participants.begin()->first,now+2000,now);
    }
    ~AndroidVoiceApplication(){invalidate();}
};
}
