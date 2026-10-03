#include "../storage.cpp"
#include "../delivery_manager.hpp"
#include <iostream>
namespace {
// Host control only: Android uses the existing non-exportable Keystore provider.
struct SigningControl final : peerchat::platform::IdentityProvider {
    peerchat::Profile key;bool fail=false;
    SigningControl(){using namespace peerchat::storage_detail;Algorithm algorithm;Key handle;
        crypto(BCryptOpenAlgorithmProvider(&algorithm.h,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0));
        crypto(BCryptGenerateKeyPair(algorithm.h,&handle.h,256,0));crypto(BCryptFinalizeKeyPair(handle.h,0));
        key.publicKey=exportKey(handle.h,BCRYPT_ECCPUBLIC_BLOB);key.privateKey=exportKey(handle.h,BCRYPT_ECCPRIVATE_BLOB);key.peerId=peerchat::Storage::publicKeyId(key.publicKey);}
    peerchat::Blob publicKey() override{if(fail)throw std::runtime_error("Identity unavailable");return key.publicKey;}
    peerchat::Blob sign(const peerchat::Blob& value) override{if(fail)throw std::runtime_error("Identity unavailable");return peerchat::signDevicePayload(value,key);}
};
template<class F> void mustReject(F call){bool failed=false;try{call();}catch(const std::exception&){failed=true;}peerchat::storage_detail::require(failed,"Invalid opaque profile accepted");}
}
int main(){using namespace peerchat;using namespace storage_detail;try{
    auto path=(std::filesystem::current_path()/L"build"/L"android-foundation"/L"profiles"/wide(Storage::newMessageId())).wstring();
    auto identity=std::make_shared<SigningControl>();auto records=std::make_shared<platform::WindowsRecordProtector>();
    auto configuration=[&]{auto platform=std::make_shared<platform::StoragePlatform>();platform->database=std::make_shared<WindowsSqlDatabase>(path);platform->records=records;platform->identity=identity;return platform;};
    const std::string remote(64,'b');StoredMessage message;std::string peer;
    {Storage store(configuration());auto profile=store.profile();peer=profile.peerId;require(profile.privateKey.empty()&&peer==identity->key.peerId,"Opaque profile copied/changed key");
        profile.name=L"Opaque profile";store.saveProfile(profile);message={Storage::newMessageId(),peer,remote,L"Opaque history",42,Delivery::Queued,0,1};require(store.append(remote,message),"Opaque history insert failed");
        auto invalid=profile;invalid.privateKey=identity->key.privateKey;mustReject([&]{store.saveProfile(invalid);});}
    for(int i=0;i<32;++i){Storage store(configuration());auto profile=store.profile();require(profile.peerId==peer&&profile.privateKey.empty()&&profile.name==L"Opaque profile","Opaque reopen changed profile");require(store.history(remote).size()==1&&!store.append(remote,message)&&store.pending(remote).size()==1,"Opaque restart lost history/outbox/dedupe");}
    {auto config=configuration();Storage store(config);identity->fail=true;mustReject([&]{store.profile();});identity->fail=false;require(store.profile().peerId==peer,"Provider failure rotated identity");}
    {auto config=configuration();config->identity=std::make_shared<SigningControl>();Storage store(config);mustReject([&]{store.profile();});}
    {auto config=configuration();config->identity.reset();mustReject([&]{Storage invalid(config);});}
    {Storage legacy(path);mustReject([&]{legacy.profile();});}
    {Storage store(configuration());delivery::DeliveryManager manager(store,peer);require(manager.authorizeTransport({7},remote)&&manager.submittedMessage(remote,message.id,{7})&&manager.acknowledgeMessage(remote,{7},message.id),"Opaque ACK failed");}
    {Storage store(configuration());require(store.pending(remote).empty()&&store.history(remote).size()==1,"Opaque ACK restart lost state");}
    {WindowsSqlDatabase db(path);auto query=db.prepare("SELECT b FROM objects WHERE k='profile'");require(query->row(),"Opaque profile record missing");Secret clear{records->unprotect(query->bytes(0),"PeerChat.v1/profile")};Reader reader(clear.data);require(reader.string()==peer,"Persisted peer mismatch");reader.text();reader.number();require(reader.bytes()==identity->key.publicKey&&reader.bytes().empty(),"Exported private key persisted");reader.end();}
    std::cout<<"PASS: opaque native profile create, 32 reopens, no private blob, identity/provider failure containment, protected persistence/history/outbox/ACK/dedupe\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
