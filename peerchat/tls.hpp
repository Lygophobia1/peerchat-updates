#pragma once
#include "storage.hpp"
#include <memory>

namespace peerchat {
// TLS 1.2 via Windows Schannel, with certificates backed by persistent P-256 keys.
// A session proves key possession. The application must separately approve/pin
// remotePublicKey() before it sends or accepts application messages.
class TlsCredentials {
public:
    explicit TlsCredentials(const Profile& identity);
    ~TlsCredentials();
    TlsCredentials(const TlsCredentials&)=delete;
    TlsCredentials& operator=(const TlsCredentials&)=delete;
    // Public certificate metadata only.  This is deliberately diagnostic data:
    // it never includes the private key or any TLS/session secret.
    std::string certificateDiagnostics() const;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    friend class TlsSession;
};
class TlsSession {
public:
    TlsSession(const TlsCredentials& identity,bool incoming);
    ~TlsSession();
    TlsSession(const TlsSession&)=delete;
    TlsSession& operator=(const TlsSession&)=delete;
    // Feed encrypted bytes received from a socket; includes handshake processing.
    void receive(const Blob& encrypted);
    Blob takeOutput();
    Blob takePlaintext();
    bool ready() const;
    Blob remotePublicKey() const;
    void send(const Blob& plaintext);
    void shutdown();
    bool closed() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
