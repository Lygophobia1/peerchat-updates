#include "../storage.cpp"
#include "../archive_provenance.hpp"
#include <iostream>
#include <set>
using namespace peerchat;
namespace ar=peerchat::archive;
static void check(bool value){if(!value)throw std::runtime_error("Archive security regression failed");}
template<class F>static void rejects(F operation){bool failed=false;try{operation();}catch(const std::exception&){failed=true;}check(failed);}
int main(){try{
    auto directory=std::filesystem::current_path()/L"build"/L"tests"/L"archive-security"/storage_detail::wide(Storage::newMessageId());
    Storage a((directory/L"a").wstring()),b((directory/L"b").wstring()),c((directory/L"c").wstring()),d((directory/L"external").wstring());
    auto pa=a.profile(),pb=b.profile(),pc=c.profile(),pd=d.profile();
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
    ar::OriginalRecord record;record.senderAccount=8;record.recipientAccount=9;
    record.message={Storage::newMessageId(),pa.peerId,pd.peerId,L"Legacy historical record",1700000000000ull,Delivery::Delivered,0,1};
    check(a.append(pd.peerId,record.message));
    auto deviceAccount=[&](const std::string& peer)->std::optional<std::uint64_t>{
        if(peer==pd.peerId)return 9;
        if(members.count(peer))return 8;
        return {};
    };
    ar::ProvenanceVerifier sourceRecords(source,a,pa,8,deviceAccount),readerRecords(reader,b,pb,8,deviceAccount);
    auto wrongContext=record;wrongContext.recipientAccount=10;
    rejects([&]{sourceRecords.attest(wrongContext,pa.peerId,140);});
    ar::ProvenanceVerifier missingDirectory(source,a,pa,8,[](const std::string&)->std::optional<std::uint64_t>{return {};});
    rejects([&]{missingDirectory.attest(record,pa.peerId,140);});
    auto assertion=sourceRecords.attest(record,pa.peerId,140);
    check(readerRecords.verify(assertion)==ar::Provenance::ArchiveAttested);
    auto wire=ar::encodeAssertion(assertion);
    auto decoded=ar::decodeAssertion(wire);
    check(ar::encodeAssertion(decoded)==wire&&readerRecords.verify(decoded)==ar::Provenance::ArchiveAttested);
    check(decoded.original.message.id==record.message.id&&decoded.original.message.sender==pa.peerId);
    auto truncated=wire;truncated.pop_back();rejects([&]{ar::decodeAssertion(truncated);});
    auto trailing=wire;trailing.push_back(0);rejects([&]{ar::decodeAssertion(trailing);});
    auto unknown=wire;unknown[2]=2;rejects([&]{ar::decodeAssertion(unknown);});
    rejects([&]{ar::decodeAssertion(Blob(protocol::MaxPacket+1));});
    // Large Unicode payload remains bounded and round-trips without the
    // smaller nested-blob ceiling accidentally rejecting valid history.
    auto large=record;large.message.id=Storage::newMessageId();large.message.text=std::wstring(16384,L'\u4e00');
    check(a.append(pd.peerId,large.message));
    auto largeAssertion=sourceRecords.attest(large,pa.peerId,140);
    check(ar::encodeAssertion(ar::decodeAssertion(ar::encodeAssertion(largeAssertion)))==ar::encodeAssertion(largeAssertion));
    check(readerRecords.verify(ar::decodeAssertion(ar::encodeAssertion(largeAssertion)))==ar::Provenance::ArchiveAttested);
    // Local delivery state/cursors cannot alter or confer sender evidence.
    auto localState=assertion;localState.original.message.delivery=Delivery::Stored;localState.original.message.sequence=999;
    check(ar::encodeAssertion(localState)==wire);
    check(source.permits(pa.peerId,pa.peerId,ar::ServeHistory));
    auto absent=record;absent.message.id=Storage::newMessageId();rejects([&]{sourceRecords.attest(absent,pa.peerId,140);});
    auto altered=assertion;altered.original.message.text=L"Substituted";
    rejects([&]{readerRecords.verify(altered);});
    altered=assertion;altered.original.senderAccount=9;rejects([&]{readerRecords.verify(altered);});
    altered=assertion;altered.original.message.id=Storage::newMessageId();rejects([&]{readerRecords.verify(altered);});
    altered=assertion;altered.original.message.utcMilliseconds++;rejects([&]{readerRecords.verify(altered);});
    altered=assertion;altered.archiveDevice=pc.peerId;rejects([&]{readerRecords.verify(altered);});
    altered=assertion;altered.signature[0]^=1;rejects([&]{readerRecords.verify(altered);});
    // Independent policy instance cannot continue using stale in-memory approval.
    ar::Authorization stale(b,pb,8,current,authoritative);reader.disableAuthority(pa.peerId,true);
    rejects([&]{stale.permits(pa.peerId,pb.peerId,ar::ReadHistory);});
    check(!reader.permits(pa.peerId,pb.peerId,ar::ReadHistory));
    rejects([&]{reader.accept(reenrolled);});
    rejects([&]{reader.approveAuthority(pa.publicKey,true);});
    check(a.profile().peerId==pa.peerId&&b.profile().peerId==pb.peerId);
    std::cout<<"PASS: explicit archive grants, account/device binding, tamper, protected reopen, revocation, stale generation, re-enrollment, device removal, CAS stale owner and honest legacy provenance\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
