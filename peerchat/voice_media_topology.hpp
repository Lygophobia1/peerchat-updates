#pragma once
#include "media_ice_configuration.hpp"
#include "local_voice_source.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace peerchat {
struct AuthorizedVoiceSource {
    std::uint64_t account=0;
    std::string device,participation;
    std::string key()const{return std::to_string(account)+":"+device+":"+participation;}
};
struct VoiceTransportSignal {
    std::string sourceKey,routeId,kind,data,mid="0";
    std::uint64_t generation=1;
};
struct VoiceRouteSnapshot {
    std::string sourceKey,routeId,state,reason;
    std::uint64_t generation=1,sentPackets=0,sentBytes=0,receivedPackets=0,receivedBytes=0,droppedPackets=0;
    bool connected=false,turn=false;
};
// Encoded audio and attributed RTP only. No room policy, DSP, device or timer.
class IVoiceMediaTopology {
public:
    using Incoming=std::function<void(const std::string&,std::uint64_t,const unsigned char*,std::size_t)>;
    virtual ~IVoiceMediaTopology()=default;
    virtual bool start(const std::string& localDevice,MediaIceConfiguration ice,Incoming incoming)=0;
    virtual void configure(MediaIceConfiguration ice)=0;
    virtual bool reconcile(const std::vector<AuthorizedVoiceSource>& sources)=0;
    virtual bool signaling(const VoiceTransportSignal& signal)=0;
    virtual unsigned sendEncoded(const LocalVoiceSource::Packet& packet)=0;
    virtual std::vector<VoiceTransportSignal> takeSignals()=0;
    virtual std::vector<VoiceRouteSnapshot> snapshot()=0;
    virtual void stop()=0;
};
// Initial topology only; a future SFU implements the same attributed-source API.
class RelayMeshVoiceTopology final:public IVoiceMediaTopology {
public:
    RelayMeshVoiceTopology();~RelayMeshVoiceTopology()override;
    bool start(const std::string&,MediaIceConfiguration,Incoming)override;
    void configure(MediaIceConfiguration)override;
    bool reconcile(const std::vector<AuthorizedVoiceSource>&)override;
    bool signaling(const VoiceTransportSignal&)override;
    unsigned sendEncoded(const LocalVoiceSource::Packet&)override;
    std::vector<VoiceTransportSignal> takeSignals()override;
    std::vector<VoiceRouteSnapshot> snapshot()override;
    void stop()override;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
