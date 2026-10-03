#include "../tls.hpp"
#include <stdexcept>

namespace peerchat {
struct TlsCredentials::Impl { std::shared_ptr<platform::PeerTlsFactory> factory; };
TlsCredentials::TlsCredentials(const Profile&){throw std::runtime_error("Android TLS requires opaque identity factory");}
TlsCredentials::TlsCredentials(const Profile& profile,std::shared_ptr<platform::PeerTlsFactory> factory):impl_(std::make_shared<Impl>()){
    if(!factory || !profile.identity || !profile.privateKey.empty() || factory->publicKey()!=profile.publicKey)
        throw std::runtime_error("Android TLS identity binding failed");
    impl_->factory=std::move(factory);
}
TlsCredentials::~TlsCredentials()=default;
std::string TlsCredentials::certificateDiagnostics() const{return "platform=Android; identity=opaque; peer_pin=required";}
struct TlsSession::Impl {std::unique_ptr<platform::PeerTlsProvider> provider;};
TlsSession::TlsSession(const TlsCredentials&,bool){throw std::runtime_error("Android TLS requires expected device pin");}
TlsSession::TlsSession(const TlsCredentials& credentials,bool incoming,const std::string& expected):impl_(std::make_unique<Impl>()){
    if(expected.size()!=64 || expected.find_first_not_of("0123456789abcdef")!=std::string::npos)
        throw std::runtime_error("Invalid expected TLS device pin");
    impl_->provider=credentials.impl_->factory->create(incoming,expected);
    if(!impl_->provider)throw std::runtime_error("Android TLS provider unavailable");
}
TlsSession::~TlsSession(){try{shutdown();}catch(...){}}
void TlsSession::receive(const Blob& bytes){impl_->provider->receive(bytes);}
Blob TlsSession::takeOutput(){return impl_->provider->takeOutput();}
Blob TlsSession::takePlaintext(){return impl_->provider->takePlaintext();}
bool TlsSession::ready() const{return impl_->provider->ready();}
Blob TlsSession::remotePublicKey() const{return impl_->provider->remotePublicKey();}
void TlsSession::send(const Blob& bytes){impl_->provider->send(bytes);}
void TlsSession::shutdown(){if(impl_ && impl_->provider && !impl_->provider->closed())impl_->provider->shutdown();}
bool TlsSession::closed() const{return impl_->provider->closed();}
}
