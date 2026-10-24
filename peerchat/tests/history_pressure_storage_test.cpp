#include "../storage.cpp"
#include <iostream>
using namespace peerchat;
using namespace peerchat::storage_detail;
struct Observed:platform::RecordProtector {
 platform::WindowsRecordProtector inner;size_t reads=0;
 Blob protect(const Blob& b,const std::string& c)override{return inner.protect(b,c);}
 Blob unprotect(const Blob& b,const std::string& c)override{++reads;return inner.unprotect(b,c);}
};
std::string hexId(unsigned value,size_t width){char b[32];snprintf(b,sizeof(b),"%x",value);return std::string(width-strlen(b),'0')+b;}
int main(){try{
 auto path=std::filesystem::current_path()/L"build"/L"tests"/L"history-pressure"/wide(Storage::newMessageId());
 auto db=std::make_shared<WindowsSqlDatabase>(path.wstring());auto records=std::make_shared<Observed>();Storage store(db,records);auto local=store.profile().peerId;
 std::vector<std::string> peers;for(unsigned chat=1;chat<=9;++chat)peers.push_back(hexId(chat,64));
 // Isolated fixture records, no outbox or server authentication changes.
 {Transaction tx(*db);for(unsigned chat=0;chat<9;++chat)for(unsigned n=1;n<=4000;++n){
  auto id=hexId(chat+1,8)+hexId(n,24);StoredMessage m{id,peers[chat],local,L"Generated history pressure fixture",100000+n,Delivery::Delivered,0,n};Writer plain;encodeMessage(plain,m);
  Statement insert(*db,"INSERT INTO messages(id,peer,b) VALUES(?,?,?)");insert.text(1,id);insert.text(2,peers[chat]);insert.blob(3,records->protect(plain.data,"PeerChat.v1/message/"+id+"/"+peers[chat]));insert.row();
 }tx.commit();}
 for(auto& p:peers)require(store.history(p,8).size()==8,"Missing warmup page");
 auto before=records->reads;
 for(auto& p:peers)require(store.history(p,8).size()==8,"Missing recent page");
 std::cout<<"nine_chats=9 rows_per_chat=4000 warm_page_decryptions="<<records->reads-before<<"\n";
 require(records->reads-before==72,"Nine chats evicted full indexes");
 // A new reader must load protected recent suffixes, not rescan 36000 bodies.
 {auto reopenedRecords=std::make_shared<Observed>();Storage reopened(std::make_shared<WindowsSqlDatabase>(path.wstring()),reopenedRecords);
  auto first=reopenedRecords->reads;for(auto& p:peers)require(reopened.history(p,8).size()==8,"Missing reopened page");
  auto cost=reopenedRecords->reads-first;std::cout<<"nine_chat_reopen_decryptions="<<cost<<"\n";
  require(cost==81,"Reopening rescanned old history instead of protected suffixes");
  auto olderPage=reopened.history(peers[0],8,100);require(olderPage.size()==8&&olderPage.back().utcMilliseconds==100099,"Persisted suffix lost older paging");
 }
 // An unreadable optional suffix must not hide valid protected history.
 {Statement damaged(*db,"UPDATE objects SET b=? WHERE k=?");damaged.blob(1,Blob{0,1,2});damaged.text(2,"history-order-cache/v1/dm/"+peers[8]);damaged.row();}
 {Storage reopened(path.wstring());auto recovered=reopened.history(peers[8],8);require(recovered.size()==8&&recovered.back().utcMilliseconds==104000,"Damaged derived cache hid original messages");}
 // Append older rows to a truncated suffix. They must never create a false
 // complete page across the missing middle of the chronological index.
 for(unsigned n=1;n<=10;++n){StoredMessage m{hexId(99,8)+hexId(n,24),peers[0],local,L"late",n,Delivery::Delivered,0,n};store.append(peers[0],m);}
 auto current=store.history(peers[0],8);require(current.back().utcMilliseconds==104000,"Late append displaced newest rows");
 auto older=store.history(peers[0],8,100);require(older.size()==8&&older.front().utcMilliseconds==100092&&older.back().utcMilliseconds==100099,"Truncated cache returned an incomplete older page");
 for(auto& p:peers)store.history(p,8);
 auto latest=store.history(peers[0],8).back();
 {Storage other(path.wstring());other.eraseMessage(peers[0],latest.id);}
 current=store.history(peers[0],8);require(current.size()==8&&current.back().utcMilliseconds==103999,"External deletion resurrected or shortened page");
 before=records->reads;store.eraseMessage(peers[0],current.back().id);
 for(size_t i=1;i<peers.size();++i)require(store.history(peers[i],8).size()==8,"Other chat missing");
 require(records->reads-before==64,"Local delete invalidated unrelated histories");
 store.eraseHistory(peers[0]);require(store.history(peers[0],8).empty(),"Deleted conversation visible");
 before=records->reads;require(store.history(peers[0],8).empty()&&records->reads==before,"Cleared conversation repeatedly decrypted deletion records");
 StoredMessage fresh{Storage::newMessageId(),peers[0],local,L"new after local clear",200000,Delivery::Delivered};store.append(peers[0],fresh);require(store.history(peers[0],8).size()==1,"Fresh message hidden after clear");
 std::cout<<"PASS bounded nine-chat history, late arrivals, older paging, cross-handle deletion, local delete isolation, clear and append\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
