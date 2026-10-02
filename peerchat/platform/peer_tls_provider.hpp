#pragma once
#include <memory>
#include <string>
#include <vector>

namespace peerchat::platform {
// Platform TLS byte transformation only. SessionTransportManager remains the
// session owner. Implementations own no sockets, routing or reconnect policy.
class PeerTlsProvider {
public:
    using Bytes=std::vector<unsigned char>;
    virtual ~PeerTlsProvider()=default;
    virtual void receive(const Bytes&)=0;
    virtual Bytes takeOutput()=0;
    virtual Bytes takePlaintext()=0;
    virtual bool ready() const=0;
    virtual Bytes remotePublicKey() const=0;
    virtual void send(const Bytes&)=0;
    virtual void shutdown()=0;
    virtual bool closed() const=0;
};
class PeerTlsFactory {
public:
    virtual ~PeerTlsFactory()=default;
    virtual PeerTlsProvider::Bytes publicKey() const=0;
    virtual std::unique_ptr<PeerTlsProvider> create(bool incoming,const std::string& expectedPeer)=0;
};
}
