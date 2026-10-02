#pragma once
#include "voice_media_topology.hpp"
#include "voice_receive_engine.hpp"
#include "audio.hpp"
#include <atomic>
namespace peerchat {
// Device abstraction is capture/playout only, with the device owning pacing.
class IVoiceAudioDevice {
public:
    using Capture=std::function<void(const std::vector<unsigned char>&)>;
    using Playout=std::function<bool(std::int16_t*,std::size_t)>;
    virtual ~IVoiceAudioDevice()=default;
    virtual bool start(int input,int output,float inputGain,float outputGain,Capture,Playout,std::wstring& error)=0;
    virtual bool mute(bool,std::wstring&)=0;
    virtual void stop()=0;
    virtual std::wstring takeError(){return {};}
};
struct VoiceRoomBinding {
    std::string group,room,epoch,localDevice,participation;
    std::uint64_t localAccount=0,revision=0,rosterGeneration=0,generation=0;
};
// One local room participation. Room authority is supplied by the application;
// the session never owns the server roster, reliable sessions or reconnect policy.
// Application control calls (including snapshot/stop) are serialized by their
// existing owner. Capture and device-paced playout execute on the device worker.
class GroupVoiceSession {
public:
    explicit GroupVoiceSession(std::unique_ptr<IVoiceMediaTopology> topology,std::unique_ptr<IVoiceAudioDevice> device={});
    ~GroupVoiceSession();
    bool start(VoiceRoomBinding binding,MediaIceConfiguration ice,const std::wstring& receiveDll,
               VoiceMedia::ProcessingOptions processing,int input,int output,float inputGain,float outputGain,std::wstring& error);
    bool reconcile(std::uint64_t revision,std::uint64_t rosterGeneration,const std::vector<AuthorizedVoiceSource>& sources);
    void configure(MediaIceConfiguration ice);
    bool signaling(const VoiceTransportSignal& signal);
    std::vector<VoiceTransportSignal> takeSignals();
    bool mute(bool value,std::wstring& error);
    void deafen(bool value);
    void stop();
    struct Snapshot {
        VoiceRoomBinding binding;
        bool active=false,muted=false,deafened=false,apmActive=false;
        std::uint64_t encodedPackets=0,fanoutCopies=0,captureBlocks=0;
        unsigned apmFailure=0;
        VoiceReceiveEngine::Snapshot receive;
        std::vector<VoiceRouteSnapshot> routes;
        std::wstring error;
    };
    Snapshot snapshot();
private:
    VoiceRoomBinding binding_;
    std::unique_ptr<IVoiceMediaTopology> topology_;
    std::unique_ptr<IVoiceAudioDevice> device_;
    std::shared_ptr<VoiceReceiveEngine> receive_;
    LocalVoiceSource source_;
    std::map<std::string,AuthorizedVoiceSource> authorized_;
    std::atomic<bool> active_{false},muted_{false},deafened_{false};
    std::atomic<std::uint64_t> copies_{0};
    std::wstring error_;
};
}
