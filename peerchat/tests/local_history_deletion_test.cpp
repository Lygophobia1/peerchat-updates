#include "../storage.cpp"
#include <iostream>
using namespace peerchat;
int main(){try{
 auto path=std::filesystem::current_path()/L"build"/L"tests"/L"local-delete"/wide(Storage::newMessageId());
 const std::string peer(64,'b'),group(32,'c'),server(32,'d'),attachment(32,'e');StoredMessage first,second;
 {Storage store(path.wstring());auto p=store.profile();first={Storage::newMessageId(),p.peerId,peer,L"first",100,Delivery::Queued};second=first;second.id=Storage::newMessageId();second.utcMilliseconds=200;store.append(peer,first);store.append(peer,second);
  require(store.history(peer).size()==2,"Fixture missing");store.eraseMessage(peer,first.id);require(!store.localMessageHidden(std::string(64,'a'),first.id),"Conversation tombstone leaked");require(store.history(peer).size()==1&&!store.historyMessage(first.id),"Single delete failed");require(store.message(first.id).has_value()&&store.pending(peer).size()==2,"Local deletion altered delivery records");
  // Identity-prefix fixture: no attachment application is instantiated.
  auto addJob=[&](char transferChar,char messageChar){const std::string transfer(32,transferChar),logical(32,messageChar);protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/DMCacheJob/v2");w.string(p.peerId);w.string(transfer);w.string(logical);w.string(peer);auto previous=store.directAttachmentState(p.peerId);store.saveDirectAttachmentJob(p.peerId,transfer,{},w.data,previous,Blob{1});return w.data;};
  const auto singleJob=addJob('1','2');store.eraseMessageAndAliases(peer,std::string(32,'1'));require(store.localMessageHidden(peer,std::string(32,'1'))&&store.localMessageHidden(peer,std::string(32,'2')),"Offline single transfer deletion lost logical alias");require(store.directAttachmentJob(p.peerId,std::string(32,'1'))==singleJob,"Single deletion altered protocol job");
  addJob('3','4');store.eraseMessageAndAliases(peer,std::string(32,'4'));require(store.localMessageHidden(peer,std::string(32,'3')),"Offline logical deletion lost transfer alias");
  const auto unloadedJob=addJob('5','6');
  store.eraseHistory(peer);require(store.localMessageHidden(peer,std::string(32,'5'))&&store.localMessageHidden(peer,std::string(32,'6'))&&store.directAttachmentJob(p.peerId,std::string(32,'5'))==unloadedJob,"Whole clear lost unloaded attachment or altered protocol job");require(store.history(peer).empty()&&store.pending(peer).size()==2,"Clear altered pending sends");
  require(!store.append(peer,first),"Deleted replay duplicated");require(store.history(peer).empty(),"Replay resurrected deletion");
  auto fresh=first;fresh.id=Storage::newMessageId();fresh.utcMilliseconds=300;store.append(peer,fresh);require(store.history(peer).size()==1,"New incoming remained hidden");
  Blob envelope(300,0);std::copy(peer.begin(),peer.end(),envelope.begin()+110);
  store.appendAuthenticatedGroupMessage(server,attachment,envelope,group_text::encode({500,1,L"group"}),group);require(store.authenticatedGroupHistory(group).size()==1,"Group fixture missing");
  GroupEvent legacy;legacy.groupId=group;legacy.eventId=std::string(64,'f');legacy.actorPeerId=p.peerId;legacy.logicalOrder=2;legacy.createdMilliseconds=400;legacy.kind=GroupEventKind::Message;legacy.message=L"legacy";store.appendGroupEvent(legacy);
  store.eraseGroupHistory(group);require(store.localMessageHidden(group,legacy.eventId,true)&&store.groupEvents(group).size()==1,"Legacy deletion lost protocol event or rejected identity");require(store.authenticatedGroupHistory(group).empty(),"Group history retained hidden row");require(!store.authenticatedGroupMessage(server,attachment).plaintext.empty(),"Group authentication evidence deleted");
  store.eraseGroupMessage(group,attachment);require(store.localMessageHidden(group,attachment,true),"Attachment tombstone missing");
 }
 {Storage store(path.wstring());require(store.localMessageHidden(peer,std::string(32,'2'))&&store.localMessageHidden(peer,std::string(32,'6')),"Restart resurrected attachment alias");require(store.localMessageHidden(peer,first.id)&&store.history(peer).size()==1,"Restart resurrected history");require(store.localMessageHidden(group,attachment,true),"Group tombstone lost");require(store.pending(peer).size()==3,"Restart lost pending sends");}
 {Storage other((path/L"other").wstring());require(!other.localMessageHidden(peer,first.id),"Profile tombstone leaked");}
 std::cout<<"PASS local-only durable history deletion\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
