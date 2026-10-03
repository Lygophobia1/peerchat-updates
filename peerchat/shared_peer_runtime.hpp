#pragma once
#include "reliable_peer_protocol.hpp"
#include "tls.hpp"

namespace peerchat::reliable {
// Synchronous, per-transport execution plumbing. The platform owns its worker or
// gate and the TLS object. SessionTransportManager and DeliveryManager remain
// authoritative; this runtime never schedules, selects a winner or persists.
// Callbacks are stack-local, never retained or invoked by a background thread.
class SharedPeerRuntime : public PeerProtocolState {
    bool stopped_=false;
public:
    enum class FrameKind { Hello, Accepted, Application };
    bool stopped()const noexcept{return stopped_;}
    void stop()noexcept{stopped_=true;}
    FrameKind enter(protocol::Reader& reader,bool relay,const Blob& tlsKey,const std::string& localId) {
        protocol::check(!stopped_);
        if(!hello){observeHello(reader,relay,tlsKey,localId);return FrameKind::Hello;}
        if(reader.type()==protocol::Accepted){observeAccepted(reader);return FrameKind::Accepted;}
        return FrameKind::Application;
    }
    void requireOnline(bool ownerOnline)const{protocol::check(!stopped_&&ownerOnline);}
    template<class Tls,class Continue,class Collect,class Send,class Dispatch>
    bool receive(Tls& tls,const Blob& encrypted,const Profile& local,
                 std::uint64_t capabilities,std::uint16_t localPort,
                 Continue continuing,Collect collect,Send send,Dispatch dispatch) {
        protocol::check(!stopped_);
        tls.receive(encrypted);collect();
        if(tls.closed())return false;
        if(tls.ready()&&!hello&&peerId.empty()){
            peerId=Storage::publicKeyId(tls.remotePublicKey());
            auto packet=helloPacket(capabilities,localPort,local.name);send(packet);
        }
        input.receive(tls.takePlaintext());
        while(!stopped_&&continuing()){
            auto frame=input.take();if(!frame)break;
            dispatch(*frame);
        }
        return true;
    }
};
}
