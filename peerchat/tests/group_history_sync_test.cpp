#include "../archive_sync.hpp"
#include "../portable/reliable_endpoint.hpp"
#include "../platform/windows_sql_database.hpp"
#include <filesystem>
#include <iostream>
using namespace peerchat;
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F>static void denied(F fn){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}need(failed,"Expected fail-closed rejection");}
int main(){const char* stage="setup";try{
    auto id=Storage::newMessageId();auto root=std::filesystem::current_path()/L"build"/L"tests"/L"group-history"/std::wstring(id.begin(),id.end());
    Storage a((root/L"a").wstring()),b((root/L"b").wstring()),c((root/L"c").wstring()),d((root/L"d").wstring());
    auto pa=a.profile(),pb=b.profile(),pc=c.profile(),pd=d.profile();GroupLedger ledger(true);const auto local=newGroupId(),server=newGroupId();
    auto append=[&](GroupEventKind kind,const Profile& actor,std::uint64_t account,const Profile& subject,std::uint64_t target){
        GroupEvent e;e.groupId=local;e.logicalOrder=ledger.state().maxLogicalOrder+1;e.createdMilliseconds=100;
        e.kind=kind;e.actorAccountId=account;e.actorPeerId=actor.peerId;e.subjectAccountId=target;e.subjectPeerId=subject.peerId;
        if(kind==GroupEventKind::Create){e.value=L"History fixture";e.subjectName=L"Owner";e.subjectPublicKey=subject.publicKey;}
        if(kind==GroupEventKind::Invite){e.subjectName=L"Member";e.subjectPublicKey=subject.publicKey;}
        if(kind==GroupEventKind::AuthorizeDevice){e.value=L"PeerChat/group-device-authorization/v1";e.subjectPublicKey=subject.publicKey;}
        if(kind==GroupEventKind::RevokeDevice)e.value=L"PeerChat/group-device-revocation/v1";
        signGroupEvent(e,actor);need(ledger.append(e),"Signed fixture event rejected");
    };
    append(GroupEventKind::Create,pa,1,pa,1);append(GroupEventKind::Invite,pa,1,pc,2);append(GroupEventKind::Accept,pc,2,pc,2);
    append(GroupEventKind::AuthorizeDevice,pa,1,pb,1);
    group_keys::RosterSnapshot roster{local,server,pa.peerId,1,{{1,pa.peerId,GroupRole::Owner,false},{1,pb.peerId,GroupRole::Owner,false},{2,pc.peerId,GroupRole::Member,false}}};
    // Explicit opt-in fixture for an ALREADY authorized sibling. This does not
    // enable or bypass production enrollment or manufacture a historical grant.
    group_keys::GroupKeyManager ka(a,1,pa.peerId,true),kb(b,1,pb.peerId,true),kc(c,2,pc.peerId,true);
    ka.apply(roster);kb.apply(roster);kc.apply(roster);need(ka.ensureCurrentKey(server,ledger.state()),"Initial key creation");
    auto offers=a.pendingGroupKeyHandoffs(pc.peerId);need(offers.size()==1&&kc.accept(offers[0],pa.peerId,ledger.state()),"Third member handoff");
    group_messages::MessageLayer ma(a,ka,pa,1),mb(b,kb,pb,1),mc(c,kc,pc,2);
    Blob first;
    for(unsigned n=0;n<73;++n){auto body=group_text::encode({100+n,n+1,L"Group history Unicode \U0001f44b\U0001f3fd\nline"});auto made=ma.create(local,ledger.state(),body);need(made.result==group_messages::Result::Accepted,"Source creation");if(n==0)first=made.wire;}
    auto third=mc.create(local,ledger.state(),group_text::encode({200,74,L"Third participant"}));need(third.result==group_messages::Result::Accepted,"Third participant create");need(ma.receive(local,ledger.state(),third.wire,group_text::valid).result==group_messages::Result::Accepted,"Third participant receive");
    bool active=true;auto context=[&](const std::string& group)->std::optional<archive::GroupHistoryContext>{if(!active||group!=server)return {};return archive::GroupHistoryContext{roster,ledger.state()};};
    archive::GroupHistoryAccess aa(a,pa,1,context),ab(b,pb,1,context);
    std::map<std::string,archive::CurrentGrant> grants;std::set<std::string> devices{pa.peerId,pb.peerId,pd.peerId};
    auto current=[&](std::uint64_t account,const std::string& peer){return account==1&&devices.count(peer)!=0;};
    auto published=[&](std::uint64_t account,const std::string& issuer,const std::string& peer)->std::optional<archive::CurrentGrant>{if(account!=1||issuer!=pa.peerId||!grants.count(peer))return {};return grants.at(peer);};
    stage="archive_grants";archive::Authorization authA(a,pa,1,current,published),authB(b,pb,1,current,published);authA.approveAuthority(pa.publicKey,true);authB.approveAuthority(pa.publicKey,true);
    for(const auto& key:{pa.publicKey,pb.publicKey,pd.publicKey}){auto g=authA.issue(key,archive::AllCapabilities,false,100,true);grants[g.device]={g.generation,g.capabilities,g.issuanceId,g.revoked};authB.accept(g);}
    auto scopeA=archive::sessionAuthorization(authA,pa.peerId,pa.peerId,pb.peerId),scopeB=archive::sessionAuthorization(authB,pa.peerId,pa.peerId,pb.peerId);
    delivery::DeliveryManager da(a,pa.peerId),db(b,pb.peerId);auto directory=[](const std::string&)->std::optional<std::uint64_t>{return {};};
    stage="reader_construct";auto reader=std::make_unique<archive::SyncReader>(b,pb,authB,db,scopeB,directory,std::function<void()>{},&ab);
    bool pageTampered=false;
    auto run=[&](bool restart){unsigned pages=0;while(!reader->snapshot().complete){need(++pages<200,"Unbounded history loop");stage="reader_request";auto request=reader->nextRequest(true,true);need(!request.empty(),"Missing request");stage="source_page";auto reply=archive::serveSyncControl(a,pa,authA,scopeA,request,directory,500,nullptr,&aa);
            protocol::Reader domain(reply);if(restart&&!pageTampered&&domain.string()==archive::GroupHistoryDomain){auto forged=archive::decodeGroupHistoryPage(reply);if(!forged.items.empty()&&!forged.items.back().wire.empty()){
                auto before=b.groupArchiveWatermark();forged.items.back().wire.back()^=1;auto invalid=archive::encodeGroupHistoryPage(forged);denied([&]{reader->accept(invalid);});need(b.groupArchiveWatermark()==before,"Invalid whole page partially committed");
                forged=archive::decodeGroupHistoryPage(reply);forged.request.id=Storage::newMessageId();invalid=archive::encodeGroupHistoryPage(forged);denied([&]{reader->accept(invalid);});pageTampered=true;
            }}
            stage="reader_accept";reader->accept(reply);
            if(restart&&pages==6){reader->disconnected();reader=std::make_unique<archive::SyncReader>(b,pb,authB,db,scopeB,directory,std::function<void()>{},&ab);}}
    };
    run(false);stage="missing_key_result";need(reader->snapshot().deferred==74&&b.groupArchiveWatermark()==0,"Missing historical key was not retained unresolved");
    offers=a.pendingGroupKeyHandoffs(pb.peerId);need(offers.size()==1&&kb.accept(offers[0],pa.peerId,ledger.state()),"Delayed recipient key");reader->rescan();run(true);
    need(reader->snapshot().deferred==0&&b.groupArchiveWatermark()==74,"Nonempty durable group import");
    need(pageTampered,"Tamper/request-binding cases did not execute");
    reader->rescan();run(false);need(b.groupArchiveWatermark()==74,"Repeated scan duplicated history");
    // A whole page is verified before any new record is committed.
    auto request=archive::encodeGroupHistoryRequest({Storage::newMessageId(),{},0,0});auto response=archive::serveGroupHistory(a,pa,authA,scopeA,request,aa);auto page=archive::decodeGroupHistoryPage(response);
    need(!page.items.empty(),"No encrypted records");page.items.back().wire.back()^=1;auto corrupted=archive::encodeGroupHistoryPage(page);
    // Cursor at end has no rows; a separate new source-bound fixture below tests
    // tamper directly through the same signature/GCM admission path.
    (void)corrupted;auto tampered=first;tampered.back()^=1;denied([&]{ab.check(tampered,scopeB);});
    need(b.groupArchiveWatermark()==74,"Rejected input reserved a message ID");
    active=false;need(aa.check(first,scopeA).state==1,"Unavailable server roster granted history");active=true;
    // Rotation never authorizes a removed sender for FUTURE messages, but an
    // originally signed message remains attributable under retained epoch 1.
    append(GroupEventKind::Remove,pa,1,pc,2);++roster.generation;roster.devices.pop_back();ka.apply(roster);kb.apply(roster);need(ka.ensureCurrentKey(server,ledger.state()),"Epoch rotation");
    auto checked=ab.check(third.wire,scopeB);need(checked.state==0,"Historical sender attribution lost on removal");platform::group_crypto::wipe(checked.plaintext.data(),checked.plaintext.size());
    auto next=ma.create(local,ledger.state(),group_text::encode({300,75,L"New epoch"}));need(next.result==group_messages::Result::Accepted,"Rotated message");reader->rescan();run(false);
    need(reader->snapshot().deferred==1&&b.groupArchiveWatermark()==74,"Missing rotated key advanced cursor");
    offers=a.pendingGroupKeyHandoffs(pb.peerId,local,roster.generation);need(offers.size()==1&&kb.accept(offers[0],pa.peerId,ledger.state()),"Delayed rotated key");reader->rescan();run(false);need(b.groupArchiveWatermark()==75,"Rotated import recovery");
    // Same identity leaving and rejoining has a new signed membership instance.
    auto epoch=group_history::retainedEpoch(a,server,1);need(epoch.has_value(),"No protected epoch evidence");auto changed=ledger.state();changed.authorizedDevices.at(pb.peerId).membershipId=std::string(64,'a');
    need(!group_history::continuousMember(*epoch,changed,1,pb.peerId),"Rejoined identity revived old history authority");
    append(GroupEventKind::AuthorizeDevice,pa,1,pd,1);++roster.generation;roster.devices.push_back({1,pd.peerId,GroupRole::Owner,false});ka.apply(roster);kb.apply(roster);
    auto otherScope=archive::sessionAuthorization(authA,pa.peerId,pa.peerId,pd.peerId);need(aa.check(first,otherScope).state==2&&d.groupKey(local,1).empty(),"New device inherited old history/key");
    append(GroupEventKind::RevokeDevice,pa,1,pb,1);++roster.generation;roster.devices.erase(std::remove_if(roster.devices.begin(),roster.devices.end(),[&](const auto& m){return m.peerId==pb.peerId;}),roster.devices.end());ka.apply(roster);
    need(aa.check(first,scopeA).state==2,"Revoked device accessed retained history");
    auto revoked=authA.issue(pb.publicKey,0,true,600,true);grants[pb.peerId]={revoked.generation,0,revoked.issuanceId,true};denied([&]{archive::serveGroupHistory(a,pa,authA,scopeA,request,aa);});
    Storage reopened((root/L"b").wstring());need(reopened.profile().peerId==pb.peerId&&reopened.groupArchiveWatermark()==75&&reopened.authenticatedGroupHistory(local).size()==75,"Restart lost protected group history");
    // Commit arrival is intentionally the reverse of authenticated order.
    // An opaque local sequence identifies the boundary but is not sort order.
    Storage ordered((root/L"ordered").wstring());auto original=a.authenticatedGroupHistory(local);need(original.size()==75,"Ordering fixture source");
    for(auto it=original.rbegin();it!=original.rend();++it){group_messages::Envelope e;need(group_messages::parse(it->envelope,e)==group_messages::Result::Accepted,"Ordering fixture parse");need(ordered.appendAuthenticatedGroupMessage(server,e.messageId,it->envelope,it->plaintext,local),"Ordering fixture append");}
    auto raw=platform::openWindowsSqlDatabase((root/L"ordered").wstring());
    raw->execute("DELETE FROM group_message_order_v1"); // Isolated legacy-index fixture only; messages remain untouched.
    need(!ordered.groupHistoryOrderingReady(local,1),"Legacy reindex ignored its batch limit");
    unsigned batches=0;while(!ordered.groupHistoryOrderingReady(local,8))need(++batches<20,"Unbounded legacy reindex");
    std::set<std::string> seen;std::int64_t before=INT64_MAX;std::uint64_t upper=UINT64_MAX;
    for(unsigned n=0;n<20;++n){auto rows=ordered.authenticatedGroupHistory(local,8,before);if(rows.empty())break;
        need(group_text::decode(rows.back().plaintext).order<upper,"Older page crossed authenticated order boundary");upper=group_text::decode(rows.front().plaintext).order;before=rows.front().sequence;
        for(const auto& row:rows){group_messages::Envelope e;need(group_messages::parse(row.envelope,e)==group_messages::Result::Accepted&&seen.insert(e.messageId).second,"Paging duplicate/corrupt envelope");}}
    need(seen.size()==75&&ordered.groupArchiveWatermark()==75,"Reverse-arrival paging omitted durable messages");
    std::cout<<"PASS group history: 3 participants, authorized same-account device, 74 nonempty imports, bounded pages/restart/replay/dedupe, delayed keys, rotation, historical sender signature, tamper, new/rejoined/revoked devices, signed grant revocation and protected reopen. HOST FIXTURE; no enrollment activation or physical acceptance.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL stage="<<stage<<": "<<error.what()<<'\n';return 1;}}
