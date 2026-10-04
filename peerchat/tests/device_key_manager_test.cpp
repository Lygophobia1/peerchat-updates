#include "../device_key_manager.hpp"
#include <iostream>
#include <map>

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
    std::cout << "PASS: opaque roles, 1000 reopen cycles, provisioning interruption, registry loss/corruption, missing/revoked keys, capabilities and identity preservation\n";
    return 0;
} catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; } }
