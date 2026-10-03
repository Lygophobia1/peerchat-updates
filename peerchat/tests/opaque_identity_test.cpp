#include "../storage.cpp"
#include "../platform/identity_provider.hpp"
#include <iostream>

namespace {
class TestIdentity final : public peerchat::platform::IdentityProvider {
    peerchat::Profile backing_;
public:
    bool fail=false,wrongSignature=false;
    explicit TestIdentity(peerchat::Profile backing):backing_(std::move(backing)){}
    peerchat::Blob publicKey() override{return backing_.publicKey;}
    peerchat::Blob sign(const peerchat::Blob& payload) override{
        if(fail)throw std::runtime_error("Test provider unavailable");
        auto result=peerchat::signDevicePayload(payload,backing_);
        if(wrongSignature)result[0]^=1;
        return result;
    }
};
template<class F> void rejects(F operation){
    bool rejected=false;try{operation();}catch(const std::exception&){rejected=true;}
    peerchat::storage_detail::require(rejected,"Invalid provider operation accepted");
}
}
int main(){using namespace peerchat;try{
    using namespace storage_detail;
    Algorithm algorithm;Key key;
    crypto(BCryptOpenAlgorithmProvider(&algorithm.h,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0));
    crypto(BCryptGenerateKeyPair(algorithm.h,&key.h,256,0));crypto(BCryptFinalizeKeyPair(key.h,0));
    Profile legacy;legacy.publicKey=exportKey(key.h,BCRYPT_ECCPUBLIC_BLOB);
    legacy.privateKey=exportKey(key.h,BCRYPT_ECCPRIVATE_BLOB);legacy.peerId=Storage::publicKeyId(legacy.publicKey);
    validateProfile(legacy);
    auto provider=std::make_shared<TestIdentity>(legacy);
    Profile opaque=legacy;opaque.privateKey.clear();opaque.identity=provider;
    const Blob payload{0,1,2,0,255};
    for(int i=0;i<1000;++i){validateProfile(opaque);auto signature=signDevicePayload(payload,opaque);
        require(signature.size()==64&&verifyDevicePayload(payload,signature,legacy.publicKey),"Provider signature rejected");}
    auto invalid=opaque;invalid.privateKey=legacy.privateKey;rejects([&]{validateProfile(invalid);});
    rejects([&]{signDevicePayload(payload,invalid);});
    invalid=opaque;invalid.publicKey[12]^=1;rejects([&]{validateProfile(invalid);});
    rejects([&]{signDevicePayload(Blob(70001),opaque);});
    provider->fail=true;rejects([&]{signDevicePayload(payload,opaque);});provider->fail=false;
    provider->wrongSignature=true;rejects([&]{validateProfile(opaque);});rejects([&]{signDevicePayload(payload,opaque);});
    provider->wrongSignature=false;
    GroupEvent event;event.actorPeerId=opaque.peerId;event.actorAccountId=1;
    event.groupId=std::string(32,'a');event.logicalOrder=1;event.value=L"Provider proof";
    signGroupEvent(event,opaque);require(verifyGroupEventSignature(event,opaque.publicKey),"Group provider signature rejected");
    require(opaque.privateKey.empty(),"Opaque identity acquired private material");
    std::cout<<"PASS: 1000 opaque binding/signature cycles; legacy CNG, bounds, mismatch, provider failure and group signatures\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
