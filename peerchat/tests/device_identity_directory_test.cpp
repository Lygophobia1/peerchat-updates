#include "../device_identity_directory.hpp"
#include "../platform/windows_record_protection.hpp"
#include "../group.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <thread>
#include <atomic>
using namespace peerchat;
using namespace peerchat::device_identity;
namespace {
void check(bool value){if(!value)throw std::runtime_error("Identity directory assertion failed");}
template<class F> void rejects(F action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed);}
struct Signing:platform::IdentityProvider {
    Profile p;explicit Signing(Profile value):p(std::move(value)){}
    Blob publicKey()override{return p.publicKey;}
    Blob sign(const Blob& bytes)override{return signDevicePayload(bytes,p);}
};
struct Store:platform::KeyRegistryStore {
    Blob bytes;bool fail=false;
    void locked(const std::function<void()>& f)override{f();}
    bool read(Blob& out)override{out=bytes;return !bytes.empty();}
    void replace(const Blob& value)override{if(fail)throw std::runtime_error("injected atomic replace failure");bytes=value;}
};
}
int main(int argc,char** argv){try{
    check(argc==2);auto path=std::filesystem::path(argv[1]);
    Storage rootStore((path/L"root").wstring()),targetStore((path/L"target").wstring()),otherStore((path/L"other").wstring());
    Signing root(rootStore.profile()),target(targetStore.profile()),other(otherStore.profile());
    auto verify=verifyDevicePayload;auto fingerprint=Storage::publicKeyId;
    Possession p{std::string(64,'a'),target.p.peerId,14,target.publicKey(),{}};
    p.signature=target.sign(possessionPayload(p));check(possession(encode(p),verify,fingerprint).key==target.publicKey());
    Endorsement e;e.target=p;e.authority=root.p.peerId;e.authorityKey=root.publicKey();
    e.authorityGeneration=1;e.targetGeneration=1;e.revision=1;e.issued=1000;e.expires=61000;
    e.issuance=std::string(64,'b');e.session=std::string(64,'c');
    Bytes accepted;
    Scope s;s.server=p.server;s.account=14;s.authority=e.authority;s.authorityKey=e.authorityKey;
    s.authorityGeneration=1;s.revision=1;s.localGeneration=7;s.session=e.session;s.validUntil=200000;s.authorityActive=true;
    bool active=true;
    s.targetCurrent=[&](const std::string& device,std::uint64_t gen,const Bytes& wire){return active&&device==p.device&&gen==1&&(wire.empty()||wire==accepted);};
    Approval a{s.server,s.authority,p.device,e.issuance,p.key,14,1,1,1,7,e.session,e.issued,e.expires};
    ScopedSigner signer(root,verify,fingerprint);
    auto bad=a;bad.target=other.p.peerId;rejects([&]{signer.sign(e,bad,s,7,1001);});
    bad=a;bad.localGeneration=6;rejects([&]{signer.sign(e,bad,s,7,1001);});
    rejects([&]{signer.sign(e,a,s,6,1001);});rejects([&]{signer.sign(e,a,s,7,61000);});
    auto changed=e;changed.target.signature[0]^=1;rejects([&]{signer.sign(changed,a,s,7,1001);});
    changed=e;changed.session=std::string(64,'d');rejects([&]{signer.sign(changed,a,s,7,1001);});
    active=false;rejects([&]{signer.sign(e,a,s,7,1001);});active=true;
    accepted=signer.sign(e,a,s,7,1001);rejects([&]{signer.sign(e,a,s,7,1001);});
    {std::ofstream file(path/"endorsement.bin",std::ios::binary);file.write(reinterpret_cast<const char*>(accepted.data()),accepted.size());check(bool(file));}
    check(endorsement(accepted,verify,fingerprint).target.key==p.key);
    for(size_t i=0;i<accepted.size();++i)rejects([&]{endorsement(Bytes(accepted.begin(),accepted.begin()+i),verify,fingerprint);});
    auto tampered=accepted;tampered.back()^=1;rejects([&]{endorsement(tampered,verify,fingerprint);});
    Store store;platform::WindowsRecordProtector protection;
    Directory directory(store,protection,"fixture",p.server,14,verify,fingerprint);
    rejects([&]{directory.reconcile(s,{accepted},7,1002);});
    rejects([&]{directory.bootstrap(s,other.p.peerId,7,1002);});
    directory.bootstrap(s,root.p.peerId,7,1002);directory.reconcile(s,{accepted},7,1002);
    check(directory.resolve(s,p.device,7,1002)==p.key);
    auto durable=store.bytes;
    check(std::search(durable.begin(),durable.end(),accepted.begin(),accepted.end())==durable.end());
    // Reopen of the protected record and expired enrollment challenge do not
    // expire a durable endorsement: fresh current directory authority is required.
    Directory reopened(store,protection,"fixture",p.server,14,verify,fingerprint);
    check(reopened.resolve(s,p.device,7,70000)==p.key);
    rejects([&]{reopened.resolve(s,p.device,7,200000);});
    auto wrong=s;wrong.account=15;rejects([&]{reopened.resolve(wrong,p.device,7,1002);});
    wrong=s;wrong.server=std::string(64,'e');rejects([&]{reopened.resolve(wrong,p.device,7,1002);});
    wrong=s;wrong.authority=other.p.peerId;wrong.authorityKey=other.publicKey();
    rejects([&]{reopened.bootstrap(wrong,other.p.peerId,7,1002);});
    wrong=s;wrong.authorityActive=false;rejects([&]{reopened.resolve(wrong,p.device,7,1002);});
    active=false;rejects([&]{reopened.resolve(s,p.device,7,1002);});
    // Revoked/omitted targets retain watermarks; no implicit re-enrollment.
    s.revision=2;reopened.reconcile(s,{},7,1002);
    rejects([&]{reopened.resolve(s,p.device,7,1002);});active=true;
    wrong=s;wrong.revision=1;rejects([&]{reopened.resolve(wrong,p.device,7,1002);});
    rejects([&]{reopened.reconcile(wrong,{accepted},7,1002);});
    rejects([&]{reopened.resolve(s,p.device,7,1002);});
    s.revision=3;rejects([&]{reopened.reconcile(s,{accepted},7,1002);});
    auto beforeFailure=store.bytes;store.fail=true;
    rejects([&]{reopened.reconcile(s,{},7,1002);});check(store.bytes==beforeFailure);store.fail=false;
    s.revision=2;rejects([&]{reopened.resolve(s,p.device,7,1002);});
    Directory wrongProfile(store,protection,"other-profile",p.server,14,verify,fingerprint);
    rejects([&]{wrongProfile.resolve(s,p.device,7,1002);});
    store.bytes.back()^=1;rejects([&]{reopened.resolve(s,p.device,7,1002);});store.bytes=beforeFailure;
    s.revision=3;s.authorityActive=false;reopened.revokeAuthority(s,7,1002);
    s.authorityActive=true;rejects([&]{reopened.bootstrap(s,root.p.peerId,7,1002);});
    rejects([&]{reopened.resolve(s,root.p.peerId,7,1002);});
    // One-hop only: a target possessing a valid endorsement cannot sign as root.
    ScopedSigner member(target,verify,fingerprint);e.issuance=std::string(64,'f');a.issuance=e.issuance;
    e.revision=s.revision;a.revision=s.revision;
    rejects([&]{member.sign(e,a,s,7,1002);});
    // An offline sibling can be verified from the authority chain without a
    // live peer route. No online flag participates in identity admission.
    Possession sibling{p.server,other.p.peerId,14,other.publicKey(),{}};
    sibling.signature=other.sign(possessionPayload(sibling));
    Endorsement second=e;second.target=sibling;second.revision=1;second.issuance=std::string(64,'e');
    Scope multi=s;multi.revision=1;Bytes secondWire;
    multi.targetCurrent=[&](const std::string& device,std::uint64_t gen,const Bytes& wire){return gen==1&&
        ((device==p.device&&(wire.empty()||wire==accepted))||(device==sibling.device&&(wire.empty()||wire==secondWire)));};
    Approval secondApproval{multi.server,multi.authority,sibling.device,second.issuance,sibling.key,14,1,1,1,7,second.session,second.issued,second.expires};
    secondWire=signer.sign(second,secondApproval,multi,7,1002);
    Store multiStore;Directory multiple(multiStore,protection,"multi",p.server,14,verify,fingerprint);
    multiple.bootstrap(multi,root.p.peerId,7,1002);multiple.reconcile(multi,{accepted,secondWire},7,1002);
    check(multiple.resolve(multi,p.device,7,1002)==p.key&&multiple.resolve(multi,sibling.device,7,1002)==sibling.key);
    rejects([&]{multiple.reconcile(multi,{accepted,accepted},7,1002);});
    rejects([&]{multiple.reconcile(multi,{secondWire},7,1002);});
    Directory multiReopen(multiStore,protection,"multi",p.server,14,verify,fingerprint);
    check(multiReopen.resolve(multi,sibling.device,7,70000)==sibling.key);
    Store emptyStore;Directory emptyDirectory(emptyStore,protection,"empty",p.server,14,verify,fingerprint);
    emptyDirectory.bootstrap(multi,root.p.peerId,7,1002);emptyDirectory.reconcile(multi,{},7,1002);
    rejects([&]{emptyDirectory.reconcile(multi,{accepted},7,1002);});
    second.issuance=std::string(64,'9');secondApproval.issuance=second.issuance;
    std::atomic<unsigned> signedCount{0};std::vector<std::thread> workers;
    for(unsigned i=0;i<8;++i)workers.emplace_back([&]{try{signer.sign(second,secondApproval,multi,7,1002);++signedCount;}catch(const std::exception&){}});
    for(auto& worker:workers)worker.join();
    check(signedCount==1);
    std::cout<<"PASS: real identity signatures; scoped approval/replay/expiry; explicit root bootstrap; no-chain denial; account/server/profile binding; revocation, revision rollback, protected reopen and atomic-write failure. Foundation only; no live enrollment or cached delivery claim.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
