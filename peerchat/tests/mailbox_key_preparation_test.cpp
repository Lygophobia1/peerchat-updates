#include "../storage.cpp"
#include "../platform/sql_key_registry.hpp"
#include "../platform/windows_recipient_key.hpp"
#include "../mailbox_key_coordinator.hpp"
#include "../reliable_peer_protocol.hpp"
#include <iostream>
using namespace peerchat;
static void check(bool value){if(!value)throw std::runtime_error("Mailbox key preparation assertion failed");}
template<class F>static void rejects(F operation){bool failed=false;try{operation();}catch(const std::exception&){failed=true;}check(failed);}
struct Signer final : platform::IdentityProvider {
    Profile profile;
    explicit Signer(Profile value):profile(std::move(value)){}
    Blob publicKey()override{return profile.publicKey;}
    Blob sign(const Blob& value)override{return signDevicePayload(value,profile);}
};
struct Recipient final : platform::RecipientKeyProvider {
    platform::WindowsRecipientKeyProvider actual;
    unsigned creates=0,opens=0;
    bool supported()const override{return actual.supported();}
    bool exists(const std::string& alias)override{return actual.exists(alias);}
    std::shared_ptr<platform::RecipientKeyHandle> create(const std::string& alias)override{++creates;return actual.create(alias);}
    std::shared_ptr<platform::RecipientKeyHandle> open(const std::string& alias)override{++opens;return actual.open(alias);}
};
struct Alias final {
    std::string name="PeerChat.Recipient.test.preparation."+std::to_string(GetCurrentProcessId())+"."+Storage::newMessageId();
    ~Alias(){platform::recipient_cng::Object provider,key;
        if(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0)!=0)return;
        auto wide=platform::recipient_cng::alias(name);
        if(NCryptOpenKey(provider.value,&key.value,wide.c_str(),0,0)==0&&NCryptDeleteKey(key.value,0)==0)key.value=0;
    }
};
// Inject only the active-metadata write failure; the backing registry remains
// real Windows SQLite plus DPAPI and the recipient remains an opaque CNG key.
struct FailSecondReplace final : DeviceKeyRegistry {
    DeviceKeyRegistry& actual;unsigned writes=0;
    explicit FailSecondReplace(DeviceKeyRegistry& value):actual(value){}
    void locked(const std::function<void()>& operation)override{actual.locked(operation);}
    bool read(std::vector<DeviceKeyMetadata>& entries)override{return actual.read(entries);}
    void replace(const std::vector<DeviceKeyMetadata>& entries)override{
        if(++writes==2)throw std::runtime_error("Injected active metadata write failure");
        actual.replace(entries);
    }
};
int main(){try{
    using Stage=mailbox::KeyPreparationStage;
    auto root=std::filesystem::current_path()/L"build"/L"tests"/L"mailbox-key-preparation"/storage_detail::wide(Storage::newMessageId());
    std::filesystem::create_directories(root);
    Storage profileStore((root/L"profile").wstring());Signer signer(profileStore.profile());
    platform::WindowsRecordProtector protection;
    auto database=std::make_shared<storage_detail::WindowsSqlDatabase>((root/L"registry").wstring());
    platform::SqlKeyRegistryStore sql(database);
    platform::ProtectedKeyRegistry registry(sql,protection,signer.profile.peerId);
    Recipient recipient;Alias alias;Stage last=Stage::RegistrySnapshot;
    auto trace=[&](Stage stage){last=stage;};
    DeviceKeyManager manager(signer,recipient,registry);
    mailbox::KeyCoordinator coordinator(manager,signer,verifyDevicePayload,Storage::publicKeyId);
    auto makeAlias=[&](std::uint32_t generation){check(generation==1);return alias.name;};
    auto binding=coordinator.prepare(4,true,makeAlias,Storage::newMessageId,100,trace);
    check(last==Stage::BindingPrepared&&recipient.creates==1);
    coordinator.accepted(binding);
    // Existing device approval persists contacts in message Storage, not the
    // separate mailbox registry. Exact protected bytes must remain unchanged.
    Blob registryBefore;sql.locked([&]{check(sql.read(registryBefore));});
    Storage contactStore((root/L"contact").wstring());auto contactProfile=contactStore.profile();
    reliable::PeerProtocolState authenticated;authenticated.hello=true;
    authenticated.peerId=contactProfile.peerId;authenticated.name=L"Verified isolated contact";
    reliable::persistAuthenticatedContact(profileStore,authenticated,nullptr,contactProfile.publicKey);
    Blob registryAfter;sql.locked([&]{check(sql.read(registryAfter));});
    check(registryBefore==registryAfter);
    auto previousCreates=recipient.creates,previousOpens=recipient.opens;
    auto reopened=coordinator.prepare(4,true,makeAlias,Storage::newMessageId,100,trace);
    check(mailbox::serializeBinding(binding)==mailbox::serializeBinding(reopened));
    check(recipient.creates==previousCreates&&recipient.opens==previousOpens);
    rejects([&]{coordinator.prepare(5,true,makeAlias,Storage::newMessageId,100,trace);});
    check(last==Stage::BindingAccount&&recipient.creates==1);
    check(coordinator.bindingDiagnostic().find("binding_account=4")!=std::string::npos);
    check(coordinator.bindingDiagnostic().find("binding_account_matches=false")!=std::string::npos);
    check(coordinator.bindingDiagnostic().find("binding_device_matches=true")!=std::string::npos);
    check(manager.openRecipient(coordinator.inspectedKeyId())->publicKey()==binding.publicKey);
    check(mailbox::serializeBinding(coordinator.prepare(4,true,makeAlias,Storage::newMessageId,100,trace))==mailbox::serializeBinding(binding));
    coordinator.resetDiagnostic();check(coordinator.inspectedKeyId().empty());
    check(coordinator.bindingDiagnostic()=="binding_inspection=not_read");
    auto retained=manager.snapshot();
    auto invalid=[&](mailbox::RecipientBinding value,Stage expected){
        auto changed=retained;changed.front().signedBinding=mailbox::serializeBinding(value);
        registry.locked([&]{registry.replace(changed);});
        rejects([&]{coordinator.prepare(4,true,makeAlias,Storage::newMessageId,100,trace);});
        check(last==expected&&recipient.creates==1);
        check(manager.openRecipient(coordinator.inspectedKeyId())->publicKey()==binding.publicKey);
        registry.locked([&]{registry.replace(retained);});
    };
    auto altered=binding;altered.keyId=std::string(32,'e');invalid(altered,Stage::BindingKeyId);
    altered=binding;altered.publicKey.back()^=1;invalid(altered,Stage::BindingPublicKey);
    altered=binding;altered.generation++;invalid(altered,Stage::BindingGeneration);
    altered=binding;altered.accountId=5;invalid(altered,Stage::BindingAccount);
    check(coordinator.bindingDiagnostic().find("binding_signature_valid=false")!=std::string::npos);
    altered=binding;altered.accountId=5;altered.signature=signer.sign(mailbox::bindingPayload(altered));
    invalid(altered,Stage::BindingAccount);
    check(coordinator.bindingDiagnostic().find("binding_signature_valid=true")!=std::string::npos);
    altered=binding;altered.device=std::string(64,'e');invalid(altered,Stage::BindingDevice);
    altered=binding;altered.signature.back()^=1;invalid(altered,Stage::BindingSignature);
    auto malformed=retained;malformed.front().signedBinding.front()^=1;
    registry.locked([&]{registry.replace(malformed);});
    rejects([&]{coordinator.prepare(4,true,makeAlias,Storage::newMessageId,100,trace);});
    check(last==Stage::BindingParse&&recipient.creates==1);
    registry.locked([&]{registry.replace(retained);});
    check(mailbox::serializeBinding(coordinator.prepare(4,true,makeAlias,Storage::newMessageId,100,trace))==mailbox::serializeBinding(binding));
    Blob saved;sql.locked([&]{check(sql.read(saved));auto corrupt=saved;corrupt.back()^=1;sql.replace(corrupt);});
    rejects([&]{coordinator.prepare(4,true,makeAlias,Storage::newMessageId,100,trace);});
    check(last==Stage::RegistrySnapshot&&recipient.creates==1);
    sql.locked([&]{sql.replace(saved);});

    auto failedDatabase=std::make_shared<storage_detail::WindowsSqlDatabase>((root/L"failed-registry").wstring());
    platform::SqlKeyRegistryStore failedSql(failedDatabase);
    platform::ProtectedKeyRegistry failedProtected(failedSql,protection,signer.profile.peerId);
    FailSecondReplace injected(failedProtected);DeviceKeyManager failedManager(signer,recipient,injected);
    mailbox::KeyCoordinator failedCoordinator(failedManager,signer,verifyDevicePayload,Storage::publicKeyId);
    Alias failedAlias;auto failedName=[&](std::uint32_t){return failedAlias.name;};
    rejects([&]{failedCoordinator.prepare(4,true,failedName,Storage::newMessageId,100,trace);});
    check(last==Stage::RecipientProvision&&recipient.exists(failedAlias.name));
    check(failedManager.snapshot().empty()); // SQL rollback removed provisioning intent.
    auto created=recipient.creates;
    rejects([&]{failedCoordinator.prepare(4,true,failedName,Storage::newMessageId,100,trace);});
    check(last==Stage::RecipientProvision&&recipient.creates==created);
    check(failedManager.snapshot().empty()&&recipient.exists(failedAlias.name));
    std::cout<<"PASS: real SQLite/DPAPI/CNG registration; existing binding avoids recipient calls; wrong account and corrupt registry retain exact stages; active-write failure reproduces rolled-back intent with preserved orphan key, retry fails closed without key replacement\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
