#include "../storage.cpp"
#include "../archive_authorization.hpp"
#include "../platform/identity_provider.hpp"
#include <iostream>
#include <set>
using namespace peerchat;
namespace ar=peerchat::archive;
static void check(bool value){if(!value)throw std::runtime_error("Archive security regression failed");}
template<class F>static void rejects(F operation){bool failed=false;try{operation();}catch(const std::exception&){failed=true;}check(failed);}
// Host-only control for the same opaque signing interface used by Keystore.
// Production Authorization sees no private blob and calls existing signing only.
struct TestSigner final:platform::IdentityProvider {
    Profile backing;bool fail=false;
    explicit TestSigner(Profile profile):backing(std::move(profile)){}
    Blob publicKey()override{return backing.publicKey;}
    Blob sign(const Blob& bytes)override{if(fail)throw std::runtime_error("Test signer unavailable");return signDevicePayload(bytes,backing);}
};
int main(){try{
    auto directory=std::filesystem::current_path()/L"build"/L"tests"/L"archive-security"/storage_detail::wide(Storage::newMessageId());
    Storage a((directory/L"a").wstring()),b((directory/L"b").wstring()),c((directory/L"c").wstring());
    auto pa=a.profile(),pb=b.profile(),pc=c.profile();
    auto signer=std::make_shared<TestSigner>(pa);pa.identity=signer;pa.privateKey.clear();
    std::set<std::string> members{pa.peerId,pb.peerId,pc.peerId};
    auto current=[&](std::uint64_t account,const std::string& device){return account==8&&members.count(device)!=0;};
    std::map<std::pair<std::string,std::string>,ar::CurrentGrant> published;
    bool fresh=true;
    auto authoritative=[&](std::uint64_t account,const std::string& issuer,const std::string& device)->std::optional<ar::CurrentGrant>{
        auto found=published.find({issuer,device});
        if(account!=8||!fresh||found==published.end())return {};
        return found->second;
    };
    auto publish=[&](const ar::Grant& grant){published[{grant.issuer,grant.device}]={grant.generation,grant.capabilities,grant.issuanceId,grant.revoked};};
    ar::Authorization source(a,pa,8,current,authoritative),reader(b,pb,8,current,authoritative);
    check(!source.permits(pa.peerId,pb.peerId,ar::ReadHistory));
    rejects([&]{source.approveAuthority(pa.publicKey,false);});
    rejects([&]{source.issue(pb.publicKey,ar::ReadHistory,false,100,true);});
    source.approveAuthority(pa.publicKey,true);
    auto serve=source.issue(pa.publicKey,ar::ServeHistory,false,100,true);
    auto read=source.issue(pb.publicKey,ar::ReadHistory,false,100,true);
    signer->fail=true;
    rejects([&]{source.issue(pb.publicKey,0,true,101,true);});
    signer->fail=false;
    check(source.grant(pa.peerId,pb.peerId).generation==1&&pa.privateKey.empty());
    check(!source.permits(pa.peerId,pb.peerId,ar::ReadHistory));
    publish(serve);publish(read);
    rejects([&]{reader.accept(read);}); // Same account/device membership is not consent.
    reader.approveAuthority(pa.publicKey,true);check(reader.accept(serve));check(reader.accept(read));
    check(!reader.accept(ar::decodeGrant(ar::encodeGrant(read)))); // Idempotent.
    check(reader.permits(pa.peerId,pb.peerId,ar::ReadHistory));
    check(!reader.permits(pa.peerId,pc.peerId,ar::ReadHistory));
    auto modified=read;modified.account=9;rejects([&]{reader.accept(modified);});
    modified=read;modified.device=pc.peerId;rejects([&]{reader.accept(modified);});
    modified=read;modified.capabilities=ar::AllCapabilities;rejects([&]{reader.accept(modified);});
    modified=read;modified.signature[0]^=1;rejects([&]{reader.accept(modified);});
    auto invalid=ar::encodeGrant(read);invalid.push_back(0);rejects([&]{ar::decodeGrant(invalid);});
    {ar::Authorization reopened(b,pb,8,current,authoritative);check(reopened.permits(pa.peerId,pb.peerId,ar::ReadHistory));}
    rejects([&]{ar::Authorization wrongAccount(b,pb,9,current,authoritative);});
    auto revoked=source.issue(pb.publicKey,0,true,110,true);check(revoked.generation==2);
    publish(revoked);
    check(!reader.permits(pa.peerId,pb.peerId,ar::ReadHistory));
    reader.accept(revoked);check(!reader.permits(pa.peerId,pb.peerId,ar::ReadHistory));
    rejects([&]{reader.accept(read);});
    auto reenrolled=source.issue(pb.publicKey,ar::ReadHistory,false,120,true);check(reenrolled.generation==3);
    publish(reenrolled);reader.accept(reenrolled);check(reader.permits(pa.peerId,pb.peerId,ar::ReadHistory));
    rejects([&]{reader.accept(revoked);});
    fresh=false;check(!reader.permits(pa.peerId,pb.peerId,ar::ReadHistory));fresh=true;
    members.erase(pb.peerId);check(!reader.permits(pa.peerId,pb.peerId,ar::ReadHistory));
    rejects([&]{source.issue(pb.publicKey,ar::ReadHistory,false,130,true);});
    members.insert(pb.peerId);
    // Independent policy instance cannot continue using stale in-memory approval.
    ar::Authorization stale(b,pb,8,current,authoritative);reader.disableAuthority(pa.peerId,true);
    rejects([&]{stale.permits(pa.peerId,pb.peerId,ar::ReadHistory);});
    check(!reader.permits(pa.peerId,pb.peerId,ar::ReadHistory));
    rejects([&]{reader.accept(reenrolled);});
    rejects([&]{reader.approveAuthority(pa.publicKey,true);});
    check(a.profile().peerId==pa.peerId&&b.profile().peerId==pb.peerId);
    std::cout<<"PASS: explicit archive grants, account/device binding, tamper, protected reopen, revocation, stale generation, re-enrollment, device removal, CAS stale owner and current metadata freshness\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
