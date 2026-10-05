#include "../storage.cpp"
#include "../platform/sql_key_registry.hpp"
#include "../platform/windows_recipient_key.hpp"
#include "../mailbox_key_coordinator.hpp"
#include <iostream>
using namespace peerchat;
static void check(bool ok){if(!ok)throw std::runtime_error("SQL key registry assertion failed");}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected);}
struct Signer : platform::IdentityProvider {
    Profile profile;explicit Signer(Profile p):profile(std::move(p)){}
    Blob publicKey()override{return profile.publicKey;}
    Blob sign(const Blob& bytes)override{return signDevicePayload(bytes,profile);}
};
struct TestRecipient {
    std::string alias="PeerChat.Recipient.test.registry."+std::to_string(GetCurrentProcessId())+"."+std::to_string(GetTickCount64());
    ~TestRecipient(){platform::recipient_cng::Object provider,key;
        if(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0)!=0)return;
        auto name=platform::recipient_cng::alias(alias);
        if(NCryptOpenKey(provider.value,&key.value,name.c_str(),0,0)==0&&NCryptDeleteKey(key.value,0)==0)key.value=0;
    }
};
int main(){try {
    auto root=std::filesystem::current_path()/L"build"/L"tests"/L"key-registry"/storage_detail::wide(Storage::newMessageId());
    std::filesystem::create_directories(root);
    DeviceKeyMetadata metadata;metadata.id="recipient-v1";metadata.providerAlias="test.registry.recipient-v1";
    metadata.algorithm=DeviceKeyManager::recipientSuite;metadata.generation=1;metadata.createdUtcMilliseconds=1;
    platform::WindowsRecordProtector protection;
    for(unsigned cycle=0;cycle<100;++cycle) {
        auto database=std::make_shared<storage_detail::WindowsSqlDatabase>(root.wstring());
        platform::SqlKeyRegistryStore store(database);platform::ProtectedKeyRegistry registry(store,protection,"synthetic-profile");
        std::vector<DeviceKeyMetadata> entries;
        registry.locked([&]{check(registry.read(entries)==(cycle!=0));if(!cycle)registry.replace({metadata});else check(entries.size()==1&&entries[0].providerAlias==metadata.providerAlias);});
        if(!cycle) {
            rejects([&]{registry.locked([&]{auto changed=metadata;changed.id="rolled-back";registry.replace({changed});throw std::runtime_error("Injected failure");});});
            registry.locked([&]{check(registry.read(entries)&&entries[0].id==metadata.id);});
        }
    }
    auto first=std::make_shared<storage_detail::WindowsSqlDatabase>(root.wstring());
    auto second=std::make_shared<storage_detail::WindowsSqlDatabase>(root.wstring());
    platform::SqlKeyRegistryStore a(first),b(second);
    a.locked([&]{rejects([&]{b.locked([]{});});});b.locked([]{});
    Blob protectedBytes;a.locked([&]{check(a.read(protectedBytes));});
    check(std::search(protectedBytes.begin(),protectedBytes.end(),metadata.providerAlias.begin(),metadata.providerAlias.end())==protectedBytes.end());
    auto modified=protectedBytes;modified.back()^=1;a.locked([&]{a.replace(modified);});
    platform::ProtectedKeyRegistry registry(a,protection,"synthetic-profile");std::vector<DeviceKeyMetadata> entries;
    rejects([&]{registry.locked([&]{registry.read(entries);});});
    a.locked([&]{a.replace(protectedBytes);});
    platform::ProtectedKeyRegistry wrongProfile(a,protection,"wrong-profile");rejects([&]{wrongProfile.locked([&]{wrongProfile.read(entries);});});
    auto production=root/L"production-path";TestRecipient recipient;Blob initialPublic;std::string keyId;
    for(unsigned cycle=0;cycle<100;++cycle) {
        Storage profileStore(production.wstring());Signer signer(profileStore.profile());
        auto database=std::make_shared<storage_detail::WindowsSqlDatabase>((production/L"device-key-registry").wstring());
        platform::SqlKeyRegistryStore store(database);platform::ProtectedKeyRegistry protectedRegistry(store,protection,signer.profile.peerId);
        platform::WindowsRecipientKeyProvider cng;DeviceKeyManager manager(signer,cng,protectedRegistry);
        mailbox::KeyCoordinator coordinator(manager,signer,verifyDevicePayload,Storage::publicKeyId);
        auto binding=coordinator.prepare(8,true,[&](std::uint32_t generation){check(generation==1);return recipient.alias;},Storage::newMessageId,100);
        if(!cycle){initialPublic=binding.publicKey;keyId=binding.keyId;}else check(binding.publicKey==initialPublic&&binding.keyId==keyId);
        coordinator.accepted(binding);
        check(manager.snapshot()[0].registration==DeviceKeyRegistration::Accepted);
        manager.withRecipient(keyId,false,[&](const auto& metadata,const auto& handle){check(metadata.publicKey==initialPublic&&handle->publicKey()==initialPublic);});
    }
    std::cout<<"PASS: SQL/DPAPI registry, 100 reopen cycles, transaction rollback, cross-connection serialization, protected metadata and fail-closed tamper/profile binding\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
