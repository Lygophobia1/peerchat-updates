#pragma once
#include "media_ice_configuration.hpp"
#include "windows_media/peerchat_screen_media.h"
#include <memory>
#include <string>
#include <vector>
namespace peerchat {
struct AuthorizedScreenEndpoint {
    std::uint64_t account=0;
    std::string device,participation;
    std::string key()const{return std::to_string(account)+":"+device+":"+participation;}
};
struct ScreenTransportSignal {
    std::string sourceKey,routeId,kind,data,mid="0";
    std::uint64_t generation=1;
};
struct ScreenRouteSnapshot {
    std::string sourceKey,routeId,state,reason,details;
    bool connected=false,turn=false;
    std::uint64_t sent=0,bytes=0,received=0,receivedBytes=0,dropped=0,decoded=0,presented=0;
};
// Encoded AUs and authorized endpoints only. No room authority, capture,
// reliable-session ownership or timer. Alternate topologies reuse this boundary.
class IScreenShareMediaTopology {
public:
    virtual ~IScreenShareMediaTopology()=default;
    virtual bool start(bool sender,MediaIceConfiguration ice,std::uint64_t window,unsigned fps)=0;
    virtual void configure(MediaIceConfiguration ice)=0;
    virtual bool reconcile(const std::vector<AuthorizedScreenEndpoint>&)=0;
    virtual bool signaling(const ScreenTransportSignal&)=0;
    virtual unsigned sendEncoded(const std::uint8_t*,const pc_screen_encoded_frame&)=0;
    virtual std::vector<ScreenTransportSignal> takeSignals()=0;
    virtual bool takeKeyframeRequest()=0;
    virtual std::vector<ScreenRouteSnapshot> snapshot()=0;
    virtual void stop()=0;
};
class RelayMeshScreenShareTopology final:public IScreenShareMediaTopology {
public:
    RelayMeshScreenShareTopology();~RelayMeshScreenShareTopology()override;
    bool start(bool,MediaIceConfiguration,std::uint64_t,unsigned)override;
    void configure(MediaIceConfiguration)override;
    bool reconcile(const std::vector<AuthorizedScreenEndpoint>&)override;
    bool signaling(const ScreenTransportSignal&)override;
    unsigned sendEncoded(const std::uint8_t*,const pc_screen_encoded_frame&)override;
    std::vector<ScreenTransportSignal> takeSignals()override;
    bool takeKeyframeRequest()override;
    std::vector<ScreenRouteSnapshot> snapshot()override;
    void stop()override;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
