#include "../storage.cpp"
#include "../archive_history.hpp"
#include <iostream>
#include <set>
#include <iomanip>
#include <sstream>
using namespace peerchat;
namespace ar=peerchat::archive;
static void check(bool ok){if(!ok)throw std::runtime_error("Archive history storage regression");}
template<class F>static void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected);}
int main(){try{
    auto root=std::filesystem::current_path()/L"build"/L"tests"/storage_detail::wide(Storage::newMessageId());
    // Recreate the old schema in an isolated test-only profile, then prove the
    // additive index backfill preserves ordinary message order and outbox state.
    const auto legacyPath=(root/L"legacy").wstring();std::string legacyPeer(64,'b');std::vector<std::string> legacyIds;
    {Storage legacy(legacyPath);auto identity=legacy.profile();
        for(unsigned n=0;n<2;++n){
            StoredMessage m{Storage::newMessageId(),identity.peerId,legacyPeer,L"legacy fixture",42,Delivery::Queued,0,n+1};
            check(legacy.append(legacyPeer,m));legacyIds.push_back(m.id);
        }
    }
    {storage_detail::WindowsSqlDatabase legacy(legacyPath);
        legacy.execute("DROP TRIGGER message_archive_insert;DROP TRIGGER message_archive_delete;DROP TABLE message_archive_index;");
    }
    {Storage legacy(legacyPath);auto page=legacy.archiveHistoryPage(legacyPeer,0,legacy.archiveHistoryWatermark());
        check(page.complete&&page.messages.size()==2&&page.cursors==std::vector<std::int64_t>({1,2})&&
            page.messages[0].id==legacyIds[0]&&page.messages[1].id==legacyIds[1]&&legacy.pending(legacyPeer).size()==2);
    }
    Storage a((root/L"source").wstring()),b((root/L"reader").wstring()),c((root/L"friend").wstring());
    auto pa=a.profile(),pb=b.profile(),pc=c.profile();
    std::set<std::string> devices{pa.peerId,pb.peerId};
    std::map<std::string,ar::CurrentGrant> published;
    auto member=[&](std::uint64_t account,const std::string& peer){return account==8&&devices.count(peer);};
    auto grants=[&](std::uint64_t account,const std::string& issuer,const std::string& peer)->std::optional<ar::CurrentGrant>{
        if(account!=8||issuer!=pa.peerId||!published.count(peer))return {};
        return published.at(peer);
    };
    auto directory=[&](const std::string& peer)->std::optional<std::uint64_t>{
        if(peer==pc.peerId)return 9;
        if(devices.count(peer))return 8;
        return {};
    };
    ar::Authorization source(a,pa,8,member,grants),reader(b,pb,8,member,grants);
    source.approveAuthority(pa.publicKey,true);reader.approveAuthority(pa.publicKey,true);
    auto serve=source.issue(pa.publicKey,ar::ServeHistory,false,100,true);
    auto read=source.issue(pb.publicKey,ar::ReadHistory,false,100,true);
    for(auto g:{serve,read}){published[g.device]={g.generation,g.capabilities,g.issuanceId,g.revoked};reader.accept(g);}
    auto scope=ar::sessionAuthorization(reader,pa.peerId,pa.peerId,pb.peerId);
    ar::ProvenanceVerifier attest(source,a,pa,8,directory);
    delivery::DeliveryManager deliveries(b,pb.peerId);
    std::vector<StoredMessage> originals;
    for(unsigned n=0;n<65;++n){
        std::ostringstream id;id<<std::setfill('0')<<std::setw(32)<<std::hex<<(1000-n); // Deliberately reverse lexical ID order.
        StoredMessage m{id.str(),pc.peerId,pa.peerId,L"archive storage Unicode \u4e00",42,Delivery::Delivered,0,n+1};
        check(a.append(pc.peerId,m));originals.push_back(m);
    }
    const auto storeId=a.archiveStoreId();
    const auto watermark=a.archiveHistoryWatermark();check(watermark==65);
    auto first=a.archiveHistoryPage(pc.peerId,0,watermark,8);
    check(first.messages.size()==8&&first.messages.front().id==originals.front().id&&!first.complete);
    auto checkpoint=first.nextCursor;
    // A later live message cannot enter a sealed historical window.
    auto newer=originals.back();newer.id=Storage::newMessageId();newer.logicalOrder++;
    check(a.append(pc.peerId,newer));
    unsigned imported=0;std::int64_t after=0;
    do{
        auto page=a.archiveHistoryPage(pc.peerId,after,watermark,8);check(page.nextCursor>after);
        for(const auto& m:page.messages){
            auto assertion=attest.attest({m,9,8},pa.peerId,200);
            check(deliveries.acceptArchiveHistory(pb,reader,scope,assertion,directory));
            check(!deliveries.acceptArchiveHistory(pb,reader,scope,assertion,directory));++imported;
        }
        after=page.nextCursor;
        if(page.complete)break;
    }while(true);
    check(imported==65&&b.history(pc.peerId,100).size()==65&&b.pending(pc.peerId).empty()&&b.pendingMailboxUploads().empty());
    check(b.message(originals.front().id)->delivery!=Delivery::Delivered&&b.message(originals.front().id)->recipient==pa.peerId);
    auto proof=b.archiveHistoryProof(originals.front().id);check(!proof.empty());
    auto real=originals.front();real.recipient=pb.peerId;real.delivery=Delivery::Delivered;
    check(!deliveries.acceptIncomingMessage(pc.peerId,real,8)); // Live/mailbox race converges with original sibling recipient.
    auto changed=real;changed.text=L"conflict";rejects([&]{deliveries.acceptIncomingMessage(pc.peerId,changed,8);});
    rejects([&]{deliveries.acceptIncomingMessage(pc.peerId,real,10);});
    // Live first, archive second: preserve real ACK state and first durable body.
    real=newer;real.recipient=pb.peerId;real.delivery=Delivery::Delivered;check(deliveries.acceptIncomingMessage(pc.peerId,real,8));
    auto newerProof=attest.attest({newer,9,8},pa.peerId,200);
    check(!deliveries.acceptArchiveHistory(pb,reader,scope,newerProof,directory));
    check(b.message(real.id)->delivery==Delivery::Delivered&&b.message(real.id)->recipient==pb.peerId);
    const std::string context(64,'e');b.saveArchiveSyncState(context,{},Blob{1,2,3});
    {Storage reopened((root/L"reader").wstring());check(reopened.archiveSyncState(context)==Blob({1,2,3}));check(reopened.archiveHistoryProof(originals.front().id)==proof);}
    rejects([&]{b.saveArchiveSyncState(context,{},Blob{4});});
    {Storage reopened((root/L"source").wstring());check(reopened.archiveStoreId()==storeId);check(reopened.archiveHistoryPage(pc.peerId,checkpoint,watermark,8).messages.front().id==originals[8].id);}
    check(a.archiveHistoryWatermark()>watermark);
    // Exercise the real bounded request/page controller, including loss and a
    // crash after a durable record but before the page checkpoint advances.
    b.eraseHistory(pc.peerId);check(b.archiveHistoryProof(originals.front().id).empty());
    auto sourceScope=ar::sessionAuthorization(source,pa.peerId,pa.peerId,pb.peerId);
    auto sync=std::make_unique<ar::HistoryReader>(b,pb,reader,deliveries,scope,pc.peerId,directory);
    auto request=sync->nextRequest();rejects([&]{sync->nextRequest();}); // Backpressure.
    source.setHistorySyncEnabled(false);
    rejects([&]{ar::serveHistoryPage(a,pa,source,sourceScope,request,directory,300);});
    source.setHistorySyncEnabled(true);
    auto response=ar::serveHistoryPage(a,pa,source,sourceScope,request,directory,300);
    reader.setHistorySyncEnabled(false);rejects([&]{sync->acceptPage(response);});
    check(b.history(pc.peerId,100).empty()); // In-flight pages cannot bypass OFF.
    reader.setHistorySyncEnabled(true);
    auto decoded=ar::decodeHistoryPage(response);check(decoded.records.size()==8);
    auto bad=decoded;bad.records.back().assertion.signature[0]^=1;
    rejects([&]{sync->acceptPage(ar::encodeHistoryPage(bad));});
    check(b.history(pc.peerId,100).empty()); // Verify WHOLE page before importing any record.
    check(deliveries.acceptArchiveHistory(pb,reader,scope,decoded.records.front().assertion,directory));
    sync.reset(); // Process-restart equivalent: no cursor was committed.
    sync=std::make_unique<ar::HistoryReader>(b,pb,reader,deliveries,scope,pc.peerId,directory);
    check(sync->snapshot().cursor==0);
    request=sync->nextRequest();response=ar::serveHistoryPage(a,pa,source,sourceScope,request,directory,300);
    check(sync->acceptPage(response)==7);rejects([&]{sync->acceptPage(response);});
    auto pausedCursor=sync->snapshot().cursor;reader.setHistorySyncEnabled(false);
    rejects([&]{sync->nextRequest();});check(sync->snapshot().cursor==pausedCursor);
    check(b.history(pc.peerId,100).size()==8); // OFF never erases imported history.
    reader.setHistorySyncEnabled(true); // Continue below with the same stable cursor/grant.
    const auto sealed=sync->snapshot().through;check(sealed==66);
    auto late=newer;late.id=Storage::newMessageId();late.logicalOrder++;
    check(a.append(pc.peerId,late));
    unsigned pages=1;
    while(!sync->snapshot().complete){
        request=sync->nextRequest();
        if(pages==2){
            sync->disconnected();request=sync->nextRequest(); // Lost response reissues same cursor, new correlation ID.
        }
        response=ar::serveHistoryPage(a,pa,source,sourceScope,request,directory,300);
        sync->acceptPage(response);++pages;
        if(pages==4){
            auto cursor=sync->snapshot().cursor;sync.reset();
            sync=std::make_unique<ar::HistoryReader>(b,pb,reader,deliveries,scope,pc.peerId,directory);
            check(sync->snapshot().cursor==cursor&&sync->snapshot().through==sealed);
        }
    }
    check(pages==9&&b.history(pc.peerId,100).size()==66&&!b.message(late.id));
    request=sync->nextRequest();response=ar::serveHistoryPage(a,pa,source,sourceScope,request,directory,300);
    check(sync->acceptPage(response)==1&&sync->snapshot().complete&&b.message(late.id));
    auto wrongStore=ar::decodeHistoryRequest(request);wrongStore.store=std::string(32,'a');
    rejects([&]{ar::serveHistoryPage(a,pa,source,sourceScope,ar::encodeHistoryRequest(wrongStore),directory,300);});
    auto malformed=response;malformed.push_back(0);rejects([&]{ar::decodeHistoryPage(malformed);});
    request=sync->nextRequest();response=ar::serveHistoryPage(a,pa,source,sourceScope,request,directory,300);
    published[pb.peerId].revoked=true;
    rejects([&]{sync->acceptPage(response);});
    rejects([&]{ar::serveHistoryPage(a,pa,source,sourceScope,request,directory,300);});
    published[pb.peerId].revoked=false;
    sync->cancel();rejects([&]{sync->nextRequest();});rejects([&]{sync->acceptPage(response);});
    check(b.pending(pc.peerId).empty()&&b.pendingMailboxUploads().empty());
    // Byte budget, not just record count: two large historical Unicode records
    // must occupy separate wire pages without rejecting valid historical text.
    auto large=newer;large.text=std::wstring(16384,L'\u4e00');large.id=Storage::newMessageId();check(a.append(pc.peerId,large));
    large.id=Storage::newMessageId();check(a.append(pc.peerId,large));
    ar::HistoryRequest big{Storage::newMessageId(),a.archiveStoreId(),pc.peerId,67,0};
    auto bigPage=ar::decodeHistoryPage(ar::serveHistoryPage(a,pa,source,sourceScope,ar::encodeHistoryRequest(big),directory,300));
    check(bigPage.records.size()==1&&!bigPage.complete);
    big=bigPage.request;big.after=bigPage.next;
    bigPage=ar::decodeHistoryPage(ar::serveHistoryPage(a,pa,source,sourceScope,ar::encodeHistoryRequest(big),directory,300));
    check(bigPage.records.size()==1&&bigPage.complete);
    a.eraseHistory(pc.peerId);check(a.archiveHistoryWatermark()>watermark);
    newer.id=Storage::newMessageId();check(a.append(pc.peerId,newer));
    check(a.archiveHistoryWatermark()>watermark+1); // Deleted positions cannot be reused.
    check(a.archiveHistoryPage(pc.peerId,0,watermark,8).complete);
    b.eraseHistory(pc.peerId);check(b.archiveHistoryProof(originals.front().id).empty());
    rejects([&]{a.archiveHistoryPage(pc.peerId,-1,watermark);});
    rejects([&]{a.archiveHistoryPage(pc.peerId,0,watermark,1000);});
    std::cout<<"PASS: bounded wire pages, whole-page authentication, 67 imports, sealed windows, restart/replay/dedupe, cancellation/revocation, stable cursors, provenance, no outbox/fake ACK and live/history races\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
