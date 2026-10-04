#include "../platform/protected_key_registry.hpp"
#include "../platform/windows_record_protection.hpp"
#include <iostream>
using namespace peerchat;
namespace {
void check(bool value) { if(!value) throw std::runtime_error("Registry assertion failed"); }
template<class F> void rejects(F f) { bool rejected=false; try { f(); } catch(const std::exception&) { rejected=true; } check(rejected); }
struct Store : platform::KeyRegistryStore {
    bool present=false, unavailable=false;
    Blob bytes;
    void locked(const std::function<void()>& f) override { f(); }
    bool read(Blob& output) override { check(!unavailable); output=bytes; return present; }
    void replace(const Blob& input) override { check(!unavailable); bytes=input; present=true; }
};
}
int main() { try {
    Store store; platform::WindowsRecordProtector protection;
    platform::ProtectedKeyRegistry registry(store,protection,"test-profile-a");
    DeviceKeyMetadata metadata; metadata.id="recipient-v1"; metadata.providerAlias="test.recipient.v1";
    metadata.algorithm=DeviceKeyManager::recipientSuite; metadata.generation=1; metadata.createdUtcMilliseconds=123;
    std::vector<DeviceKeyMetadata> entries;
    check(!registry.read(entries)); registry.replace({metadata});
    const auto valid=store.bytes;
    for(unsigned i=0;i<1000;++i) { check(registry.read(entries)); check(entries.size()==1 && entries[0].id==metadata.id); }
    check(std::search(valid.begin(),valid.end(),metadata.providerAlias.begin(),metadata.providerAlias.end())==valid.end());
    store.bytes.back()^=1; rejects([&]{registry.read(entries);}); store.bytes=valid;
    store.bytes.resize(valid.size()/2); rejects([&]{registry.read(entries);}); store.bytes={}; rejects([&]{registry.read(entries);});
    store.bytes=valid;
    platform::ProtectedKeyRegistry otherProfile(store,protection,"test-profile-b"); rejects([&]{otherProfile.read(entries);});
    store.unavailable=true; rejects([&]{registry.read(entries);}); rejects([&]{registry.replace({metadata});}); store.unavailable=false;
    auto bytes=platform::ProtectedKeyRegistry::encode({metadata});
    for(std::size_t i=0;i<bytes.size();++i) rejects([&]{platform::ProtectedKeyRegistry::decode(Blob(bytes.begin(),bytes.begin()+i));});
    auto bad=bytes; bad[3]=2; rejects([&]{platform::ProtectedKeyRegistry::decode(bad);});
    bad=bytes; bad.push_back(0); rejects([&]{platform::ProtectedKeyRegistry::decode(bad);});
    rejects([&]{platform::ProtectedKeyRegistry::encode({metadata,metadata});});
    metadata.state=DeviceKeyState::Active; metadata.publicKey=Blob(65,9); metadata.publicKey[0]=4;
    registry.replace({metadata}); check(registry.read(entries) && entries[0].publicKey==metadata.publicKey);
    metadata.registration=DeviceKeyRegistration::Accepted;
    rejects([&]{registry.replace({metadata});});
    std::cout << "PASS: DPAPI registry, 1000 authenticated reads, profile binding, tamper/truncation, missing backend, schema bounds and no secret fields\n";
    return 0;
} catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; } }
