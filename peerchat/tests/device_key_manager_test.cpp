#include "../device_key_manager.hpp"
#include <iostream>
#include <map>
#include <future>

using namespace peerchat;
namespace {
void check(bool value) { if (!value) throw std::runtime_error("Test assertion failed"); }
template<class F> void rejects(F f) { bool rejected=false; try { f(); } catch(const std::exception&) { rejected=true; } check(rejected); }
struct Identity : platform::IdentityProvider {
    unsigned calls=0;
    std::vector<unsigned char> publicKey() override { return {1,2,3}; }
    std::vector<unsigned char> sign(const std::vector<unsigned char>& original) override { ++calls; return original; }
};
struct Handle : platform::RecipientKeyHandle {
    std::vector<unsigned char> point=std::vector<unsigned char>(65,7);
    Handle() { point[0]=4; }
    std::vector<unsigned char> publicKey() override { return point; }
    std::vector<unsigned char> agreeP256(const std::vector<unsigned char>&) override { throw std::runtime_error("Not a crypto test provider"); }
};
struct Provider : platform::RecipientKeyProvider {
    bool enabled=true;
    unsigned creations=0;
    std::map<std::string,std::shared_ptr<Handle>> handles;
    bool supported() const override { return enabled; }
    bool exists(const std::string& alias) override { return handles.count(alias)!=0; }
    std::shared_ptr<platform::RecipientKeyHandle> create(const std::string& alias) override {
        check(!exists(alias)); ++creations; return handles.emplace(alias,std::make_shared<Handle>()).first->second;
    }
    std::shared_ptr<platform::RecipientKeyHandle> open(const std::string& alias) override {
        check(exists(alias)); return handles.at(alias);
    }
};
struct Registry : DeviceKeyRegistry {
    bool present=false, corrupt=false;
    unsigned writes=0, failWrite=0;
    std::vector<DeviceKeyMetadata> data;
    void locked(const std::function<void()>& operation) override { operation(); }
    bool read(std::vector<DeviceKeyMetadata>& result) override { check(!corrupt); result=data; return present; }
    void replace(const std::vector<DeviceKeyMetadata>& value) override {
        ++writes; check(writes!=failWrite); data=value; present=true;
    }
};
struct LockedRegistry : Registry {
    std::mutex gate;
    void locked(const std::function<void()>& f)override{std::lock_guard<std::mutex> lock(gate);f();}
};
}
int main() { try {
    Identity identity; Provider provider; Registry registry;
    DeviceKeyManager manager(identity,provider,registry);
    check(manager.identityPublicKey()==identity.publicKey());
    check(manager.signIdentity({9,0,8})==std::vector<unsigned char>({9,0,8}));
    check(identity.calls==1);
    rejects([&]{ manager.openRecipient("recipient-v1"); });
    auto descriptor=manager.provisionRecipient("recipient-v1","profile.recipient.v1",1,123);
    check(descriptor.state==DeviceKeyState::Active && descriptor.registration==DeviceKeyRegistration::NotRegistered);
    for(unsigned i=0;i<1000;++i) {
        auto reopened=manager.provisionRecipient(descriptor.id,descriptor.providerAlias,1,999);
        check(reopened.createdUtcMilliseconds==123 && manager.openRecipient(descriptor.id)->publicKey()==descriptor.publicKey);
    }
    check(provider.creations==1 && identity.calls==1);
    rejects([&]{manager.provisionRecipient("recipient-v2","profile.recipient.v2",2,124);});
    registry.corrupt=true; rejects([&]{manager.openRecipient(descriptor.id);}); registry.corrupt=false;
    auto original=provider.handles.at(descriptor.providerAlias)->point;
    provider.handles.at(descriptor.providerAlias)->point[1]^=1;
    rejects([&]{manager.openRecipient(descriptor.id);});
    provider.handles.at(descriptor.providerAlias)->point=original;
    registry.data[0].state=DeviceKeyState::Revoked;
    rejects([&]{manager.openRecipient(descriptor.id);});
    registry.data[0].state=DeviceKeyState::Retired; check(bool(manager.openRecipient(descriptor.id)));
    registry.data[0].state=DeviceKeyState::Active;
    unsigned operations=0;
    manager.withRecipient(descriptor.id,false,[&](const auto& metadata,const auto& handle){check(metadata.id==descriptor.id&&handle->publicKey()==descriptor.publicKey);++operations;});
    check(operations==1);
    registry.data[0].state=DeviceKeyState::Retired;
    rejects([&]{manager.withRecipient(descriptor.id,false,[](const auto&,const auto&){});});
    manager.withRecipient(descriptor.id,true,[&](const auto&,const auto&){++operations;});check(operations==2);
    manager.revokeRecipient(descriptor.id);manager.revokeRecipient(descriptor.id);
    check(registry.data[0].state==DeviceKeyState::Revoked);
    rejects([&]{manager.withRecipient(descriptor.id,true,[](const auto&,const auto&){});});
    registry.data[0].state=DeviceKeyState::Active;
    provider.handles.clear(); rejects([&]{manager.provisionRecipient(descriptor.id,descriptor.providerAlias,1,123);});
    check(provider.creations==1); // Missing initialized key must never be replaced.

    Provider interrupted; Registry intent; intent.failWrite=2;
    DeviceKeyManager interruptedManager(identity,interrupted,intent);
    rejects([&]{interruptedManager.provisionRecipient("r1","p.recipient.v1",1,1);});
    check(interrupted.creations==1 && intent.data[0].state==DeviceKeyState::Provisioning);
    intent.failWrite=0;
    check(interruptedManager.provisionRecipient("r1","p.recipient.v1",1,1).state==DeviceKeyState::Active);
    check(interrupted.creations==1);
    Registry lost; DeviceKeyManager lostManager(identity,interrupted,lost);
    rejects([&]{lostManager.provisionRecipient("r1","p.recipient.v1",1,1);});
    Provider unavailable; unavailable.enabled=false; Registry clean;
    DeviceKeyManager unsupported(identity,unavailable,clean);
    rejects([&]{unsupported.provisionRecipient("r1","p.recipient.v1",1,1);}); check(unavailable.creations==0);
    rejects([&]{manager.signIdentity(std::vector<unsigned char>(65537));});
    Provider rotating;Registry rotatingRegistry;DeviceKeyManager rotation(identity,rotating,rotatingRegistry);
    auto previous=rotation.provisionRecipient("rotate-v1","rotate.alias.v1",1,1);
    rejects([&]{rotation.recordRegistration(previous.id,{1},true);});
    rotation.recordRegistration(previous.id,{1},false);rotation.recordRegistration(previous.id,{1},true);
    rejects([&]{rotation.recordRegistration(previous.id,{2},false);});
    auto next=rotation.rotateRecipient(previous.id,"rotate-v2","rotate.alias.v2",2,2);
    check(next.predecessorId==previous.id&&rotation.snapshot()[0].state==DeviceKeyState::Retired&&rotating.creations==2);
    check(rotation.rotateRecipient(previous.id,next.id,next.providerAlias,2,2).publicKey==next.publicKey&&rotating.creations==2);
    rejects([&]{rotation.withRecipient(previous.id,false,[](const auto&,const auto&){});});
    rotation.withRecipient(previous.id,true,[](const auto&,const auto&){});
    rejects([&]{rotation.rotateRecipient(previous.id,"rotate-v3","rotate.alias.v3",3,3);});
    rotatingRegistry.failWrite=rotatingRegistry.writes+2;
    rejects([&]{rotation.rotateRecipient(next.id,"rotate-v3","rotate.alias.v3",3,3);});
    check(rotating.creations==3);rotatingRegistry.failWrite=0;
    check(rotation.rotateRecipient(next.id,"rotate-v3","rotate.alias.v3",3,3).generation==3&&rotating.creations==3);
    Provider concurrentProvider;LockedRegistry concurrentRegistry;
    DeviceKeyManager firstManager(identity,concurrentProvider,concurrentRegistry),secondManager(identity,concurrentProvider,concurrentRegistry);
    auto concurrentKey=firstManager.provisionRecipient("lease-key","lease.recipient.v1",1,1);
    std::promise<void> entered,release;auto released=release.get_future().share();auto began=entered.get_future();
    auto operation=std::async(std::launch::async,[&]{firstManager.withRecipient(concurrentKey.id,false,[&](const auto&,const auto&){entered.set_value();released.wait();});});
    check(began.wait_for(std::chrono::seconds(10))==std::future_status::ready);
    auto revocation=std::async(std::launch::async,[&]{secondManager.revokeRecipient(concurrentKey.id);});
    check(revocation.wait_for(std::chrono::milliseconds(100))==std::future_status::timeout);
    release.set_value();operation.get();revocation.get();
    rejects([&]{firstManager.withRecipient(concurrentKey.id,true,[](const auto&,const auto&){});});
    rejects([&]{secondManager.openRecipient(concurrentKey.id);});
    std::cout << "PASS: opaque roles, 1000 reopen cycles, provisioning interruption, registry loss/corruption, missing/revoked keys, capabilities and identity preservation\n";
    return 0;
} catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; } }
