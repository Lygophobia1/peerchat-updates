#include "../device_identity_application.hpp"
#include "../platform/windows_sql_database.hpp"
#include "../platform/windows_record_protection.hpp"
#include <filesystem>
#include <iostream>
using namespace peerchat;using namespace peerchat::device_identity;
namespace {
void check(bool b){if(!b)throw std::runtime_error("Identity application assertion failed");}
template<class F>void reject(F f){bool bad=false;try{f();}catch(const std::exception&){bad=true;}check(bad);}
struct Sign:platform::IdentityProvider{Profile p;explicit Sign(Profile x):p(std::move(x)){}Bytes publicKey()override{return p.publicKey;}Bytes sign(const Bytes& b)override{return signDevicePayload(b,p);}};
const std::string pin(64,'a');constexpr std::uint64_t wall=1000,steady=5000;
Bytes command(unsigned op,std::uint64_t account,const std::function<void(protocol::Writer&)>& fields={}){
    protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/IdentityDirectoryCommand/v1");w.number(op);w.number(account);w.number(7);w.string(pin);w.number(wall);if(fields)fields(w);return w.data;
}
Bytes snapshot(std::uint64_t local,std::uint64_t remote,std::uint64_t revision,const Bytes& root,const std::vector<Bytes>& proofs,const std::string& confirmed="",bool active=true){
    return command(3,local,[&](auto& w){w.number(remote);w.number(revision);w.bytes(root);w.number(active?1:0);w.number(proofs.size());for(auto& proof:proofs){w.bytes(proof);w.number(1);}w.string(confirmed);});
}
}
int main(int argc,char** argv){try{
    check(argc==2);auto dir=std::filesystem::path(argv[1]);Storage ownStore((dir/L"root").wstring()),peerStore((dir/L"peer").wstring()),targetStore((dir/L"target").wstring());
    Sign root(ownStore.profile()),peer(peerStore.profile()),target(targetStore.profile());platform::WindowsRecordProtector records;
    auto ownDb=platform::openWindowsSqlDatabase((dir/L"root-directory").wstring()),peerDb=platform::openWindowsSqlDatabase((dir/L"peer-directory").wstring());
    Application own(root,*ownDb,records,root.p.peerId),other(peer,*peerDb,records,peer.p.peerId);
    auto mapping=[&](const std::string& id)->std::uint64_t{return id==root.p.peerId||id==target.p.peerId?14:0;};
    auto run=[&](Application& a,const Bytes& bytes,std::uint64_t local){return a.execute(bytes,local,7,wall,steady,{},mapping);};
    auto possessionWire=run(own,command(0,14),14);
    AuthoritySelection claim;claim.identity=possession(possessionWire,verifyDevicePayload,Storage::publicKeyId);claim.generation=1;claim.revision=1;claim.issued=wall;claim.expires=wall+60000;claim.issuance=std::string(64,'b');claim.session=std::string(64,'c');
    auto rootCommand=command(1,14,[&](auto& w){w.bytes(payload(claim));w.string(root.p.peerId);});
    auto rootWire=run(own,rootCommand,14);check(authority(rootWire,verifyDevicePayload,Storage::publicKeyId).identity.key==root.p.publicKey);
    reject([&]{run(own,rootCommand,14);});
    run(own,snapshot(14,14,2,rootWire,{},root.p.peerId),14);
    Possession p{pin,target.p.peerId,14,target.p.publicKey,{}};p.signature=target.sign(possessionPayload(p));
    Endorsement e;e.target=p;e.authority=root.p.peerId;e.authorityKey=root.p.publicKey;e.authorityGeneration=1;e.targetGeneration=1;e.revision=2;e.issued=wall;e.expires=wall+60000;e.issuance=std::string(64,'d');e.session=std::string(64,'e');
    auto endorseCommand=command(2,14,[&](auto& w){w.bytes(payload(e));w.string(target.p.peerId);});
    auto endorsed=run(own,endorseCommand,14);check(endorsement(endorsed,verifyDevicePayload,Storage::publicKeyId).target.key==target.p.publicKey);
    reject([&]{run(other,snapshot(13,14,3,rootWire,{endorsed}),13);});
    run(other,snapshot(13,14,3,rootWire,{endorsed},root.p.peerId),13);
    check(other.resolve(13,7,14,target.p.peerId,wall,steady)==target.p.publicKey);
    reject([&]{other.resolve(13,8,14,target.p.peerId,wall,steady);});
    reject([&]{other.resolve(13,7,14,target.p.peerId,wall,steady+60000);});
    // Reopen requires a fresh directory; prior consent/proof is durable but not a live lease.
    Application reopened(peer,*peerDb,records,peer.p.peerId);
    reject([&]{reopened.resolve(13,7,14,target.p.peerId,wall,steady);});
    run(reopened,snapshot(13,14,3,rootWire,{endorsed}),13);
    check(reopened.resolve(13,7,14,target.p.peerId,wall,steady)==target.p.publicKey);
    auto corruptedRoot=rootWire;corruptedRoot.back()^=1;
    reject([&]{run(reopened,snapshot(13,14,4,corruptedRoot,{endorsed}),13);});
    reject([&]{reopened.resolve(13,7,14,target.p.peerId,wall,steady);});
    run(reopened,snapshot(13,14,3,rootWire,{endorsed}),13);
    auto corrupted=endorsed;corrupted.back()^=1;
    reject([&]{run(reopened,snapshot(13,14,4,rootWire,{corrupted}),13);});
    reject([&]{reopened.resolve(13,7,14,target.p.peerId,wall,steady);});
    run(reopened,snapshot(13,14,4,rootWire,{},"",false),13);
    run(reopened,snapshot(13,14,4,rootWire,{},"",false),13);
    reject([&]{run(reopened,snapshot(13,14,5,rootWire,{endorsed},root.p.peerId),13);});
    check(ownStore.contacts().empty()&&peerStore.contacts().empty()&&targetStore.contacts().empty());
    std::cout<<"PASS native typed application: own authority selection, exact scoped endorsement, explicit contact authority, offline target key resolution, separate protected SQL restart, fresh lease requirement, invalidation and permanent revocation. No existing contact mutations.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
