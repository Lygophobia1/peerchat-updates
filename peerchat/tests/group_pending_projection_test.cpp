#include "../portable/group_pending_projection.hpp"
#include <array>
#include <filesystem>
#include <iostream>
using namespace peerchat;
static void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F>static void rejects(F operation,const char* reason){bool failed=false;try{operation();}catch(...){failed=true;}check(failed,reason);}
struct Fixture {
    Storage a,b;Profile pa,pb;delivery::DeliveryManager da,db;portable::GroupApplication ga,gb;
    std::vector<group_keys::RosterSnapshot> rows;
    Fixture(const std::filesystem::path& root):a((root/"a").wstring()),b((root/"b").wstring()),pa(a.profile()),pb(b.profile()),
        da(a,pa.peerId),db(b,pb.peerId),ga(a,pa,da,1),gb(b,pb,db,2){ga.authenticated(1,{});}
    group_keys::RosterSnapshot make(){
        auto proof=ga.create(L"Recovery \U0001F680",L"Owner",1000);auto creation=decodeGroupEvent(proof);
        auto domain=std::string("PeerChat group roster v1|")+creation.groupId+"|"+pa.peerId;
        group_keys::RosterSnapshot row;row.legacyGroupId=creation.groupId;row.serverGroupId=Storage::publicKeyId(Blob(domain.begin(),domain.end())).substr(0,32);
        row.distributorPeerId=pa.peerId;row.generation=1;row.creationProof=proof;row.devices={{1,pa.peerId,GroupRole::Owner,false}};
        rows.push_back(row);ga.authenticated(1,rows);return row;
    }
    void addRecipient(){
        auto& row=rows.front();row.generation=2;row.devices.push_back({2,pb.peerId,GroupRole::Member,false});
        ga.authenticated(1,rows);gb.authenticated(1,{row});
        session::ReliableAuthorization ab{session::ReliableAuthorizationScope::GroupControl,row.serverGroupId,2,pb.peerId};
        session::ReliableAuthorization ba{session::ReliableAuthorizationScope::GroupControl,row.serverGroupId,2,pa.peerId};
        check(da.authorizeTransport({1},pb.peerId)&&db.authorizeTransport({2},pa.peerId),"delivery authorization");
        portable::GroupApplication::Control left(ga,ab,{1},pb.publicKey),right(gb,ba,{2},pa.publicKey);
        for(std::uint64_t pass=0;pass<16;pass++){
            for(auto& frame:left.outgoing(1001+pass))for(auto& ack:right.receive(frame,1001+pass))left.receive(ack,1001+pass);
            for(auto& frame:right.outgoing(1001+pass))for(auto& ack:left.receive(frame,1001+pass))right.receive(ack,1001+pass);
        }
        check(a.groupKey(row.legacyGroupId,2)==b.groupKey(row.legacyGroupId,2)&&a.groupKey(row.legacyGroupId,2).size()==32,"native key handoff");
    }
    group_messages::Creation send(const std::string& group,const std::wstring& text=L"Queued \U0001F44B e\u0301 \u65e5\u672c\u8a9e"){
        auto result=ga.send(group,text,2000);check(result.result==group_messages::Result::Accepted,"native creation failed");return result;
    }
};
using Expected=std::map<std::string,Blob>;
static Expected drain(portable::GroupPendingProjection& projection,const portable::GroupApplication& app,std::size_t* scanned=nullptr){
    Expected result;for(int call=0;call<100;call++){
        auto batch=projection.scan(app);check(batch.scanned<=8&&batch.items.size()<=4,"batch count bound exceeded");
        if(scanned)*scanned+=batch.scanned;
        std::size_t bytes=0;
        for(const auto& item:batch.items){bytes+=item.wire.size()+104;group_messages::Envelope e;
            check(group_messages::parse(item.wire,e)==group_messages::Result::Accepted&&e.groupId==item.groupId&&e.messageId==item.messageId&&e.rosterGeneration==item.rosterGeneration,"projection header mismatch");
            check(result.emplace(item.messageId,item.wire).second,"cursor replayed a row within one scan pass");}
        check(bytes<=portable::GroupPendingProjection::MaxReturnedBytes,"batch byte bound exceeded");
        if(!batch.more)return result;
    }throw std::runtime_error("bounded history scan did not finish");
}
int main(int argc,char** argv){try{
    check(argc==2,"Isolated profile root required");const auto root=std::filesystem::path(argv[1]);
    Fixture f(root/"normal");f.make();f.addRecipient();auto primary=f.rows.front();auto other=f.make();
    Expected expected;std::map<std::string,std::string> statuses;
    const std::array<std::string,4> allowed{{"Android group queued","Awaiting server acceptance (online recipients only)","Not submitted; server event channel unavailable","Delivery unknown after disconnect"}};
    for(int i=0;i<17;i++){
        auto message=f.send(primary.serverGroupId);std::string status;
        if(i<12){status=allowed[i%4];expected.emplace(message.envelope.messageId,message.wire);}
        else if(i==12)status="Server accepted (online recipients only; not E2E delivery)";
        else if(i==13)status="Recipients: 1 accepted, 0 duplicates, 0 rejected / 1 sessions";
        else if(i==14)status="Group delivery rejected or unavailable";
        else if(i==15)status="unknown future state";
        check(f.a.setGroupMessageStatus(primary.serverGroupId,message.envelope.messageId,status,i==12?1:i==13?2:0),"status fixture write");
        statuses[message.envelope.messageId]=status;
        if(i==12)check(!f.a.setGroupMessageStatus(primary.serverGroupId,message.envelope.messageId,allowed[0],0),"late uncertainty erased server admission");
    }
    for(int i=0;i<6;i++){auto message=f.send(other.serverGroupId,std::wstring(4096,L'\u65e5'));expected.emplace(message.envelope.messageId,message.wire);}
    auto remote=f.gb.send(primary.serverGroupId,L"Incoming member message",3000);check(remote.result==group_messages::Result::Accepted,"remote native send");
    check(f.ga.receive(remote.wire)==group_messages::Result::Accepted,"remote native import");
    f.a.setGroupMessageStatus(primary.serverGroupId,remote.envelope.messageId,allowed[0],0);
    const auto primaryCount=f.a.authenticatedGroupHistory(primary.legacyGroupId).size();
    const auto otherCount=f.a.authenticatedGroupHistory(other.legacyGroupId).size();
    portable::GroupPendingProjection projection(f.a,f.pa);auto first=projection.scan(f.ga);
    check(first.scanned<=8&&first.items.size()==4&&first.more,"scan return budget exceeded or omitted pending rows");
    projection.reset();std::size_t inspected=0;auto actual=drain(projection,f.ga,&inspected);
    check(actual==expected&&inspected==primaryCount+otherCount,"pending allowlist, foreign sender or multi-group scan mismatch");
    check(projection.scan(f.ga).items.empty(),"exhausted cursor auto-retried");
    for(const auto& item:actual){group_messages::Envelope e;group_messages::parse(item.second,e);if(e.groupId==primary.serverGroupId)
        check(f.gb.receive(item.second)==group_messages::Result::Accepted,"recovered original ciphertext failed remote native import");}
    check(f.a.authenticatedGroupHistory(primary.legacyGroupId).size()==primaryCount&&f.a.authenticatedGroupHistory(other.legacyGroupId).size()==otherCount,"projection changed history");
    for(const auto& entry:statuses)check(f.a.authenticatedGroupMessage(primary.serverGroupId,entry.first).status==entry.second,"projection changed delivery status");
    // Profile/adapter recreation retains wire and IDs, but requires fresh live authority.
    Storage reopenedStore((root/"normal"/"a").wstring());auto reopenedProfile=reopenedStore.profile();
    delivery::DeliveryManager reopenedDelivery(reopenedStore,reopenedProfile.peerId);
    portable::GroupApplication reopened(reopenedStore,reopenedProfile,reopenedDelivery,1);
    portable::GroupPendingProjection restarted(reopenedStore,reopenedProfile);
    check(restarted.scan(reopened).items.empty(),"restart replayed without authenticated roster");
    reopened.authenticated(2,f.rows);check(drain(restarted,reopened)==expected,"restart altered wire or logical IDs");
    for(const auto& item:expected){group_messages::Envelope e;group_messages::parse(item.second,e);if(e.groupId==primary.serverGroupId)
        check(f.gb.receive(item.second)==group_messages::Result::DuplicateMessage,"restart replay bypassed durable remote dedupe");}
    // The projection never invents retry scheduling; a new send is visible after reset.
    auto fresh=f.send(primary.serverGroupId);check(projection.scan(f.ga).items.empty(),"new send reset itself without caller request");
    expected.emplace(fresh.envelope.messageId,fresh.wire);projection.reset();check(drain(projection,f.ga)==expected,"explicit reconnect/new-send reset missed retained wire");
    // Epoch changes automatically reset that group's cursor but never re-encrypt old rows.
    f.rows.front().generation=3;f.ga.authenticated(1,f.rows);auto rotated=f.send(primary.serverGroupId);
    auto current=drain(projection,f.ga);check(current.size()==1&&current.at(rotated.envelope.messageId)==rotated.wire,"stale epoch was replayed or re-encrypted");
    portable::GroupPendingProjection mismatched(f.a,f.pb);check(drain(mismatched,f.ga).empty(),"foreign local device replayed another account's ciphertext");
    f.ga.authenticated(2,{});projection.reset();check(drain(projection,f.ga).empty(),"revoked group retained replay authority");
    Fixture terminal(root/"terminal-only");auto terminalRow=terminal.make();
    for(int i=0;i<10;i++){auto message=terminal.send(terminalRow.serverGroupId);terminal.a.setGroupMessageStatus(terminalRow.serverGroupId,message.envelope.messageId,"Server accepted (online recipients only; not E2E delivery)",1);}
    portable::GroupPendingProjection terminalScan(terminal.a,terminal.pa);auto limited=terminalScan.scan(terminal.ga);
    check(limited.scanned==8&&limited.items.empty()&&limited.more,"terminal-only history bypassed eight-row scan bound");
    auto last=terminalScan.scan(terminal.ga);check(last.scanned==2&&last.items.empty()&&!last.more,"terminal-only cursor failed to finish");
    auto afterComplete=terminal.send(terminalRow.serverGroupId);terminalScan.changed();
    auto afterCompleteRows=drain(terminalScan,terminal.ga);
    check(afterCompleteRows.size()==1&&afterCompleteRows.at(afterComplete.envelope.messageId)==afterComplete.wire,"changed notification missed a same-epoch send after scan completion");
    // Frequent new commits/unchanged snapshots cannot keep an older backlog
    // behind the newest page forever. changed() preserves active read cursors.
    Fixture growing(root/"growing");auto growingRow=growing.make();Expected original;
    for(int i=0;i<24;i++){auto m=growing.send(growingRow.serverGroupId);original.emplace(m.envelope.messageId,m.wire);}
    portable::GroupPendingProjection growingScan(growing.a,growing.pa);Expected observed;
    bool completedPassRequestedRescan=false;
    for(int call=0;call<9;call++){
        growing.send(growingRow.serverGroupId);growingScan.changed();
        auto batch=growingScan.scan(growing.ga);check(batch.scanned<=8&&batch.items.size()<=4,"growing history exceeded scan bounds");
        for(const auto& item:batch.items)if(original.count(item.messageId))observed[item.messageId]=item.wire;
        if(observed==original){check(batch.more,"changed pass completed without a deferred rescan");completedPassRequestedRescan=true;break;}
    }
    check(observed==original&&completedPassRequestedRescan,"frequent new-send changes starved older queued ciphertext");
    // Once commits stop, exactly one deferred rescan drains and becomes idle.
    auto stable=drain(growingScan,growing.ga);check(!stable.empty()&&!growingScan.scan(growing.ga).more,"deferred rescan did not reach a stable completion");
    // Corrupt/forged records are deliberately inserted through low-level test
    // storage to prove projection verification, never through production ingress.
    for(int mode=0;mode<4;mode++){
        Fixture bad(root/("bad-"+std::to_string(mode)));auto row=bad.make();auto message=bad.send(row.serverGroupId);
        auto saved=bad.a.authenticatedGroupMessage(row.serverGroupId,message.envelope.messageId);auto id=Storage::newMessageId();
        Blob wire=message.wire,plain=saved.plaintext;
        if(mode==0)wire=Blob(300,0);
        else if(mode==1){auto e=message.envelope;e.messageId=id;wire=group_messages::serialize(e);}
        else if(mode==2){auto e=message.envelope;e.messageId=id;e.signature=signDevicePayload(group_messages::signingPayload(e),bad.pa);wire=group_messages::serialize(e);}
        else plain=group_text::encode({4000,1,L"Different protected body"});
        check(bad.a.appendAuthenticatedGroupMessage(row.serverGroupId,id,wire,plain,row.legacyGroupId),"malformed fixture insert");
        bad.a.setGroupMessageStatus(row.serverGroupId,id,allowed[0],0);portable::GroupPendingProjection denied(bad.a,bad.pa);
        rejects([&]{denied.scan(bad.ga);},"malformed/signature/AEAD/body mismatch replayed");
        rejects([&]{denied.scan(bad.ga);},"failed scan consumed corrupt cursor state");
    }
    std::cout<<"PASS: read-only native pending projection: bounded scans/multiple groups/status allowlist; exact PCGE/IDs across restart, durable recipient dedupe, explicit reset, stale/foreign/revoked filters, signature/AEAD/body corruption fail closed.\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
