// Storage-only fixtures: authentication admission/transport is exercised by the
// existing archive and group-envelope suites, not simulated by these records.
#include "../storage.cpp"
#include <iostream>
#include <set>

using namespace peerchat;
using namespace peerchat::storage_detail;
struct CountRecords:platform::RecordProtector {
    platform::WindowsRecordProtector inner;unsigned reads=0;
    Blob protect(const Blob& b,const std::string& c)override{return inner.protect(b,c);}
    Blob unprotect(const Blob& b,const std::string& c)override{++reads;return inner.unprotect(b,c);}
};
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}require(failed,"Invalid operation accepted");}
std::vector<std::string> ids(const std::vector<StoredMessage>& rows){std::vector<std::string> result;for(const auto& row:rows)result.push_back(row.id);return result;}
int main(){try{
    const auto root=std::filesystem::current_path()/L"build"/L"tests"/L"chronology"/wide(Storage::newMessageId());
    const std::string peer(64,'b'),other(64,'c');
    auto records=std::make_shared<CountRecords>();
    Profile profile;std::vector<std::string> expected;StoredMessage early,latest,tie;
    {
        auto sql=std::make_shared<WindowsSqlDatabase>(root.wstring());Storage store(sql,records);profile=store.profile();
        // Descending arrival and contradictory logical values are deliberate.
        for(unsigned i=0;i<32;++i){StoredMessage m{Storage::newMessageId(),peer,profile.peerId,L"chronology fixture",1000+31-i,Delivery::Delivered,0,100+i};store.append(peer,m);}
        auto all=store.history(peer);require(all.size()==32&&all.front().utcMilliseconds==1000&&all.back().utcMilliseconds==1031,"Arrival/logical order replaced timestamps");
        expected=ids(all);latest=all.back();early=all.front();
        auto first=store.history(peer,8);const auto reads=records->reads;
        require(ids(store.history(peer,8))==ids(first)&&records->reads-reads==8,"Warm history decrypted the whole conversation");
        auto second=store.history(peer,8,first.front().sequence);require(second.size()==8&&second.back().utcMilliseconds==1023,"Opaque cursor compared arrival positions");
        // A late old message gets the newest rowid and belongs on the oldest page.
        StoredMessage late{Storage::newMessageId(),peer,profile.peerId,L"late fixture",900,Delivery::Delivered,0,999};store.append(peer,late);
        require(ids(store.history(peer,8))==ids(first),"Late old arrival displaced newest preview/page");
        require(store.history(peer).front().id==late.id,"Late old message was not merged chronologically");
        expected.insert(expected.begin(),late.id);
        // Equal timestamps use sender then ID, independently of arrival/logical.
        tie={std::string(32,'1'),peer,profile.peerId,L"tie fixture",1031,Delivery::Delivered,0,1};store.append(peer,tie);
        StoredMessage tie2=tie;tie2.id=std::string(32,'2');tie2.logicalOrder=900;store.append(peer,tie2);
        StoredMessage ownTie=tie;ownTie.id=std::string(32,'0');ownTie.sender=profile.peerId;ownTie.recipient=peer;store.append(peer,ownTie);
        all=store.history(peer);auto stable=all;std::sort(stable.begin(),stable.end(),[](const auto& a,const auto& b){return std::tie(a.utcMilliseconds,a.sender,a.id)<std::tie(b.utcMilliseconds,b.sender,b.id);});
        require(ids(all)==ids(stable),"Equal-time ID order is unstable");expected=ids(all);
        // Device-account pagination may anchor in another peer's local history.
        StoredMessage cross{Storage::newMessageId(),other,profile.peerId,L"other device",1015,Delivery::Delivered,0,1};store.append(other,cross);
        auto crossRow=store.history(other).front();auto beforeCross=store.history(peer,100,crossRow.sequence);
        require(beforeCross.size()>=15&&beforeCross.back().utcMilliseconds<=1015,"Cross-device anchor lost earlier history");
        // A second connection inserts a new item while the first cache is warm.
        Storage writer(root.wstring());StoredMessage external{Storage::newMessageId(),peer,profile.peerId,L"external live fixture",2000,Delivery::Delivered,0,1};writer.append(peer,external);
        require(store.history(peer,1).front().id==external.id,"External append did not invalidate ordering cache");expected.push_back(external.id);
        require(!store.append(peer,external),"History/live duplicate inserted twice");
        require(store.pending(peer).empty(),"History projection generated delivery work");
        const Blob tuning{1,2,3};require(store.transferTuning(profile.peerId).empty(),"Unexpected tuning fixture");
        store.saveTransferTuning(profile.peerId,{},tuning);store.saveTransferTuning(profile.peerId,tuning,tuning);
        rejects([&]{store.saveTransferTuning(profile.peerId,{},Blob{4});});rejects([&]{store.saveTransferTuning("bad",{},Blob{4});});
        require(store.transferJournal(profile.peerId).empty(),"Tuning modified the transfer journal");
        rejects([&]{store.saveTransferTuning(profile.peerId,tuning,Blob(8193,1));});
        // The conversation-count bound evicts old metadata rather than keeping
        // an unbounded per-friend map alive for the lifetime of the process.
        for(char digit='0';digit<='8';++digit){std::string device(64,digit);StoredMessage m{Storage::newMessageId(),device,profile.peerId,L"cache eviction fixture",1,Delivery::Delivered,0,1};store.append(device,m);store.history(device);}
        const auto beforeCold=records->reads;store.history(std::string(64,'0'));
        require(records->reads-beforeCold==2,"Ordering cache exceeded eight conversations");
    }
    {
        Storage reopened(root.wstring());require(reopened.profile().peerId==profile.peerId,"Reopen changed identity");
        require(reopened.transferTuning(profile.peerId)==Blob({1,2,3}),"Tuning was not durable");
        std::vector<std::string> paged;std::int64_t before=INT64_MAX;
        for(;;){auto page=reopened.history(peer,7,before);if(page.empty())break;before=page.front().sequence;
            auto pageIds=ids(page);paged.insert(paged.begin(),pageIds.begin(),pageIds.end());}
        require(paged==expected,"Restart/pagination skipped or duplicated late/equal-time messages");
        require(reopened.message(early.id)->utcMilliseconds==early.utcMilliseconds&&reopened.message(latest.id)->logicalOrder==latest.logicalOrder,"Projection rewrote authenticated fields");
        // Group records use the same chronology and opaque cursor contract.
        const std::string group(32,'d'),server(32,'e');
        for(unsigned i=0;i<5;++i){const std::string id(32,static_cast<char>('1'+i));Blob envelope(300,0);envelope[0]='P';envelope[1]='C';envelope[2]='G';envelope[3]='E';std::copy(peer.begin(),peer.end(),envelope.begin()+110);
            auto body=group_text::encode({5000-i,1+i,L"group fixture"});reopened.appendAuthenticatedGroupMessage(server,id,envelope,body,group);}
        auto groupPage=reopened.authenticatedGroupHistory(group,2);require(groupPage.size()==2&&group_text::decode(groupPage.front().plaintext).time==4999,"Group newest page is arrival ordered");
        auto groupOlder=reopened.authenticatedGroupHistory(group,2,groupPage.front().sequence);require(groupOlder.size()==2&&group_text::decode(groupOlder.front().plaintext).time==4997,"Group chronological cursor skipped records");
        Blob envelope(300,0);std::copy(peer.begin(),peer.end(),envelope.begin()+110);
        reopened.appendAuthenticatedGroupMessage(server,std::string(32,'0'),envelope,group_text::encode({5000,999,L"equal group"}),group);
        auto equalPage=reopened.authenticatedGroupHistory(group,2);require(equalPage.size()==2&&group_text::decode(equalPage.front().plaintext).text==L"equal group","Equal-time group order followed arrival/logical order");
        reopened.eraseHistory(other);require(reopened.history(other).empty(),"Erase kept a stale ordering cache");
    }
    // Protected preference context prevents a copied device row being reused.
    {WindowsSqlDatabase raw(root.wstring());raw.execute("INSERT INTO objects(k,b) SELECT 'transfer-tuning/v1/"+other+"',b FROM objects WHERE k='transfer-tuning/v1/"+profile.peerId+"'");}
    {Storage reopened(root.wstring());rejects([&]{reopened.transferTuning(other);});}
    std::cout<<"PASS: chronological DM/group paging; late, equal-time, cross-device, external/live duplicates, restart, bounded warm-cache reads, preserved fields/outbox, protected tuning CAS/durability/context\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
