#pragma once
#include "../call_manager.hpp"
#include "../protocol.hpp"
#include "../voice_media.hpp"
#include <array>
#include <functional>
#include <string_view>

namespace peerchat::portable {
// Thread-confined application adapter. CallManager remains the sole call and
// recovery owner; authenticated endpoints remain the sole signaling transport.
class VoiceApplication {
public:
    struct Control { bool connected=false,voice=false,recovery=false; };
    class Media {
    public:
        virtual ~Media()=default;
        virtual bool startOffer()=0;
        virtual bool startAnswer(const std::string&)=0;
        virtual bool setAnswer(const std::string&)=0;
        virtual bool addCandidate(const std::string&,const std::string&)=0;
        virtual bool setMuted(bool)=0;
        virtual void setDeafened(bool)=0;
        virtual std::vector<VoiceMedia::Event> poll()=0;
        virtual VoiceMedia::Route route()=0;
        virtual std::wstring diagnostics(){return L"Media diagnostics unavailable";}
        virtual void stop()=0;
    };
    // The production factory wraps the existing real media engine here. No
    // substitute engine is constructed when media dependencies are unavailable.
    class RealMedia final:public Media {
        std::unique_ptr<VoiceMedia> media_;
    public:
        explicit RealMedia(std::unique_ptr<VoiceMedia> media):media_(std::move(media)){protocol::check(bool(media_));}
        bool startOffer()override{std::wstring error;return media_->startOffer(error);}
        bool startAnswer(const std::string& offer)override{std::wstring error;return media_->startAnswer(offer,error);}
        bool setAnswer(const std::string& answer)override{std::wstring error;return media_->setAnswer(answer,error);}
        bool addCandidate(const std::string& candidate,const std::string& mid)override{std::wstring error;return media_->addCandidate(candidate,mid,error);}
        bool setMuted(bool value)override{std::wstring error;return media_->setMuted(value,error);}
        void setDeafened(bool value)override{media_->setDeafened(value);}
        std::vector<VoiceMedia::Event> poll()override{return media_->poll();}
        VoiceMedia::Route route()override{return media_->route();}
        std::wstring diagnostics()override{return media_->diagnostics();}
        void stop()override{media_->stop();}
    };
    struct Hooks {
        std::function<bool()> available;
        std::function<bool(const std::string&)> authorized;
        std::function<Control(const std::string&)> control;
        std::function<bool(const std::string&,const Blob&)> send;
        // Factory must validate current authenticated ICE credentials and create
        // fresh network/media state bound to exactly this CallManager lifetime.
        std::function<std::unique_ptr<Media>(const VoiceMedia::ReceiveBinding&)> factory;
    };
    static constexpr std::uint64_t RingTimeout=30000,ConnectTimeout=20000,RecoveryWindow=30000,RecoveryRetry=4000;
private:
    calls::CallManager& calls_;
    std::string local_;
    Hooks hooks_;
    std::unique_ptr<Media> media_;
    VoiceMedia::ReceiveBinding binding_;
    const char* diagnostic_="Idle";
    bool authorized(const std::string& peer)const{return hooks_.authorized&&hooks_.authorized(peer);}
    Control control(const std::string& peer)const{return hooks_.control?hooks_.control(peer):Control{};}
    bool send(const std::string& peer,const protocol::Writer& writer){
        auto c=control(peer);return available()&&authorized(peer)&&c.connected&&c.voice&&hooks_.send&&hooks_.send(peer,writer.data);
    }
    bool signal(const std::string& peer,protocol::Type type,const std::string& id){protocol::Writer w(type);w.string(id);return send(peer,w);}
    void stopMedia()noexcept{
        if(media_)try{media_->stop();}catch(...){}
        media_.reset();binding_={};
    }
    void finish(const char* reason,bool failed=false){stopMedia();calls_.finish(failed?calls::State::Failed:calls::State::Ended);diagnostic_=reason;}
    void notifyEnd(){if(auto active=calls_.active())signal(calls_.peer(),protocol::CallEnd,active->id.value);}
    bool configure(){
        auto active=calls_.active();if(!active||!available()||!authorized(calls_.peer())||!hooks_.factory)return false;
        stopMedia();
        binding_={calls_.peer(),active->id.value,active->lifetime,active->mediaGeneration};
        try{media_=hooks_.factory(binding_);}catch(...){media_.reset();}
        if(!media_){binding_={};diagnostic_="Media configuration unavailable";return false;}
        media_->setDeafened(active->deafened);
        return true;
    }
    bool currentMedia()const{return media_&&calls_.matchesMedia(binding_.callLifetime,binding_.callId,binding_.remotePeerId,binding_.mediaGeneration);}
    // startOffer/startAnswer create Audio, but poll starts the device on its
    // Connected event. Restore microphone intent before that first poll.
    bool prepareMicrophone(){return currentMedia()&&(!calls_.active()->muted||media_->setMuted(true));}
    void interrupt(std::uint64_t now){
        auto active=calls_.active();if(!active)return;
        // Ringing is not consent to create media, including local recovery.
        if(active->state==calls::State::Incoming)return;
        if(!active->mediaRecoveryCapable){notifyEnd();finish("Media unavailable",true);return;}
        calls_.interruptMedia(now,now+RecoveryWindow);stopMedia();diagnostic_="Reconnecting media";
    }
    void serviceRecovery(std::uint64_t now){
        auto active=calls_.active();if(!active||!calls_.mediaRetryDue(now))return;
        if(active->state==calls::State::Incoming)return;
        auto c=control(calls_.peer());if(!c.connected||!c.voice){calls_.deferMediaRetry(now,500);return;}
        calls_.observeControl(true);
        if(local_<calls_.peer()){
            if(!calls_.beginMediaRecovery(now,RecoveryRetry))return;
            if(!configure()||!media_->startOffer()||!prepareMicrophone()){stopMedia();calls_.deferMediaRetry(now,1000);}
        }else{
            protocol::Writer request(protocol::CallMediaRecoveryRequest);request.string(active->id.value);request.number(active->mediaGeneration);
            send(calls_.peer(),request);calls_.deferMediaRetry(now,2000);
        }
    }
    void mediaEvents(std::uint64_t now){
        if(!media_)return;
        if(!currentMedia()){stopMedia();return;}
        const auto binding=binding_;auto events=media_->poll();
        if(events.size()>128){notifyEnd();finish("Media event capacity exceeded",true);return;}
        for(const auto& event:events){
            if(!calls_.matchesMedia(binding.callLifetime,binding.callId,binding.remotePeerId,binding.mediaGeneration))return;
            auto active=calls_.active();const auto generation=binding.mediaGeneration;
            if(event.kind==VoiceMedia::EventKind::Description){
                if(event.value.empty()||event.value.size()>32768||(event.detail!="offer"&&event.detail!="answer")){notifyEnd();finish("Invalid media description",true);return;}
                const bool offer=event.detail=="offer";
                protocol::Writer w(generation>1?(offer?protocol::CallMediaRestart:protocol::CallMediaAnswer):(offer?protocol::CallOffer:protocol::CallAnswer));
                w.string(binding.callId);if(generation>1)w.number(generation);w.string(event.value);
                if(!send(binding.remotePeerId,w)){calls_.observeControl(false,now+RecoveryWindow);interrupt(now);return;}
            }else if(event.kind==VoiceMedia::EventKind::Candidate){
                if(event.value.size()>4096||event.detail.size()>64){notifyEnd();finish("Invalid media candidate",true);return;}
                protocol::Writer w(generation>1?protocol::CallMediaCandidate:protocol::CallCandidate);
                w.string(binding.callId);if(generation>1)w.number(generation);w.string(event.value);w.string(event.detail);send(binding.remotePeerId,w);
            }else if(event.kind==VoiceMedia::EventKind::Connected){
                auto route=media_->route();auto mapped=route==VoiceMedia::Route::DirectLanIce?calls::MediaRoute::DirectLanIce:
                    route==VoiceMedia::Route::NatTraversedIce?calls::MediaRoute::NatTraversedIce:route==VoiceMedia::Route::TurnRelay?calls::MediaRoute::TurnRelay:calls::MediaRoute::Unknown;
                calls_.transition(binding.callId,calls::State::Connected);calls_.transitionMedia(generation,calls::MediaState::Connected,mapped);
                if(active->muted&&!media_->setMuted(true)){notifyEnd();finish("Microphone control failed",true);return;}
                media_->setDeafened(active->deafened);diagnostic_="Connected";signal(binding.remotePeerId,protocol::CallConnected,binding.callId);
            }else if(event.kind==VoiceMedia::EventKind::AudioError){
                // Device open/control/worker or essential receive failure leaves
                // audio unusable. End this matched lifetime instead of retaining
                // a Connected call or retrying a transport-only recovery.
                notifyEnd();finish("Audio device unavailable",true);return;
            }
            else if(event.kind==VoiceMedia::EventKind::AudioProcessingError)diagnostic_="Audio processing unavailable";
            else {interrupt(now);return;}
        }
    }
public:
    VoiceApplication(calls::CallManager& calls,std::string local,Hooks hooks):calls_(calls),local_(std::move(local)),hooks_(std::move(hooks)){protocol::check(protocol::id(local_));}
    ~VoiceApplication(){invalidate();}
    bool available()const{return hooks_.available&&hooks_.available();}
    const calls::CallSession* active()const noexcept{return calls_.active();}
    const char* diagnostic()const noexcept{return diagnostic_;}
    // Only named measurements leave native media. Never forward candidate/SDP,
    // network identifiers, credentials, or arbitrary error text to the UI.
    static std::string safeMediaDiagnostics(const std::wstring& raw){
        static constexpr std::array<std::wstring_view,55> numeric={
            L"capture_frames",L"encoded_frames",L"opus_submitted",L"send_api_accepted",L"remote_track_callback_packets",
            L"payload_delivered",L"received_packets",L"playback_frames",L"aec_render_blocks",L"aec_reference_underruns",
            L"aec_resets",L"aec_delay_ms",L"apm_capture_10ms_calls",L"apm_processed_20ms_frames",L"apm_raw_bypass_frames",
            L"apm_level_frames",L"opus_encode_failures",L"media_send_api_failures",L"opus_encoded_bytes",L"media_send_buffer_bytes",
            L"receive_insert_lock_drops",L"receive_authorized_sources",L"receive_active_sources",L"receive_rtp_accepted",
            L"receive_rtp_rejected",L"receive_insert_busy",L"receive_failure_code",L"receive_mixed_10ms_blocks",
            L"receive_busy_or_silence_10ms_blocks",L"neteq_packets_inserted",L"neteq_insert_failures",
            L"neteq_pulled_10ms_blocks",L"neteq_pull_failures",L"neteq_current_delay_ms",L"neteq_target_delay_ms",
            L"neteq_concealment_events",L"neteq_concealed_samples",L"neteq_fec_packets_received",
            L"neteq_fec_packets_discarded",L"neteq_secondary_decoded_rate_q14",L"neteq_packets_discarded",
            L"neteq_buffer_flushes",L"media_anomaly_skipped_contention", L"automatic_noise_level", L"rnnoise_vad", L"noise_gate_open", L"noise_gate_background", L"noise_gate_gain", L"noise_gate_target", L"noise_gate_attenuation_db", L"noise_floor_dbfs", L"rnnoise_input_dbfs", L"rnnoise_output_dbfs", L"rnnoise_dry_mix", L"noise_control_outgoing_dbfs"};
        static constexpr std::array<std::wstring_view,9> timed={L"capture_interval",L"playback_interval",L"capture_work",
            L"apm_capture",L"apm_render",L"opus_encode",L"receive_pull",L"rtp_arrival_interval",L"screen_capture_wait"};
        std::string out;
        if(raw.size()>262144)return "Media diagnostics unavailable";
        for(std::size_t at=0;at<raw.size();){
            auto end=raw.find(L'\n',at);if(end==std::wstring::npos)end=raw.size();
            auto eq=raw.find(L'=',at);if(eq<end&&eq-at<=64&&end-eq<=160){
                std::wstring_view key(raw.data()+at,eq-at),value(raw.data()+eq+1,end-eq-1);
                bool allowed=std::find(numeric.begin(),numeric.end(),key)!=numeric.end();
                bool performance=false;
                if(key.size()>5&&key.substr(0,5)==L"perf_"){
                    auto name=key.substr(5);performance=std::find(timed.begin(),timed.end(),name)!=timed.end();
                }
                bool valid=!value.empty();
                if(allowed&&valid){
                    std::size_t pos=value[0]==L'-'?1:0;auto digits=pos;
                    while(pos<value.size()&&value[pos]>=L'0'&&value[pos]<=L'9')++pos;
                    if(pos==digits)valid=false;
                    if(pos<value.size()&&value[pos]==L'.'){
                        ++pos;digits=pos;while(pos<value.size()&&value[pos]>=L'0'&&value[pos]<=L'9')++pos;
                        if(pos==digits)valid=false;
                    }
                    if(pos!=value.size())valid=false;
                }
                else if(performance&&valid){
                    // Fixed timing grammar from MediaPerformance::text().
                    static constexpr std::array<std::wstring_view,5> labels={L"count:",L";last_us:",L";max_us:",L";mean_us:",L";deadline_misses:"};
                    std::size_t pos=0;
                    for(auto label:labels){if(value.substr(pos,label.size())!=label){valid=false;break;}pos+=label.size();
                        auto start=pos;while(pos<value.size()&&value[pos]>=L'0'&&value[pos]<=L'9')++pos;if(pos==start)valid=false;}
                    if(pos!=value.size())valid=false;
                }else valid=false;
                if(valid&&out.size()+key.size()+value.size()+2<=16384){
                    for(auto c:key)out.push_back(static_cast<char>(c));
                    out.push_back('=');
                    for(auto c:value)out.push_back(static_cast<char>(c));
                    out.push_back('\n');
                }
            }
            at=end==raw.size()?end:end+1;
        }
        return out.empty()?"Media diagnostics unavailable":out;
    }
    std::string mediaDiagnostics()const{
        if(!currentMedia())return "Media diagnostics unavailable";
        try{return safeMediaDiagnostics(media_->diagnostics());}catch(...){return "Media diagnostics unavailable";}
    }
    bool start(const std::string& peer,const std::string& id,std::uint64_t now){
        auto c=control(peer);
        if(!available()||!protocol::id(peer)||peer==local_||!protocol::id(id,32)||!authorized(peer)||!c.connected||!c.voice||calls_.finished(id))return false;
        if(!calls_.beginOutgoing({id},calls::CallType::Voice,{peer},now+RingTimeout))return false;
        calls_.setMediaRecoveryCapable(peer,c.recovery);calls_.beginInitialMedia(id);
        if(!configure()||!media_->startOffer()||!prepareMicrophone()){finish("Media start failed",true);return false;}
        diagnostic_="Outgoing";return true;
    }
    bool answer(const std::string& id,std::uint64_t now){
        auto active=calls_.active();if(!active||active->id.value!=id||active->state!=calls::State::Incoming||active->direction!=calls::Direction::Incoming||!available()||!authorized(calls_.peer()))return false;
        auto c=control(calls_.peer());if(!c.connected||!c.voice)return false;
        calls_.transition(id,calls::State::ConnectingMedia,now+ConnectTimeout);
        if(!configure()||!media_->startAnswer(active->participants.at(calls_.peer()).remoteDescription)||!prepareMicrophone()){notifyEnd();finish("Media answer failed",true);return false;}
        auto& pending=active->participants.at(calls_.peer()).candidates;
        for(const auto& candidate:pending)media_->addCandidate(candidate.first,candidate.second);
        pending.clear();diagnostic_="Connecting media";return true;
    }
    bool decline(const std::string& id){
        auto active=calls_.active();if(!active||active->id.value!=id||active->state!=calls::State::Incoming||active->direction!=calls::Direction::Incoming)return false;
        signal(calls_.peer(),protocol::CallDecline,id);finish("Declined");return true;
    }
    bool end(const std::string& id){if(!calls_.active()||calls_.active()->id.value!=id)return false;notifyEnd();finish("Ended");return true;}
    bool setMuted(bool value){if(!calls_.active())return false;if(media_&&!media_->setMuted(value))return false;return calls_.setMuted(value);}
    bool setDeafened(bool value){if(!calls_.active())return false;if(media_)media_->setDeafened(value);return calls_.setDeafened(value);}
    // Endpoint must call this only after its ordinary friend TLS trust and
    // active-winner checks. Hooks recheck current friendship/pin and capability.
    bool receive(const std::string& peer,const Blob& wire,std::uint64_t now){
        auto c=control(peer);protocol::check(available()&&authorized(peer)&&c.connected&&c.voice&&wire.size()<=protocol::MaxPacket);
        protocol::Reader r(wire);const auto type=r.type();auto id=r.string();protocol::check(protocol::id(id,32));
        if(type==protocol::CallOffer){
            auto sdp=r.string();r.end();protocol::check(!sdp.empty()&&sdp.size()<=32768);
            auto previous=calls_.active()?calls_.active()->id.value:std::string{};
            auto disposition=calls_.beginIncoming({id},calls::CallType::Voice,peer,sdp,local_,now+RingTimeout);
            if(disposition==calls::IncomingDisposition::Replay||disposition==calls::IncomingDisposition::Duplicate)return true;
            if(disposition==calls::IncomingDisposition::Busy){signal(peer,protocol::CallBusy,id);return true;}
            if(disposition==calls::IncomingDisposition::ReplaceOutgoing){signal(peer,protocol::CallEnd,previous);stopMedia();}
            calls_.setMediaRecoveryCapable(peer,c.recovery);calls_.beginInitialMedia(id);diagnostic_="Incoming";return true;
        }
        auto active=calls_.active();const bool matching=active&&active->id.value==id&&calls_.peer()==peer;
        if(type==protocol::CallAnswer||type==protocol::CallMediaRestart||type==protocol::CallMediaAnswer){
            const auto generation=type==protocol::CallAnswer?1:r.number();auto sdp=r.string();r.end();
            protocol::check(!sdp.empty()&&sdp.size()<=32768&&(type==protocol::CallAnswer||(c.recovery&&generation>1)));
            if(!matching)return true;
            if(type==protocol::CallAnswer){
                if(active->state!=calls::State::Outgoing||active->direction!=calls::Direction::Outgoing||!currentMedia()||active->mediaGeneration!=1)return true;
                if(!media_->setAnswer(sdp)){notifyEnd();finish("Media answer rejected",true);return true;}
                calls_.transition(id,calls::State::ConnectingMedia,now+ConnectTimeout);diagnostic_="Connecting media";
            }else if(type==protocol::CallMediaRestart){
                // An incoming ringing offer has no microphone consent yet.
                // Recovery signaling cannot stand in for the local Answer action.
                if(active->state==calls::State::Incoming)return true;
                if(!calls_.acceptMediaGeneration(generation,now+RecoveryWindow))return true;
                stopMedia();if(!configure()||!media_->startAnswer(sdp)||!prepareMicrophone()){stopMedia();calls_.deferMediaRetry(now,1000);}
                diagnostic_="Reconnecting media";
            }else if(currentMedia()&&calls_.matchesMedia(binding_.callLifetime,id,peer,generation)&&!media_->setAnswer(sdp))interrupt(now);
            return true;
        }
        if(type==protocol::CallCandidate||type==protocol::CallMediaCandidate){
            const auto generation=type==protocol::CallCandidate?1:r.number();auto candidate=r.string(),mid=r.string();r.end();
            protocol::check(candidate.size()<=4096&&mid.size()<=64&&(type==protocol::CallCandidate||(c.recovery&&generation>1)));
            if(!matching||active->mediaGeneration!=generation)return true;
            if(currentMedia()){if(!media_->addCandidate(candidate,mid))diagnostic_="Media candidate rejected";}
            else if(type==protocol::CallCandidate){auto& pending=active->participants.at(peer).candidates;if(pending.size()<64)pending.emplace_back(std::move(candidate),std::move(mid));}
            return true;
        }
        if(type==protocol::CallMediaRecoveryRequest){
            auto known=r.number();r.end();protocol::check(c.recovery);
            if(matching&&local_<peer){
                if(active->mediaState==calls::MediaState::Connected)interrupt(now);
                if(calls_.active()&&calls_.active()->mediaState==calls::MediaState::Interrupted&&known>=calls_.active()->mediaGeneration)calls_.deferMediaRetry(now,0);
            }
            return true;
        }
        r.end();
        if(type==protocol::CallConnected)return true; // Remote signaling cannot assert local media connectivity.
        protocol::check(type==protocol::CallDecline||type==protocol::CallBusy||type==protocol::CallEnd);
        if(matching)finish(type==protocol::CallDecline?"Declined":type==protocol::CallBusy?"Busy":"Remote ended");
        return true;
    }
    void tick(std::uint64_t now){
        if(!calls_.active())return;
        if(!available()||!authorized(calls_.peer())){finish("Authorization unavailable",true);return;}
        auto c=control(calls_.peer());bool connected=c.connected&&c.voice;
        if(connected!=calls_.active()->controlAvailable)calls_.observeControl(connected,connected?0:now+RecoveryWindow);
        mediaEvents(now);if(!calls_.active())return;
        if(calls_.timedOut(now)||calls_.mediaTimedOut(now)||calls_.controlAndMediaTimedOut(now)){notifyEnd();finish("Call timed out",true);return;}
        serviceRecovery(now);
    }
    void networkChanged(std::uint64_t now){if(calls_.active())interrupt(now);}
    void invalidate(){finish("Authorization unavailable");}
};
}
