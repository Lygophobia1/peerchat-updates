#include "tls.hpp"
#include "platform/identity_provider.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value){if(!value)throw std::runtime_error("Native TLS provider contract failed");}
template<class F> void rejected(F operation){bool failed=false;try{operation();}catch(...){failed=true;}check(failed);}
struct Identity final:peerchat::platform::IdentityProvider {
    peerchat::Blob publicKey() override{return peerchat::Blob(72,1);}
    peerchat::Blob sign(const peerchat::Blob&) override{throw std::runtime_error("Contract fixture has no signer");}
};
struct Provider final:peerchat::platform::PeerTlsProvider {
    static int destroyed,shutdowns;bool shut=false;Bytes output;
    ~Provider(){++destroyed;}
    void receive(const Bytes& bytes) override{output=bytes;}
    Bytes takeOutput() override{auto result=output;output.clear();return result;}
    Bytes takePlaintext() override{return {};}
    bool ready() const override{return !shut;}
    Bytes remotePublicKey() const override{return Bytes(72,2);}
    void send(const Bytes& bytes) override{if(shut)throw std::runtime_error("Closed");output=bytes;}
    void shutdown() override{check(!shut);shut=true;++shutdowns;}
    bool closed() const override{return shut;}
};
int Provider::destroyed=0;int Provider::shutdowns=0;
struct Factory final:peerchat::platform::PeerTlsFactory {
    bool fail=false;int creates=0;
    peerchat::Blob publicKey() const override{return peerchat::Blob(72,1);}
    std::unique_ptr<peerchat::platform::PeerTlsProvider> create(bool incoming,const std::string& expected) override{
        check(incoming && expected==std::string(64,'a'));++creates;
        if(fail)throw std::runtime_error("Synthetic provider failure");
        return std::make_unique<Provider>();
    }
};
}
int main(){try{
    peerchat::Profile profile;profile.publicKey=peerchat::Blob(72,1);profile.identity=std::make_shared<Identity>();
    auto factory=std::make_shared<Factory>();peerchat::TlsCredentials credentials(profile,factory);
    rejected([&]{peerchat::TlsCredentials absent(profile);});
    rejected([&]{peerchat::TlsSession noPin(credentials,true);});
    rejected([&]{peerchat::TlsSession invalid(credentials,true,"wrong");});
    auto changed=profile;changed.publicKey[0]^=1;rejected([&]{peerchat::TlsCredentials mismatch(changed,factory);});
    changed=profile;changed.privateKey={1};rejected([&]{peerchat::TlsCredentials exported(changed,factory);});
    factory->fail=true;rejected([&]{peerchat::TlsSession failure(credentials,true,std::string(64,'a'));});factory->fail=false;
    for(int i=0;i<1000;++i){peerchat::TlsSession session(credentials,true,std::string(64,'a'));check(session.ready());
        session.receive({1,2,3});check(session.takeOutput()==peerchat::Blob({1,2,3}));check(session.takeOutput().empty());
        session.send({4,5});check(session.takeOutput()==peerchat::Blob({4,5}));check(session.remotePublicKey()==peerchat::Blob(72,2));
        if(i%2==0){session.shutdown();check(session.closed());session.shutdown();}
    }
    check(Provider::destroyed==1000 && Provider::shutdowns==1000);
    std::cout<<"PASS: native TLS provider delegation, identity/pin fail-closed, 1000 lifetimes and exact cleanup (contract fixture, not crypto proof).\n";
    return 0;
}catch(const std::exception& failure){std::cerr<<failure.what()<<'\n';return 1;}}
