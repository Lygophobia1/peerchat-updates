#include "../portable/group_application.hpp"
#include "../portable/reliable_endpoint.hpp"
#include <filesystem>
#include <iostream>
using namespace peerchat;
static void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
template<class F>static void rejects(F f){bool rejected=false;try{f();}catch(...){rejected=true;}check(rejected,"Expected fail-closed rejection");}
int main(int argc,char** argv){try{
    check(argc==2,"Isolated profile root required");auto root=std::filesystem::path(argv[1]);
    Storage a((root/"a").wstring()),b((root/"b").wstring()),c((root/"c").wstring());
    auto pa=a.profile(),pb=b.profile(),pc=c.profile();
    delivery::DeliveryManager da(a,pa.peerId),db(b,pb.peerId),dc(c,pc.peerId);
    portable::GroupApplication ga(a,pa,da,1),gb(b,pb,db,2),gc(c,pc,dc,3);
    ga.authenticated(1,{});auto proof=ga.create(L"Group \U0001F680",L"Owner",1000);auto create=decodeGroupEvent(proof);
    auto domain=std::string("PeerChat group roster v1|")+create.groupId+"|"+pa.peerId;
    group_keys::RosterSnapshot r;r.legacyGroupId=create.groupId;r.serverGroupId=Storage::publicKeyId(Blob(domain.begin(),domain.end())).substr(0,32);
    r.distributorPeerId=pa.peerId;r.generation=1;r.creationProof=proof;r.devices={{1,pa.peerId,GroupRole::Owner,false}};
    ga.authenticated(1,{r});r.generation=2;r.devices.push_back({2,pb.peerId,GroupRole::Member,false});
    ga.authenticated(1,{r});gb.authenticated(1,{r});
    check(!group_keys::GroupKeyManager::matchesSignedLedger(r,ga.ledger(create.groupId)->state())&&
          gb.ledger(create.groupId)==nullptr&&a.groupKey(create.groupId,2).empty()&&b.groupKey(create.groupId,2).empty(),
          "Authenticated membership alone supplied signed readiness or an epoch key");
    session::ReliableAuthorization ab{session::ReliableAuthorizationScope::GroupControl,r.serverGroupId,2,pb.peerId};
    session::ReliableAuthorization ba{session::ReliableAuthorizationScope::GroupControl,r.serverGroupId,2,pa.peerId};
    da.authorizeTransport({1},pb.peerId);db.authorizeTransport({2},pa.peerId);
    portable::GroupApplication::Control left(ga,ab,{1},pb.publicKey),right(gb,ba,{2},pa.publicKey);
    auto exchange=[&]{for(int pass=0;pass<12;++pass){for(auto& f:left.outgoing(1001+pass))for(auto& ack:right.receive(f,1001+pass))left.receive(ack,1001+pass);
        for(auto& f:right.outgoing(1001+pass))for(auto& ack:left.receive(f,1001+pass))right.receive(ack,1001+pass);}};
    exchange();check(ga.ledger(create.groupId)->state().members.size()==2&&gb.ledger(create.groupId)->state().members.size()==2,"Signed membership did not converge");
    check(group_keys::GroupKeyManager::matchesSignedLedger(r,ga.ledger(create.groupId)->state())&&
          group_keys::GroupKeyManager::matchesSignedLedger(r,gb.ledger(create.groupId)->state())&&
          a.groupKey(create.groupId,2).size()==32&&b.groupKey(create.groupId,2)==a.groupKey(create.groupId,2)&&
          a.hasGroupKeyReceipt(create.groupId,2,pb.peerId),
          "Signed membership and the current epoch key did not become ready after authenticated handoff");
    const auto eventCount=ga.ledger(create.groupId)->events().size(),storedEvents=a.groupEvents(create.groupId).size();
    const auto name=ga.ledger(create.groupId)->state().name;
    for(std::uint64_t n=0;n<32;n++){
        GroupEvent forbidden;forbidden.groupId=create.groupId;forbidden.kind=GroupEventKind::Rename;
        forbidden.logicalOrder=ga.ledger(create.groupId)->state().maxLogicalOrder+1;forbidden.createdMilliseconds=1200+n;
        forbidden.actorAccountId=2;forbidden.actorPeerId=pb.peerId;forbidden.value=L"Member cannot rename the group";signGroupEvent(forbidden,pb);
        protocol::Writer frame(protocol::GroupRosterEvent);frame.bytes(encodeGroupEvent(forbidden));
        rejects([&]{left.receive(frame.data,1300+n);});
        check(ga.ledger(create.groupId)->events().size()==eventCount&&a.groupEvents(create.groupId).size()==storedEvents&&
            ga.ledger(create.groupId)->state().name==name,"Rejected member metadata grew or changed the ledger");
    }
    // A full older unscoped page must not hide this group's current epoch offer.
    for(int n=0;n<64;n++){
        GroupKeyHandoff stale;stale.id=Storage::newMessageId();stale.groupId=create.groupId;stale.serverGroupId=r.serverGroupId;
        stale.distributorPeerId=pa.peerId;stale.recipientPeerId=pb.peerId;stale.rosterGeneration=stale.keyGeneration=1;
        stale.contentKey=a.groupKey(create.groupId,1);a.queueGroupKeyHandoff(stale);
    }
    // Exercise the additive endpoint with actual Schannel peer TLS and NO
    // authenticated friendship. Group scope must never manufacture friend trust.
    r.generation=3;ga.authenticated(1,{r});gb.authenticated(1,{r});ab.rosterGeneration=ba.rosterGeneration=3;
    auto oldPage=a.pendingGroupKeyHandoffs(pb.peerId,64);check(oldPage.size()==64,"Stale handoff fixture is incomplete");
    for(const auto& offer:oldPage)check(offer.keyGeneration==1,"Unscoped page did not place stale offers first");
    auto currentPage=da.pendingGroupKeyHandoffs(pb.peerId,create.groupId,3,64);
    check(currentPage.size()==1&&currentPage.front().keyGeneration==3&&currentPage.front().contentKey==a.groupKey(create.groupId,3),"Scoped pending query hid the current epoch behind stale handoffs");
    session::SessionTransportManager as(pa.peerId,pa.peerId,{1}),bs(pb.peerId,pb.peerId,{1});
    reliable::AuthenticatedFriendSnapshot af,bf;af.replace(1,1,{});bf.replace(2,1,{});
    auto admission=[](portable::GroupApplication& app,session::ReliableAuthorization scope,std::uint64_t token){
        auto state=std::make_shared<std::shared_ptr<portable::GroupApplication::Control>>();
        auto control=[&app,scope,token,state](const Blob& key)->portable::GroupApplication::Control&{if(!*state)*state=std::make_shared<portable::GroupApplication::Control>(app,scope,session::TransportInstanceId{token},key);return **state;};
        return portable::ReliableEndpoint::GroupAdmission{scope,[&app,scope](const Blob* key){return app.current(scope,key);},
            [control](const Blob& bytes,const Blob& key,std::uint64_t now){return control(key).receive(bytes,now);},
            [control](const Blob& key,std::uint64_t now){return control(key).outgoing(now);}};
    };
    auto route=[](std::uint64_t token){return session::RouteObservation{{token},"relay:group","authenticated roster","TLS group control",false,1,session::TransportKind::Relay,session::RouteClass::ServerRelay};};
    TlsCredentials ac(pa),bc(pb);
    portable::ReliableEndpoint tlsLeft(pa,a,as,da,af,{1},{101},std::make_unique<TlsSession>(ac,false),pb.peerId,route(101),1,{}, {},admission(ga,ab,101));
    portable::ReliableEndpoint tlsRight(pb,b,bs,db,bf,{1},{102},std::make_unique<TlsSession>(bc,true),pa.peerId,route(102),1,{}, {},admission(gb,ba,102));
    tlsLeft.receive({},2);tlsRight.receive({},2);
    for(std::uint64_t pass=3;pass<300;++pass){auto x=tlsLeft.takeOutput();if(!x.empty())tlsRight.receive(x,pass);auto y=tlsRight.takeOutput();if(!y.empty())tlsLeft.receive(y,pass);}
    check(tlsLeft.ready()&&tlsRight.ready(),"Scoped group TLS did not become ready");
    check(a.hasGroupKeyReceipt(create.groupId,3,pb.peerId)&&b.groupKey(create.groupId,3)==a.groupKey(create.groupId,3),"Current key handoff was starved by 64 stale offers");
    check(a.contacts().empty()&&b.contacts().empty(),"Group TLS manufactured friend pins");
    check(!tlsLeft.initialPin(1,400),"Group TLS allowed ordinary friend pin operation");
    auto text=L"Hello \U0001F44B \U0001F680 e\u0301 \u041f\u0440\u0438\u0432\u0435\u0442";
    auto message=ga.send(r.serverGroupId,text,2000);check(message.result==group_messages::Result::Accepted,"Native encrypted send failed");
    check(ga.retry(r.serverGroupId,message.envelope.messageId)==message.wire,"Retry changed retained PCGE wire or logical message ID");
    check(gb.receive(message.wire)==group_messages::Result::Accepted,"Native decrypt/import failed");
    check(gb.receive(message.wire)==group_messages::Result::DuplicateMessage,"Duplicate not rejected");
    auto history=b.authenticatedGroupHistory(create.groupId);check(history.size()==1&&group_text::decode(history.front().plaintext).text==text,"Unicode protected history mismatch");
    auto tampered=message.wire;tampered.back()^=1;check(gb.receive(tampered)!=group_messages::Result::Accepted,"Tamper admitted");
    check(gc.receive(message.wire)==group_messages::Result::RosterUnavailable,"Unauthorized third profile admitted");
    // Stored history and signed membership survive application recreation; new
    // current authorization is still mandatory before decrypting or releasing keys.
    portable::GroupApplication reopened(b,pb,db,2);check(!reopened.current(ba),"Restart retained live authorization");
    reopened.authenticated(2,{r});check(reopened.receive(message.wire)==group_messages::Result::DuplicateMessage,"Restart lost durable dedupe");
    rejects([&]{reopened.retry(r.serverGroupId,message.envelope.messageId);});
    portable::GroupApplication senderReopened(a,pa,da,1);senderReopened.authenticated(2,{r});
    check(senderReopened.retry(r.serverGroupId,message.envelope.messageId)==message.wire,"Restart retry re-encrypted retained ciphertext");
    reopened.authenticated(3,{});check(reopened.receive(message.wire)==group_messages::Result::RosterUnavailable,"Removed roster retained authority");
    rejects([&]{portable::GroupApplication::Control denied(reopened,ba,{3},pa.publicKey);});
    auto invalid=r;invalid.generation=1;rejects([&]{gb.authenticated(1,{invalid});});check(!gb.current(ba),"Rejected roster retained authority");
    check(b.authenticatedGroupHistory(create.groupId).size()==1,"Revocation destroyed protected history");
    auto sameAccount=r;sameAccount.devices.push_back({2,pc.peerId,GroupRole::Member,false});sameAccount.generation=4;
    rejects([&]{reopened.authenticated(4,{sameAccount});});
    check(tlsRight.takeOutput().empty()&&tlsRight.closed(),"Revoked endpoint retained ciphertext output");
    protocol::Writer unrelated(protocol::Text);unrelated.text(L"forbidden");rejects([&]{left.receive(unrelated.data,3000);});
    std::cout<<"PASS: Android native group adapter reuses signed ledger/key handoff/PCGE; Unicode, tamper, duplicate/restart, exact retry, stale handoff starvation prevention, rejected metadata no-growth, roster revocation and device admission. HOST ONLY\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
