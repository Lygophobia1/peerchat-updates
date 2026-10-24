#pragma once
#include "dm_attachment_receipt.hpp"
#include "dm_attachment_finalizer.hpp"
#include <deque>
#include <map>
#include <set>
#include <tuple>

namespace peerchat::dm_attachments {
// Worker-owned portable controller. Transport execution and filesystem policy
// belong to adapters; TransferManager remains the only chunk/retry owner.
class Application {
public:
 struct Platform {
  std::function<std::uint64_t()> wall,monotonic;
  std::function<std::unique_ptr<transfer::Hash>()> hash;
  std::function<std::unique_ptr<transfer::Sink>(const transfer::Metadata&)> stage,receiveSink;
  std::function<Blob(transfer::Sink&)> location;
  std::function<std::unique_ptr<transfer::Source>(const Blob&,const transfer::Metadata&)> reopen;
  std::function<std::unique_ptr<transfer::Source>(const Blob&,const transfer::Metadata&)> reopenPlain;
  std::function<std::unique_ptr<transfer::Sink>(const transfer::Metadata&,const Blob&)> resumeStage;
  std::function<std::unique_ptr<transfer::Sink>(const Manifest&,const Blob&)> plainSink;
  std::function<void(const Manifest&,const Blob&)> verified;
  std::function<void(const Blob&,const transfer::Metadata&)> discardCipher;
  std::function<Blob(transfer::Source&)> thumbnail;
 };
 using EvidenceLookup=std::function<Evidence()>;
 using KeyAccess=std::function<void(const std::string&,const std::function<void(platform::HpkeProvider&,platform::IdentityProvider&,const mailbox::RecipientBinding&,const std::shared_ptr<platform::RecipientKeyHandle>&)>&)>;
 struct Snapshot {std::string id,message,peer,name,state,error;bool sending=false,verified=false,uploadComplete=false,noticesStored=false;std::uint64_t size=0,done=0,time=0,order=0;Blob thumbnail={};};
 struct HistoryPage {std::vector<Snapshot> items;std::string cursor;bool more=false;};
private:
 struct Recipient {Blob notice,receipt;bool stored=false,verified=false;std::uint64_t done=0;};
 struct Job {
  std::string id,message,peer,server,state="preflight",error,lease,cursor;std::uint64_t account=0,recipient=0,time=0,order=0,route=0,version=2,negotiation=0,readOffset=0,retry=0,contextUntil=0,noticeDeadline=0;
  unsigned bodyVersion=1;bool earlyBlocked=false,sending=false,accepted=false,admitted=false,uploaded=false,verified=false,published=false,committed=false,outputNotified=false;
  std::uint64_t preparationCalls=0,preparationMicros=0;
  Blob audience,descriptor,cipherToken,outputToken,envelope,noticeAck,verifiedAck,sourceToken;std::string sourceName;std::uint64_t sourceSize=0;SecretRecord saved,serializedBody;std::unique_ptr<const Body> body;std::vector<Recipient> recipients;
  std::unique_ptr<transfer::Source> input;std::unique_ptr<transfer::Sink> sink,plain;
  std::unique_ptr<transfer::Hash> ph,ch;std::unique_ptr<group_attachments::Sealer> sealer;std::unique_ptr<Finalizer> finalizer;
 };
 struct Request {std::string id,job,operation;std::uint64_t generation=0,offset=0,maximum=0;Blob data,frame;bool dispatched=false;std::uint64_t sentAt=0,queuedAt=0;};
 struct ReplyRow {std::uint64_t account=0,done=0;std::string device;bool verified=false;Blob receipt;};
 struct Reply {std::uint64_t generation=0;std::string id,state;std::uint64_t cacheVersion=0,dmVersion=0,offset=0,size=0,expires=0,complete=0;std::string lease;Blob descriptorWire,audience,data;std::uint64_t newNotice=0;std::vector<ReplyRow> rows;std::string cursor;std::uint64_t more=0;unsigned bodyVersion=1;};
 static Reply decodeReply(const Blob& wire){
 protocol::check(wire.size()<=100000);protocol::Reader r(wire);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/DMCacheReply/v1");auto generation=r.number();auto id=r.string();auto state=r.string();auto cacheVersion=r.number(),dmVersion=r.number(),offset=r.number(),size=r.number(),expires=r.number(),complete=r.number();auto lease=r.string();auto descriptorWire=r.bytes(),audience=r.bytes(),data=r.bytes();auto newNotice=r.number(),count=r.number();protocol::check(complete<=1&&newNotice<=1&&count<=8&&expires<=253402300799ull&&(lease.empty()||protocol::id(lease,32))&&descriptorWire.size()<=2048&&audience.size()<=32768);
 std::vector<ReplyRow> rows;while(count--){ReplyRow row;row.account=r.number();row.device=r.string();row.done=r.number();auto verified=r.number();protocol::check(mailbox::account(row.account)&&protocol::id(row.device)&&verified<=1);row.verified=verified;row.receipt=r.bytes();protocol::check(row.receipt.size()<=4096);rows.push_back(std::move(row));}auto cursor=r.string();auto more=r.number();unsigned bodyVersion=1;if(r.cursor<r.data.size()){protocol::check(r.string()=="PeerChat/AttachmentBodyVersion/v1");auto n=r.number();protocol::check(n==1||n==2);bodyVersion=static_cast<unsigned>(n);}r.end();protocol::check(more<=1&&(cursor.empty()||protocol::id(cursor)));
 return {generation,std::move(id),std::move(state),cacheVersion,dmVersion,offset,size,expires,complete,std::move(lease),std::move(descriptorWire),std::move(audience),std::move(data),newNotice,std::move(rows),std::move(cursor),more,bodyVersion};
 }
 Storage& storage_;const Profile& identity_;transfer::TransferManager& transfers_;Platform io_;EvidenceLookup evidence_;KeyAccess keys_;
 using Indexed=std::pair<std::uint64_t,std::string>;
 struct IndexPage {std::string previous;std::vector<Indexed> entries;};
 std::vector<Indexed> openIndex_;std::set<std::string> working_;std::string previousPage_;bool legacyPreview_=false;
 std::map<std::string,std::unique_ptr<Job>> jobs_;std::deque<Request> requests_;SecretRecord previous_;
 std::uint64_t account_=0,generation_=0,nextRoute_=1,probeAt_=0,turn_=0;std::string server_;bool supported_=false;unsigned capability_=0,registrationBodyTarget_=1;
 std::uint64_t bodyEncodingCalls_=0,bodyReuseCalls_=0;
 std::uint64_t localPreparationCalls_=0,localPreparationSteps_=0,localHashPreparationSteps_=0,fullTickCalls_=0;
 Job& job(const std::string& id){if(!jobs_.count(id))loadJob(id);return *jobs_.at(id);}
 bool busy(const std::string& id)const{return std::any_of(requests_.begin(),requests_.end(),[&](const auto& r){return r.job==id;});}
 static transfer::Metadata metadata(const Job& j){protocol::check(bool(j.body));return {j.id,"ciphertext.peerchat",j.body->manifest.cipherSize(),j.body->manifest.cipherDigest};}
 static Blob digest(const Blob& wire){auto hex=Storage::publicKeyId(wire);Blob b;for(size_t i=0;i<hex.size();i+=2)b.push_back(static_cast<unsigned char>(std::stoul(hex.substr(i,2),nullptr,16)));return b;}
 mailbox::Header header(const Job& j)const{return mailbox::parse(j.envelope).header;}
 SecretRecord encodeProtectedBody(const Body& body,const mailbox::Header& h){++bodyEncodingCalls_;return encodeBody(body,h);}
 const Blob& bodyWire(const Job& j){
  protocol::check(bool(j.body)==!j.serializedBody.bytes.empty()&&j.serializedBody.bytes.size()<=mailbox::MaxBody);
  if(j.body)++bodyReuseCalls_;
  return j.serializedBody.bytes;
 }
 static bool allStored(const Job& j){return !j.recipients.empty()&&std::all_of(j.recipients.begin(),j.recipients.end(),[](const auto& r){return r.stored;});}
 void enqueue(Job* j,std::string operation,std::uint64_t offset=0,std::uint64_t maximum=0,Blob data={},Blob frame={}){
  if(j&&terminalCacheError(*j)&&operation!="cache_dm_release")return;
  protocol::check(account_);
  // Unsent receipt refreshes are optional reads. They must not consume the
  // last bounded slot needed by a newly selected file or cancellation cleanup.
  // Never remove a borrowed request, payload, control frame or durable reply.
  if(requests_.size()>=24&&operation!="cache_dm_receipts"){
   auto idle=std::find_if(requests_.begin(),requests_.end(),[](const auto& q){return !q.dispatched&&q.operation=="cache_dm_receipts";});if(idle!=requests_.end())requests_.erase(idle);
  }
  protocol::check(requests_.size()<24);requests_.push_back({Storage::newMessageId(),j?j->id:"",std::move(operation),generation_,offset,maximum,std::move(data),std::move(frame),false,0,io_.wall()});
 }
 // Register the legacy identity proof first so v1 senders can still bootstrap
 // this device. Only a server advertising v2 receives the optional upgrade.
 void registerBodyCapability(unsigned version){
  protocol::check(version==1||version==2);
  keys_("",[&](auto&,auto& signer,const auto& local,const auto&){protocol::check(local.accountId==account_&&local.device==identity_.peerId&&local.storageAllowed&&signer.publicKey()==identity_.publicKey);protocol::Writer proof(protocol::FileTransfer);proof.string("PeerChat/dm-cache-device/v1");proof.number(account_);proof.string(identity_.peerId);proof.string(local.keyId);proof.number(local.generation);proof.number(version);proof.number(1);proof.bytes(identity_.publicKey);proof.bytes(signer.sign(proof.data));enqueue(nullptr,"cache_dm_register",0,0,std::move(proof.data));});
 }
 Blob control(const Job& j,transfer::Kind kind,std::optional<std::uint64_t> offset={})const{protocol::Writer w(protocol::FileTransfer);w.number(j.version);w.number(static_cast<unsigned>(kind));w.string(j.id);if(j.version==2)w.number(j.negotiation);if(offset)w.number(*offset);return w.data;}
 void receive(Job& j,const Blob& frame){transfers_.receiveDirectCacheCiphertext(j.peer,metadata(j),j.body->manifest.size,frame,io_.monotonic());}

 // WIP v1 fixtures retain their original reader and writer without migration.
 // New profiles use additive per-job records and an atomic protected index.
 void saveLegacy(){
  protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/DMCacheJobs/v1");w.string(identity_.peerId);w.number(jobs_.size());
  for(const auto& item:jobs_){const auto& j=*item.second;w.string(j.id);w.string(j.message);w.string(j.peer);w.string(j.server);w.string(j.state);w.string(j.error);w.number(j.account);w.number(j.recipient);w.number(j.time);w.number(j.order);
   w.number(j.sending);w.number(j.accepted);w.number(j.admitted);w.number(j.uploaded);w.number(j.verified);w.number(j.published);w.number(j.committed);w.number(j.noticeDeadline);
   w.bytes(j.audience);w.bytes(j.descriptor);w.bytes(j.cipherToken);w.bytes(j.outputToken);w.bytes(j.envelope);w.bytes(j.noticeAck);w.bytes(j.verifiedAck);
   w.bytes(bodyWire(j));w.number(j.recipients.size());for(const auto& r:j.recipients){w.bytes(r.notice);w.bytes(r.receipt);w.number(r.stored);w.number(r.verified);w.number(r.done);}
  }
  SecretRecord next(std::move(w.data));protocol::check(next.bytes.size()<=960*1024);storage_.saveDirectAttachmentState(identity_.peerId,previous_.bytes,next.bytes);previous_.bytes.swap(next.bytes);
 }

 static bool terminalCacheError(const Job& j){return j.error=="cache_attachment_expired"||j.error=="cache_attachment_unavailable";}
 static bool active(const Job& j){if(!j.sending&&j.verified&&!j.cipherToken.empty())return true;if(j.state=="canceled")return !j.cipherToken.empty()||!j.lease.empty()||(j.sending&&j.admitted&&!j.uploaded);if(j.state=="failed"&&!j.body&&!j.cipherToken.empty())return true;if(j.state=="failed"||terminalCacheError(j))return false;return j.sending?(!j.uploaded||!allStored(j)):(j.noticeAck.empty()||(j.accepted&&!j.verified)||(j.verified&&!j.published));}
 void memoryRoom(){while(jobs_.size()>=64){auto oldest=jobs_.end();for(auto it=jobs_.begin();it!=jobs_.end();++it)if(!active(*it->second)&&!busy(it->first)&&(oldest==jobs_.end()||it->second->time<oldest->second->time))oldest=it;protocol::check(oldest!=jobs_.end());jobs_.erase(oldest);}}
 void room(){reconcileTerminalTransfers();auto count=std::count_if(jobs_.begin(),jobs_.end(),[](const auto& item){return active(*item.second);});protocol::check(count<32);memoryRoom();}
 std::string storePage(const std::vector<Indexed>& entries,const std::string& previous){
  protocol::check(!entries.empty()&&entries.size()<=128&&(previous.empty()||protocol::id(previous)));protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/DMCacheIndexPage/v1");w.string(identity_.peerId);w.string(previous);w.number(entries.size());for(const auto& e:entries){w.string(e.second);w.number(e.first);}auto id=Storage::publicKeyId(w.data);storage_.saveDirectAttachmentIndexPage(identity_.peerId,id,w.data);return id;
 }
 IndexPage readPage(const std::string& id){
  protocol::check(protocol::id(id));SecretRecord wire(storage_.directAttachmentIndexPage(identity_.peerId,id));protocol::check(!wire.bytes.empty()&&Storage::publicKeyId(wire.bytes)==id);protocol::Reader r(wire.bytes);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/DMCacheIndexPage/v1"&&r.string()==identity_.peerId);IndexPage p;p.previous=r.string();protocol::check(p.previous.empty()||(protocol::id(p.previous)&&p.previous!=id));auto count=r.number();protocol::check(count&&count<=128);std::set<std::string> unique;while(count--){auto object=r.string();auto time=r.number();protocol::check(protocol::id(object,32)&&time&&unique.insert(object).second);p.entries.push_back({time,object});}r.end();return p;
 }
 void sealPages(std::vector<Indexed>& entries,std::string& previous){while(entries.size()>128){std::vector<Indexed> page(entries.begin(),entries.begin()+128);previous=storePage(page,previous);entries.erase(entries.begin(),entries.begin()+128);}}

 void save(){
  if(legacyPreview_){saveLegacy();return;}
  for(auto& item:jobs_){auto& j=*item.second;protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/DMCacheJob/v2");w.string(identity_.peerId);w.string(j.id);w.string(j.message);w.string(j.peer);w.string(j.server);w.string(j.state);w.string(j.error);w.number(j.account);w.number(j.recipient);w.number(j.time);w.number(j.order);
   w.number(j.sending);w.number(j.accepted);w.number(j.admitted);w.number(j.uploaded);w.number(j.verified);w.number(j.published);w.number(j.committed);w.number(j.noticeDeadline);
   w.bytes(j.audience);w.bytes(j.descriptor);w.bytes(j.cipherToken);w.bytes(j.outputToken);w.bytes(j.envelope);w.bytes(j.noticeAck);w.bytes(j.verifiedAck);w.bytes(j.sourceToken);w.string(j.sourceName);w.number(j.sourceSize);w.string(j.lease);
   w.bytes(bodyWire(j));w.number(j.recipients.size());for(const auto& r:j.recipients){w.bytes(r.notice);w.bytes(r.receipt);w.number(r.stored);w.number(r.verified);w.number(r.done);}
   SecretRecord next(std::move(w.data));protocol::check(next.bytes.size()<=900*1024);if(next.bytes==j.saved.bytes)continue;
   auto entries=openIndex_;auto working=working_;auto previousPage=previousPage_;if(j.saved.bytes.empty())entries.push_back({j.time,j.id});if(active(j))working.insert(j.id);else working.erase(j.id);protocol::check(working.size()<=32);sealPages(entries,previousPage);
   protocol::Writer ix(protocol::FileTransfer);ix.string("PeerChat/DMCacheHead/v1");ix.string(identity_.peerId);ix.string(previousPage);ix.number(entries.size());for(const auto& e:entries){ix.string(e.second);ix.number(e.first);}ix.number(working.size());for(const auto& id:working)ix.string(id);protocol::check(ix.data.size()<=65536);
   SecretRecord nextIndex(std::move(ix.data));storage_.saveDirectAttachmentJob(identity_.peerId,j.id,j.saved.bytes,next.bytes,previous_.bytes,nextIndex.bytes);j.saved.bytes.swap(next.bytes);previous_.bytes.swap(nextIndex.bytes);openIndex_=std::move(entries);working_=std::move(working);previousPage_=std::move(previousPage);
  }
 }
 std::unique_ptr<Job> readJob(protocol::Reader& r,bool modern){
  auto flag=[&](){auto n=r.number();protocol::check(n<=1);return n==1;};
  auto j=std::make_unique<Job>();j->id=r.string();j->message=r.string();j->peer=r.string();j->server=r.string();j->state=r.string();j->error=r.string();j->account=r.number();j->recipient=r.number();j->time=r.number();j->order=r.number();j->sending=flag();j->accepted=flag();j->admitted=flag();j->uploaded=flag();j->verified=flag();j->published=flag();j->committed=flag();j->noticeDeadline=r.number();j->audience=r.bytes();j->descriptor=r.bytes();j->cipherToken=r.bytes();j->outputToken=r.bytes();j->envelope=r.bytes();j->noticeAck=r.bytes();j->verifiedAck=r.bytes();
  if(modern){j->sourceToken=r.bytes();j->sourceName=r.string();j->sourceSize=r.number();j->lease=r.string();protocol::check(j->sourceToken.size()<=4096&&j->sourceSize<=transfer::MaxCachedFileBytes&&(j->sourceName.empty()||transfer::safeName(j->sourceName))&&(j->lease.empty()||protocol::id(j->lease,32)));}
  SecretRecord plain(r.bytes());if(!plain.bytes.empty()){auto body=std::make_unique<Body>(decodeStoredBody(plain.bytes,header(*j)));j->body=std::move(body);j->serializedBody.bytes.swap(plain.bytes);}auto recipients=r.number();protocol::check(recipients<=32);while(recipients--){Recipient p;p.notice=r.bytes();p.receipt=r.bytes();p.stored=flag();p.verified=flag();p.done=r.number();j->recipients.push_back(std::move(p));}
  protocol::check(protocol::id(j->id,32)&&protocol::id(j->message,32)&&protocol::id(j->peer)&&protocol::id(j->server)&&mailbox::account(j->account)&&mailbox::account(j->recipient)&&j->time&&j->order);
  if(j->body){protocol::check(j->body->descriptor==j->descriptor&&j->body->audience==j->audience&&j->body->manifest.binding.attachment==j->id&&header(*j).messageId==j->message);for(const auto& p:j->recipients){auto h=mailbox::parse(p.notice).header;protocol::check(h.messageId==j->message&&h.senderDevice==identity_.peerId&&h.recipientAccount==j->recipient);}}
  else if(j->sending&&j->state!="canceled"&&j->state!="failed"){
   try{protocol::check(modern&&io_.reopenPlain&&!j->sourceToken.empty());j->input=io_.reopenPlain(j->sourceToken,{j->id,j->sourceName,j->sourceSize,{}});protocol::check(j->input&&j->input->size()==j->sourceSize&&j->input->name()==j->sourceName);j->state="preflight";}
   catch(...){j->state="failed";j->error="Original source cannot be reopened securely; select it again";}
  }
  return j;
 }
 void loadJob(const std::string& id){
  protocol::check(protocol::id(id,32));memoryRoom();SecretRecord bytes(storage_.directAttachmentJob(identity_.peerId,id));protocol::check(!bytes.bytes.empty()&&bytes.bytes.size()<=900*1024);protocol::Reader r(bytes.bytes);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/DMCacheJob/v2"&&r.string()==identity_.peerId);auto j=readJob(r,true);r.end();protocol::check(j->id==id);j->saved.bytes.swap(bytes.bytes);protocol::check(jobs_.emplace(id,std::move(j)).second);
 }
 void load(){
  previous_.bytes=storage_.directAttachmentState(identity_.peerId);if(previous_.bytes.empty())return;protocol::Reader r(previous_.bytes);protocol::check(r.type()==protocol::FileTransfer);auto domain=r.string();protocol::check(r.string()==identity_.peerId);
  if(domain=="PeerChat/DMCacheJobs/v1"){auto count=r.number();protocol::check(count<=32);legacyPreview_=true;while(count--){auto j=readJob(r,false);auto id=j->id;protocol::check(jobs_.emplace(id,std::move(j)).second);}r.end();return;}
  if(domain=="PeerChat/DMCacheIndex/v1"){
   auto count=r.number();protocol::check(count<=512);std::set<std::string> unique;while(count--){auto id=r.string();auto time=r.number(),flag=r.number();protocol::check(protocol::id(id,32)&&time&&flag<=1&&unique.insert(id).second);openIndex_.push_back({time,id});if(flag){loadJob(id);if(active(*jobs_.at(id)))working_.insert(id);}}r.end();std::sort(openIndex_.begin(),openIndex_.end());
  }else {
   protocol::check(domain=="PeerChat/DMCacheHead/v1");previousPage_=r.string();protocol::check(previousPage_.empty()||protocol::id(previousPage_));auto count=r.number();protocol::check(count<=128);std::set<std::string> unique;while(count--){auto id=r.string();auto time=r.number();protocol::check(protocol::id(id,32)&&time&&unique.insert(id).second);openIndex_.push_back({time,id});}auto activeCount=r.number();protocol::check(activeCount<=32);while(activeCount--){auto id=r.string();protocol::check(protocol::id(id,32)&&working_.insert(id).second);}r.end();for(const auto& id:working_)loadJob(id);
  }
  protocol::check(working_.size()<=32);auto recent=openIndex_;if(recent.size()<32&&!previousPage_.empty()){auto page=readPage(previousPage_);recent.insert(recent.begin(),page.entries.begin(),page.entries.end());}size_t loaded=0;for(auto it=recent.rbegin();it!=recent.rend()&&jobs_.size()<64&&loaded++<32;++it)if(!jobs_.count(it->second))loadJob(it->second);
 }
 // Finish only cleanup when cancellation races a borrowed server request.
 // In particular, a late admitted object/lease must never activate a transfer.
 bool canceledResult(Job& j,const Request& q,bool accepted,const std::string& lease={},const std::string& state={}){
  if(j.state!="canceled")return false;
  if(accepted&&q.operation=="cache_dm_release"){j.lease.clear();j.admitted=false;j.route=0;j.error.clear();save();return true;}
  if(state=="cache_attachment_unavailable"||state=="cache_attachment_expired"){j.lease.clear();j.admitted=false;j.route=0;save();return true;}
  if(state=="cache_lease_expired"||state=="cache_lease_invalid")j.lease.clear();
  if(!accepted){j.retry=io_.wall()+10000;j.error="Canceled; server cleanup will retry when available";save();return true;}
  if(q.operation=="cache_dm_acquire"){protocol::check(protocol::id(lease,32));j.lease=lease;}
  const bool admitted=q.operation=="cache_dm_admit"||q.operation=="cache_dm_status";if(admitted)j.admitted=true;
  if(!busy(j.id)){
   if(!j.lease.empty())enqueue(&j,"cache_dm_release",0,0,j.sending?Blob{1}:Blob{});
   else if(admitted&&j.sending&&!j.uploaded)enqueue(&j,"cache_dm_acquire");
  }
  save();return true;
 }
 void pause(Job& j,const std::string& reason){requests_.erase(std::remove_if(requests_.begin(),requests_.end(),[&](const auto& q){return q.job==j.id;}),requests_.end());if(j.state=="canceled"){j.error="Canceled; server cleanup will retry when available";j.retry=io_.wall()+10000;j.route=0;j.contextUntil=0;save();return;}if(!terminalCacheError(j)){j.error=reason;j.retry=io_.wall()+10000;if(!j.verified)j.state="paused";}j.route=0;j.contextUntil=0;save();}
 bool terminalTransfersReconciled_=false;
 void reconcileTerminalTransfers(){if(terminalTransfersReconciled_)return;for(const auto& d:transfers_.retainedRecords())if(d.purpose==transfer::Purpose::DirectCacheCiphertext&&d.state!=transfer::State::Complete&&d.state!=transfer::State::Failed&&d.state!=transfer::State::Cancelled){auto& j=job(d.metadata.id);if(terminalCacheError(j)){protocol::check(j.body&&j.peer==d.peer&&metadata(j)==d.metadata);transfers_.retireDirectCacheCiphertext(j.peer,j.id,j.error);}}terminalTransfersReconciled_=true;}
 void cacheUnavailable(Job& j,const std::string& reason){transfers_.retireDirectCacheCiphertext(j.peer,j.id,reason);j.error=reason;j.state=j.verified?"complete":"failed";j.retry=0;j.route=0;j.contextUntil=0;requests_.erase(std::remove_if(requests_.begin(),requests_.end(),[&](const auto& q){return q.job==j.id;}),requests_.end());save();}
 Evidence authorizedEvidence(const Job& j){
  auto evidence=evidence_();protocol::check(evidence.identity&&evidence.current&&j.contextUntil>io_.wall());auto mapping=evidence.current;auto relationship=j.body?j.body->manifest.binding.relationship:parseAudience(j.audience,j.account,identity_.peerId,j.recipient).relationship;
  evidence.current=[mapping,relationship](const auto& b,const std::string& rel,std::uint64_t other){return rel==relationship&&mapping(b,rel,other);};return evidence;
 }
 void acknowledgeNotice(Job& j){auto evidence=authorizedEvidence(j);validateReceivedBody(*j.body,header(j),evidence);storage_.observeLogicalOrder(j.order);mailbox::Receipt ack;ack.recipientAccount=j.recipient;ack.recipientDevice=identity_.peerId;ack.senderDevice=header(j).senderDevice;ack.messageId=j.message;ack.envelopeDigest=digest(j.envelope);ack.signature=signDevicePayload(mailbox::receiptPayload(ack),identity_);auto previous=j.noticeAck;auto state=j.state;j.noticeAck=mailbox::serializeReceipt(ack);j.state=j.accepted?"downloading":"available";try{save();}catch(...){j.noticeAck=std::move(previous);j.state=state;j.contextUntil=0;throw;}}
 // A prepare response lists devices; it cannot enroll them. Before any
 // ciphertext exists, narrow a legacy/full preflight to existing local pins,
 // then ask the server to authorize that exact subset in a fresh prepare.
 Blob preparedAudience(const Job& j,const Blob& wire,const char*& stage){
  if(j.body||j.sealer||j.sink||!j.cipherToken.empty()||!j.descriptor.empty()||!j.envelope.empty()||!j.recipients.empty()||j.admitted||j.uploaded||j.committed)return wire;
  stage="prepare_audience_parse";auto a=parseAudience(wire,j.account,identity_.peerId,j.recipient);auto evidence=evidence_();protocol::check(evidence.identity&&evidence.current);
  stage="prepare_source_identity";auto source=evidence.identity(j.account,identity_.peerId);protocol::check(source==identity_.publicKey);
  stage="prepare_source_signature";protocol::check(mailbox::verifyBinding(a.source,j.account,identity_.peerId,source,verifyDevicePayload,Storage::publicKeyId));
  stage="prepare_source_binding";protocol::check(evidence.current(a.source,a.relationship,j.recipient));
  auto contacts=storage_.contacts();std::vector<mailbox::RecipientBinding> selected;bool retained=false;
  for(const auto& b:a.recipients){
   auto contact=std::find_if(contacts.begin(),contacts.end(),[&](const auto& c){return c.peerId==b.device;});
   // Unverified nonselected siblings receive no envelope. Every verified
   // pin must still pass identity, signature and current binding checks.
   stage="prepare_recipient_identity";if(contact==contacts.end()||!contact->verified){protocol::check(b.device!=j.peer);continue;}
   protocol::check(contact->verified&&!contact->publicKey.empty()&&evidence.identity(j.recipient,b.device)==contact->publicKey);
   stage="prepare_recipient_signature";protocol::check(mailbox::verifyBinding(b,j.recipient,b.device,contact->publicKey,verifyDevicePayload,Storage::publicKeyId));
   stage="prepare_recipient_binding";protocol::check(evidence.current(b,a.relationship,j.account));selected.push_back(b);retained=retained||b.device==j.peer;
  }
  stage="prepare_selected_recipient";protocol::check(retained&&!selected.empty());
  if(selected.size()==a.recipients.size())return wire;
  protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/dm-cache-audience/v1");w.string(a.relationship);w.bytes(mailbox::serializeBinding(a.source));w.number(selected.size());for(const auto& b:selected)w.bytes(mailbox::serializeBinding(b));return w.data;
 }
 void beginSeal(Job& j,const char*& stage){
  // Reserve enough protected-state capacity for every immutable notice BEFORE
  // encrypting any bytes. Keep room for receipts and storage encryption overhead.
  stage="prepare_audience_parse";
  auto count=parseAudience(j.audience,j.account,identity_.peerId,j.recipient).recipients.size();std::uint64_t reserve=(j.audience.size()+8192)*(count+3);if(reserve>850*1024){j.state="failed";j.error="Protected attachment state capacity is full; no ciphertext was prepared";j.input.reset();save();return;}
  auto a=parseAudience(j.audience,j.account,identity_.peerId,j.recipient);stage="prepare_evidence";auto evidence=authorizedEvidence(j);verifyAudience(a,evidence.identity,&stage);stage="prepare_source_binding";protocol::check(evidence.current(a.source,a.relationship,j.recipient));stage="prepare_recipient_binding";for(const auto& r:a.recipients)protocol::check(evidence.current(r,a.relationship,j.account));
  group_attachments::Binding b;b.attachment=j.id;b.sender=identity_.peerId;b.account=j.account;b.direct=true;b.recipientAccount=j.recipient;b.relationship=a.relationship;b.audienceDigest=Storage::publicKeyId(j.audience);
  auto length=j.input->size();transfer::Metadata m{j.id,"ciphertext.peerchat",group_attachments::HeaderBytes+length+std::max<std::uint64_t>(1,(length+group_attachments::BlockBytes-1)/group_attachments::BlockBytes)*group_attachments::TagBytes,{}};
  stage="prepare_staging";if(j.cipherToken.empty())j.sink=io_.stage(m);else {protocol::check(bool(io_.resumeStage));j.sink=io_.resumeStage(m,j.cipherToken);}protocol::check(bool(j.sink));j.cipherToken=io_.location(*j.sink);protocol::check(!j.cipherToken.empty());stage="prepare_staging_save";save();stage="prepare_sealer";j.ph=io_.hash();j.ch=io_.hash();protocol::check(j.ph&&j.ch);j.sealer=std::make_unique<group_attachments::Sealer>(*j.input,*j.sink,std::move(b),*j.ph,*j.ch);j.state="preparing";stage="prepare_sealer_save";save();
 }
 void localStep(Job& j){
  try{const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(4);if(j.sealer){for(unsigned n=0;n<32&&j.sealer&&std::chrono::steady_clock::now()<until;++n)prepare(j);}else if(j.state=="verifying"){for(unsigned n=0;n<32&&j.state=="verifying"&&std::chrono::steady_clock::now()<until;++n)finalize(j);}else if(j.verified&&!j.outputNotified&&io_.verified){io_.verified(j.body->manifest,j.outputToken);j.outputNotified=true;}}
  catch(...){j.sealer.reset();j.finalizer.reset();j.sink.reset();j.input.reset();j.plain.reset();if(!j.verified){j.state="failed";j.error="Attachment encryption, integrity or protected storage failed";}else if(!terminalCacheError(j))j.error="Verified internal copy retained; output action failed";save();}
 }
 void prepare(Job& j){
  struct Timing {Job& job;std::chrono::steady_clock::time_point began=std::chrono::steady_clock::now();~Timing(){++job.preparationCalls;job.preparationMicros+=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-began).count());}} timing{j};
  if(!j.sealer->step())return;
  auto manifest=j.sealer->takeManifest();j.sealer.reset();j.descriptor=descriptor(manifest,j.message,identity_);auto a=parseAudience(j.audience,j.account,identity_.peerId,j.recipient);
  auto body=std::make_unique<Body>(Body{j.time,j.order,"application/octet-stream",std::move(manifest),j.descriptor,j.audience,{}});
  if(j.bodyVersion>=2&&io_.thumbnail)try{auto image=io_.thumbnail(*j.input);if(attachment_thumbnails::valid(image))body->thumbnail=std::move(image);}catch(...){}
  // Freeze every exact HPKE notice before ciphertext becomes publishable.
  keys_(a.source.keyId,[&](auto& hpke,auto& signer,const auto& local,const auto&){protocol::check(mailbox::serializeBinding(local)==mailbox::serializeBinding(a.source));auto evidence=evidence_();for(const auto& recipient:a.recipients){mailbox::Header h;h.senderAccount=j.account;h.senderDevice=identity_.peerId;h.recipientAccount=j.recipient;h.recipientDevice=recipient.device;h.keyId=recipient.keyId;h.keyGeneration=recipient.generation;h.messageId=j.message;h.createdUtc=j.time;h.expiresUtc=std::min(j.time+mailbox::MaxTtlMilliseconds,j.noticeDeadline);protocol::check(h.expiresUtc>io_.wall());h.logicalOrder=j.order;h.storageAllowed=true;body->mailboxBinding=mailbox::headerPayload(h);auto plain=encodeBody(*body,h);auto envelope=mailbox::seal(h,plain.bytes,recipient,evidence.identity(j.recipient,recipient.device),verifyDevicePayload,Storage::publicKeyId,signer,hpke);j.recipients.push_back({mailbox::serialize(envelope),{},false,false,0});}});
  protocol::check(!j.recipients.empty());j.envelope=j.recipients.front().notice;body->mailboxBinding=mailbox::headerPayload(header(j));auto encoded=encodeProtectedBody(*body,header(j));j.body=std::move(body);j.serializedBody.bytes.swap(encoded.bytes);j.cipherToken=io_.location(*j.sink);protocol::check(!j.cipherToken.empty());j.state="uploading";save();j.sink->commit();j.committed=true;save();j.sink.reset();j.input.reset();j.ph.reset();j.ch.reset();
 }
 void upload(Job& j){auto rows=transfers_.snapshots();if(std::none_of(rows.begin(),rows.end(),[&](const auto& r){return r.metadata.id==j.id;}))transfers_.offerDirectCacheCiphertext(j.peer,metadata(j),j.body->manifest.size,io_.reopen(j.cipherToken,metadata(j)),io_.monotonic());}
 bool uploadHandshake(const Job& j)const{auto rows=transfers_.snapshots();return std::any_of(rows.begin(),rows.end(),[&](const auto& r){return r.metadata.id==j.id&&r.sending&&r.state!=transfer::State::Complete&&r.state!=transfer::State::Cancelled&&r.state!=transfer::State::Failed;});}
 void activate(Job& j){protocol::check(!j.lease.empty()&&j.server==server_&&(j.sending?j.account:j.recipient)==account_);j.route=nextRoute_++;transfers_.cacheCapability(transfer::Purpose::DirectCacheCiphertext,j.peer,j.id,j.route,true);if(j.sending)upload(j);else offer(j);}
 void offer(Job& j){j.version=2;j.negotiation=j.route;auto m=metadata(j);protocol::Writer w(protocol::FileTransfer);w.data=control(j,transfer::Kind::Offer);w.string(m.name);w.number(m.size);w.bytes(Blob(m.digest.begin(),m.digest.end()));w.number(transfer::ExtendedChunkBytes);w.number(1);receive(j,w.data);auto rows=transfers_.snapshots();auto row=std::find_if(rows.begin(),rows.end(),[&](const auto& r){return r.metadata.id==j.id;});if(row!=rows.end()&&row->state==transfer::State::AwaitingAcceptance)transfers_.accept(j.id,io_.receiveSink(m),io_.monotonic());}
 void finalize(Job& j){
  if(!j.finalizer){auto rows=transfers_.completedRecords();auto r=std::find_if(rows.begin(),rows.end(),[&](const auto& d){return d.metadata.id==j.id&&d.purpose==transfer::Purpose::DirectCacheCiphertext;});protocol::check(r!=rows.end());j.cipherToken=r->location;j.input=io_.reopen(j.cipherToken,metadata(j));if(!j.plain)j.plain=io_.plainSink(j.body->manifest,j.outputToken);protocol::check(j.input&&j.plain);j.outputToken=io_.location(*j.plain);protocol::check(!j.outputToken.empty());j.ph=io_.hash();j.ch=io_.hash();protocol::check(j.ph&&j.ch);save();j.finalizer=std::make_unique<Finalizer>(*j.input,*j.plain,j.body->manifest,*j.ph,*j.ch);}
  if(!j.finalizer->step())return;
  j.finalizer.reset();j.verified=true;j.state="complete";j.verifiedAck=serializeReceipt(verifiedReceipt(*j.body,header(j),io_.wall(),identity_));save();j.plain.reset();j.input.reset();j.ph.reset();j.ch.reset();
 }
public:
 Application(Storage& storage,const Profile& profile,transfer::TransferManager& transfers,Platform io,EvidenceLookup evidence,KeyAccess keys):storage_(storage),identity_(profile),transfers_(transfers),io_(std::move(io)),evidence_(std::move(evidence)),keys_(std::move(keys)){protocol::check(io_.wall&&io_.monotonic&&io_.hash&&io_.stage&&io_.receiveSink&&io_.location&&io_.reopen&&io_.plainSink&&evidence_&&keys_);load();}
 void session(std::uint64_t account,std::uint64_t generation,const std::string& server){protocol::check(!account||(mailbox::account(account)&&generation&&protocol::id(server)));if(account==account_&&generation==generation_&&server==server_)return;requests_.clear();account_=account;generation_=generation;server_=server;supported_=false;capability_=0;probeAt_=0;for(auto& x:jobs_){x.second->route=0;x.second->contextUntil=0;}}
 std::string start(const std::string& peer,std::uint64_t recipient,std::unique_ptr<transfer::Source> source,const Blob& audience={}){protocol::check(account_&&supported_&&protocol::id(peer)&&mailbox::account(recipient)&&recipient!=account_&&source&&source->size()<=transfer::MaxCachedFileBytes&&transfer::safeName(source->name()));room();auto j=std::make_unique<Job>();j->id=Storage::newMessageId();j->message=Storage::newMessageId();j->peer=peer;j->server=server_;j->account=account_;j->recipient=recipient;if(!audience.empty())parseAudience(audience,account_,identity_.peerId,recipient);j->audience=audience;j->sending=true;j->time=io_.wall();j->order=storage_.nextLogicalOrder();j->sourceToken=source->resumeToken();j->sourceName=source->name();j->sourceSize=source->size();protocol::check(legacyPreview_||(!j->sourceToken.empty()&&j->sourceToken.size()<=4096&&bool(io_.reopenPlain)));j->input=std::move(source);auto id=j->id;jobs_.emplace(id,std::move(j));try{save();enqueue(&job(id),"cache_dm_prepare");}catch(...){jobs_.erase(id);throw;}return id;}
 void accept(const std::string& id){auto& j=job(id);protocol::check(!terminalCacheError(j)&&!j.sending&&j.body&&!j.noticeAck.empty()&&!j.verified&&j.server==server_&&j.recipient==account_);j.accepted=true;j.state="downloading";j.retry=0;save();}
 void accept(const std::string& id,std::unique_ptr<transfer::Sink> sink){protocol::check(bool(sink));accept(id);job(id).plain=std::move(sink);}
 void cancel(const std::string& id){auto& j=job(id);protocol::check(account_&&j.server==server_&&(j.sending?j.account:j.recipient)==account_&&!j.verified);if(j.sealer)j.sealer->cancel();if(j.finalizer)j.finalizer->cancel();j.sealer.reset();j.finalizer.reset();j.input.reset();j.sink.reset();j.plain.reset();for(const auto& row:transfers_.retainedRecords())if(row.metadata.id==j.id&&!row.sending)j.cipherToken=row.location;transfers_.cancel(id);requests_.erase(std::remove_if(requests_.begin(),requests_.end(),[&](const auto& r){return r.job==id&&!r.dispatched;}),requests_.end());j.state="canceled";if(j.sending&&std::any_of(requests_.begin(),requests_.end(),[&](const auto& q){return q.job==id&&q.operation=="cache_dm_admit"&&q.dispatched;}))j.admitted=true;save();if(!j.lease.empty()&&account_&&!busy(j.id))enqueue(&j,"cache_dm_release",0,0,j.sending?Blob{1}:Blob{});}
 // Presentation-neutral local CPU work only. No request, receipt or delivery
 // acknowledgement is created by observing this bounded continuation hint.
 // This local-only turn does not inspect transfer snapshots, history pages,
 // receipt jobs, group work or transport queues. A finished/failed sealer still
 // reports a performed turn so the adapter can release the native gate first.
 bool matchesSession(std::uint64_t account,std::uint64_t generation,const std::string& server)const{return account&&account_==account&&generation_==generation&&server_==server;}
 bool continueLocalPreparation(){
  ++localPreparationCalls_;if(!account_||!supported_)return false;
  const auto now=io_.wall();std::vector<Job*> work;
  for(auto& item:jobs_){auto& j=*item.second;if(j.server==server_&&j.sending&&j.account==account_&&j.state=="preparing"&&j.sealer&&now>=j.retry&&!terminalCacheError(j))work.push_back(&j);}
  if(!work.empty()){
   auto& j=*work[turn_++%work.size()];const auto before=j.preparationCalls;localStep(j);
   if(j.preparationCalls==before)return false;
   ++localPreparationSteps_;return true;
  }
  // Reopened ciphertext is still fully read and compared with its immutable
  // manifest digest. Only incomplete runtime hash turns avoid full cache ticks.
  for(auto& item:jobs_){auto& j=*item.second;if(j.server==server_&&j.sending&&j.account==account_&&j.state=="uploading"&&j.body&&j.committed&&!j.uploaded&&now>=j.retry&&!terminalCacheError(j))work.push_back(&j);}
  if(work.empty())return false;
  const auto start=turn_++%work.size();
  for(size_t offset=0;offset<work.size();++offset){auto& j=*work[(start+offset)%work.size()];if(transfers_.continueDirectCachePreparation(j.peer,metadata(j),j.body->manifest.size,io_.monotonic())){++localPreparationSteps_;++localHashPreparationSteps_;return true;}}
  return false;
 }
 bool localPreparationPending()const{
  if(!account_||!supported_)return false;
  if(std::any_of(jobs_.begin(),jobs_.end(),[&](const auto& item){const auto& j=*item.second;return j.server==server_&&j.sending&&j.account==account_&&j.state=="preparing"&&j.sealer&&!terminalCacheError(j);}))return true;
  const auto now=io_.wall();if(!transfers_.hasPreparationWork(io_.monotonic()))return false;
  // The retained encrypted source is still fully rehashed by TransferManager.
  // Keep that bounded local work moving before its first network Offer.
  for(const auto& row:transfers_.snapshots()){
   if(row.purpose!=transfer::Purpose::DirectCacheCiphertext||!row.sending||(row.state!=transfer::State::Preparing&&row.state!=transfer::State::Recovering))continue;
   auto found=jobs_.find(row.metadata.id);if(found==jobs_.end())continue;const auto& j=*found->second;
   if(j.server==server_&&j.sending&&j.account==account_&&j.state=="uploading"&&j.body&&now>=j.retry&&!terminalCacheError(j)&&row.peer==j.peer&&row.metadata==metadata(j))return true;
  }
  return false;
 }
 bool supported()const{return account_&&supported_;}
 // 0 waiting/unavailable, 1 negotiated+registered, 2 explicit incompatible.
 unsigned capabilityState()const{return account_?capability_:0;}
 Blob completedLocationFor(std::uint64_t account,const std::string& server,const std::string& id){protocol::check(mailbox::account(account)&&protocol::id(server));auto& j=job(id);protocol::check(j.verified&&j.server==server&&(j.sending?j.account:j.recipient)==account);return j.outputToken;}
 Blob completedLocation(const std::string& id){protocol::check(account_);return completedLocationFor(account_,server_,id);}
 Blob completedLocation(const std::string& id)const{auto p=jobs_.find(id);protocol::check(account_&&p!=jobs_.end()&&p->second->verified&&p->second->server==server_&&(p->second->sending?p->second->account:p->second->recipient)==account_);return p->second->outputToken;}
 // nullopt is ordinary mailbox text; an empty Blob is an attachment pending
 // authenticated context. Malformed or unsupported typed bodies fail closed.
 std::optional<Blob> dispatchNotice(const Blob& wire){auto envelope=mailbox::parse(wire);bool attachment=false;keys_(envelope.header.keyId,[&](auto& hpke,auto&,const auto& local,const auto& handle){auto evidence=evidence_();protocol::check(bool(evidence.identity));auto sender=evidence.identity(envelope.header.senderAccount,envelope.header.senderDevice);SecretRecord plain(mailbox::open(envelope,local,envelope.header.senderAccount,sender,verifyDevicePayload,Storage::publicKeyId,handle,hpke,io_.wall()));mailbox::Writer prefix("PeerChat/mailbox-text/v1");if(plain.bytes.size()>=prefix.bytes.size()&&std::equal(prefix.bytes.begin(),prefix.bytes.end(),plain.bytes.begin())){mailbox::parseTextBody(plain.bytes);return;}protocol::Reader r(plain.bytes);protocol::check(r.type()==protocol::FileTransfer);auto domain=r.string();protocol::check(domain=="PeerChat/dm-attachment-private/v1"||domain=="PeerChat/dm-attachment-private/v2");attachment=true;});if(!attachment)return std::nullopt;return receiveNotice(wire);}
 // A mailbox ACK confirms only receipt of the encrypted offer. It must never
 // mark the file verified or replace the separate cache verification receipt.
 bool acknowledgeNoticeReceipt(const Blob& wire,const Contact& recipient){
  auto ack=mailbox::parseReceipt(wire);
  if(!account_||ack.senderDevice!=identity_.peerId||!recipient.verified||recipient.peerId!=ack.recipientDevice)return false;
  auto validate=[&](const Job& j){
   if(!j.sending||j.message!=ack.messageId||j.server!=server_||j.account!=account_||j.recipient!=ack.recipientAccount)return false;
   for(const auto& target:j.recipients){auto h=mailbox::parse(target.notice).header;
    if(h.recipientDevice==ack.recipientDevice&&h.senderAccount==account_&&h.senderDevice==identity_.peerId&&h.recipientAccount==ack.recipientAccount&&h.messageId==ack.messageId)
     return mailbox::verifyReceipt(ack,j.recipient,h.recipientDevice,identity_.peerId,j.message,digest(target.notice),recipient.publicKey,verifyDevicePayload,Storage::publicKeyId);
   }return false;
  };
  for(const auto& item:jobs_)if(item.second->message==ack.messageId)return validate(*item.second);
  // Inspect protected history one record at a time, without populating the
  // active-job cache or reopening unrelated sources. This also covers v1 heads.
  auto inspect=[&](const std::string& id){
   if(jobs_.count(id))return false;
   SecretRecord bytes(storage_.directAttachmentJob(identity_.peerId,id));protocol::check(!bytes.bytes.empty()&&bytes.bytes.size()<=900*1024);
   protocol::Reader prefix(bytes.bytes);protocol::check(prefix.type()==protocol::FileTransfer&&prefix.string()=="PeerChat/DMCacheJob/v2"&&prefix.string()==identity_.peerId&&prefix.string()==id);
   if(prefix.string()!=ack.messageId)return false;
   protocol::Reader full(bytes.bytes);full.type();full.string();full.string();auto saved=readJob(full,true);full.end();return validate(*saved);
  };
  for(const auto& entry:openIndex_)if(inspect(entry.second))return true;
  auto pageId=previousPage_;while(!pageId.empty()){auto page=readPage(pageId);for(const auto& entry:page.entries)if(inspect(entry.second))return true;pageId=page.previous;}
  return false;
 }
 std::unique_ptr<transfer::Source> reopen(const transfer::DurableState& d){auto& j=job(d.metadata.id);protocol::check(d.purpose==transfer::Purpose::DirectCacheCiphertext&&j.sending&&metadata(j)==d.metadata);return io_.reopen(j.cipherToken,d.metadata);}
 std::uint64_t route(const transfer::Snapshot& s)const{auto p=jobs_.find(s.metadata.id);return s.purpose==transfer::Purpose::DirectCacheCiphertext&&p!=jobs_.end()&&p->second->server==server_&&account_&&(p->second->sending?p->second->account:p->second->recipient)==account_&&!p->second->lease.empty()&&(!p->second->sending||p->second->earlyBlocked||allStored(*p->second))?p->second->route:0;}
 size_t appendCredit(const transfer::Snapshot& s)const{
  if(!s.sending||!route(s))return 0;
  size_t pending=0,reclaimable=0;
  for(const auto& q:requests_){if(q.job==s.metadata.id){if(q.operation!="cache_dm_append")return 0;++pending;}if(!q.dispatched&&q.operation=="cache_dm_receipts")++reclaimable;}
  return pending>=8?0:std::min(size_t(8)-pending,size_t(24)-requests_.size()+reclaimable);
 }
 bool send(const transfer::Snapshot& s,const Blob& frame){if(!route(s)||(requests_.size()>=24&&std::none_of(requests_.begin(),requests_.end(),[](const auto& q){return !q.dispatched&&q.operation=="cache_dm_receipts";})))return false;auto& j=job(s.metadata.id);protocol::Reader r(frame);protocol::check(r.type()==protocol::FileTransfer);auto version=r.number();auto kind=static_cast<transfer::Kind>(r.number());protocol::check(r.string()==j.id);auto negotiation=version==2?r.number():0;
  // Reuse v2 credit, but bound cache uploads to eight ordered append requests.
  // Control/lease/finalization work must still wait for the outstanding window.
  if(busy(j.id)){
   if(!j.sending||version!=2||kind!=transfer::Kind::Chunk)return false;
   unsigned pending=0;for(const auto& q:requests_)if(q.job==j.id){if(q.operation!="cache_dm_append")return false;++pending;}
   if(pending>=8)return false;
  }
  j.version=version;j.negotiation=negotiation;
  if(j.sending){if(kind==transfer::Kind::Offer){enqueue(&j,"cache_dm_status",0,0,{},frame);return true;}if(kind==transfer::Kind::Chunk){auto offset=r.number();auto data=r.bytes();r.end();enqueue(&j,"cache_dm_append",offset,0,std::move(data),frame);return true;}if(kind==transfer::Kind::Finish){enqueue(&j,j.uploaded?"cache_dm_status":"cache_dm_complete",0,0,{},frame);return true;}}
  else {if(kind==transfer::Kind::Resume){auto offset=r.number();r.end();enqueue(&j,"cache_dm_progress",offset);return true;}if(kind==transfer::Kind::Complete){j.state="verifying";save();return true;}}
  if(kind==transfer::Kind::Cancel){enqueue(&j,"cache_dm_release",0,0,j.sending&&j.state=="canceled"?Blob{1}:Blob{});return true;}return false;
 }
 Blob nextCommand(){
  // Preserve FIFO within payload/control work and within idle receipt reads.
  // A retained history of unverified files must not delay a fresh prepare.
  auto next=std::find_if(requests_.begin(),requests_.end(),[](const auto& q){return !q.dispatched&&q.operation!="cache_dm_receipts";});
  if(next==requests_.end()){
   // An empty command lets the existing local-work continuation advance a
   // sealer instead of waiting for an unrelated historical receipt RPC.
   if(localPreparationPending())return {};
   next=std::find_if(requests_.begin(),requests_.end(),[](const auto& q){return !q.dispatched;});
  }
  if(next==requests_.end())return {};
  auto& q=*next;protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/DMCacheCommand/v1");w.number(q.generation);w.string(q.id);w.string(q.operation);Job* j=q.job.empty()?nullptr:&job(q.job);w.number(j?j->recipient:0);w.string(j?j->id:"");w.string(j?j->lease:"");w.number(q.offset);w.number(q.maximum);w.number(j&&j->sending);w.bytes(j?j->descriptor:Blob{});w.bytes(j?j->audience:Blob{});w.bytes(q.data);protocol::check(w.data.size()<=100000);q.dispatched=true;q.sentAt=io_.wall();return w.data;
 }
 // Empty result means pending authenticated server context: NO mailbox ACK.
 // EvidenceLookup.current supplies current device/key/friend mapping. Only an
 // exact, recent response on this session proves the relationship incarnation.
 Blob receiveNotice(const Blob& wire){protocol::check(account_&&supported_);auto envelope=mailbox::parse(wire);protocol::check(envelope.header.recipientAccount==account_&&envelope.header.recipientDevice==identity_.peerId);std::unique_ptr<Body> body;keys_(envelope.header.keyId,[&](auto& hpke,auto&,const auto& local,const auto& handle){auto evidence=evidence_();protocol::check(evidence.identity&&evidence.current);auto sender=evidence.identity(envelope.header.senderAccount,envelope.header.senderDevice);SecretRecord plain(mailbox::open(envelope,local,envelope.header.senderAccount,sender,verifyDevicePayload,Storage::publicKeyId,handle,hpke,io_.wall()));body=std::make_unique<Body>(decodeStoredBody(plain.bytes,envelope.header));auto a=parseAudience(body->audience,envelope.header.senderAccount,envelope.header.senderDevice,envelope.header.recipientAccount);validateReceivedBody(*body,envelope.header,evidence);auto target=std::find_if(a.recipients.begin(),a.recipients.end(),[&](const auto& b){return mailbox::serializeBinding(b)==mailbox::serializeBinding(local);});protocol::check(target!=a.recipients.end()&&evidence.current(*target,a.relationship,envelope.header.senderAccount));});protocol::check(bool(body));auto id=body->manifest.binding.attachment;if(!jobs_.count(id)&&!legacyPreview_){SecretRecord existing(storage_.directAttachmentJob(identity_.peerId,id));if(!existing.bytes.empty())loadJob(id);}auto old=jobs_.find(id);if(old!=jobs_.end()){auto& j=*old->second;protocol::check(!j.sending&&j.server==server_&&j.envelope==wire);if(j.contextUntil>io_.wall()&&!j.noticeAck.empty())return j.noticeAck;if(!busy(j.id))enqueue(&j,"cache_dm_context");return {};}room();for(const auto& item:jobs_)protocol::check(item.second->message!=envelope.header.messageId);
  auto encoded=encodeProtectedBody(*body,envelope.header);auto j=std::make_unique<Job>();j->id=id;j->message=envelope.header.messageId;j->peer=envelope.header.senderDevice;j->server=server_;j->account=envelope.header.senderAccount;j->recipient=account_;j->time=body->time;j->order=body->order;j->state="available";j->audience=body->audience;j->descriptor=body->descriptor;j->body=std::move(body);j->serializedBody.bytes.swap(encoded.bytes);j->envelope=wire;
  j->state="awaiting_context";jobs_.emplace(id,std::move(j));try{save();}catch(...){jobs_.erase(id);throw;}enqueue(&job(id),"cache_dm_context");return {};
 }
 std::vector<Snapshot> snapshotsFor(std::uint64_t account,const std::string& server,const std::string& peer={})const{protocol::check(!account||(mailbox::account(account)&&protocol::id(server)&&(peer.empty()||protocol::id(peer))));std::vector<Snapshot> out;auto transfers=transfers_.snapshots();for(const auto& item:jobs_){const auto& j=*item.second;if(!account||(!j.sending&&j.noticeAck.empty())||(j.sending?j.account:j.recipient)!=account||j.server!=server||(!peer.empty()&&j.peer!=peer))continue;Snapshot s{j.id,j.message,j.peer,j.body?j.body->manifest.name:j.input?j.input->name():!j.sourceName.empty()?j.sourceName:"Encrypted attachment",j.state,j.error,j.sending,j.verified,j.uploaded,allStored(j),j.body?j.body->manifest.size:j.input?j.input->size():j.sourceSize,0,j.time,j.order};if(j.body)s.thumbnail=j.body->thumbnail;if(j.verified||j.uploaded)s.done=s.size;for(const auto& t:transfers)if(t.metadata.id==j.id&&j.body)s.done=std::min(s.size,t.done>group_attachments::HeaderBytes?(t.done-group_attachments::HeaderBytes)*s.size/std::max<std::uint64_t>(1,t.metadata.size-group_attachments::HeaderBytes):0);out.push_back(std::move(s));}return out;}
 std::vector<Snapshot> snapshots()const{return snapshotsFor(account_,server_);}
#if defined(__ANDROID__)
 // Explicit foreground lifetime follows protocol work even when its local
 // timeline entry is hidden. This query never starts or restores a job.
 bool hasActiveTransfer(const std::string& id)const{auto found=jobs_.find(id);return found!=jobs_.end()&&(active(*found->second)||busy(id));}
#endif

 // Bounded metadata only: no filenames, paths, keys, envelopes or payloads.
 // Include preflight jobs which do not yet exist in TransferManager metrics.
 std::vector<std::string> diagnostics()const{
  std::vector<const Job*> activeJobs;for(const auto& item:jobs_){const auto& j=*item.second;if(j.server==server_&&(j.sending?j.account:j.recipient)==account_&&active(j))activeJobs.push_back(&j);}
  std::sort(activeJobs.begin(),activeJobs.end(),[](const Job* a,const Job* b){return a->time>b->time;});
  std::vector<std::string> rows;const auto now=io_.wall();
  unsigned queuedReceipts=0,inflightReceipts=0,queuedWork=0,inflightWork=0;std::uint64_t oldestQueued=0;
  for(const auto& q:requests_){auto& count=q.operation=="cache_dm_receipts"?(q.dispatched?inflightReceipts:queuedReceipts):(q.dispatched?inflightWork:queuedWork);++count;if(!q.dispatched&&q.queuedAt&&now>=q.queuedAt)oldestQueued=std::max(oldestQueued,now-q.queuedAt);}
  rows.push_back("cache_queue queued_work="+std::to_string(queuedWork)+" inflight_work="+std::to_string(inflightWork)+" queued_receipts="+std::to_string(queuedReceipts)+" inflight_receipts="+std::to_string(inflightReceipts)+" oldest_queued_age_ms="+std::to_string(oldestQueued)+" body_encode_calls="+std::to_string(bodyEncodingCalls_)+" cached_body_reuses="+std::to_string(bodyReuseCalls_)+" local_prep_calls="+std::to_string(localPreparationCalls_)+" local_prep_steps_without_full_tick="+std::to_string(localPreparationSteps_)+" local_hash_steps_without_full_tick="+std::to_string(localHashPreparationSteps_)+" full_tick_calls="+std::to_string(fullTickCalls_));
  for(const auto* j:activeJobs){if(rows.size()>=16)break;unsigned queued=0,inflight=0;std::uint64_t age=0;std::string operation="none";
   for(const auto& q:requests_)if(q.job==j->id){if(q.dispatched){++inflight;age=std::max(age,now>=q.sentAt?now-q.sentAt:0);}else ++queued;if(operation=="none")operation=q.operation;}
   rows.push_back("cache_transfer="+j->id+" phase="+j->state+" supported="+std::to_string(supported_)+" sealing="+std::to_string(bool(j->sealer))+" preparation_calls="+std::to_string(j->preparationCalls)+" preparation_work_us="+std::to_string(j->preparationMicros)+" preparation_bytes="+std::to_string(j->sealer?j->sealer->processedBytes():j->body?j->body->manifest.size:0)+" metadata_ready="+std::to_string(bool(j->body))+" admitted="+std::to_string(j->admitted)+" upload_complete="+std::to_string(j->uploaded)+" queued="+std::to_string(queued)+" inflight="+std::to_string(inflight)+" request="+operation+" request_age_ms="+std::to_string(age)+" retry_in_ms="+std::to_string(j->retry>now?j->retry-now:0));
  }return rows;
 }

 void eraseLocalHistory(const std::string& peer,const std::string& id={}){
  protocol::check(protocol::id(peer));
  if(!id.empty()){
   storage_.eraseMessage(peer,id);if(!jobs_.count(id)){SecretRecord saved(storage_.directAttachmentJob(identity_.peerId,id));if(!saved.bytes.empty())loadJob(id);}
   auto found=jobs_.find(id);if(found!=jobs_.end()&&found->second->peer==peer&&!found->second->message.empty())storage_.eraseMessage(peer,found->second->message);return;
  }
  auto hide=[&](const std::string& object){if(!jobs_.count(object))loadJob(object);auto& j=job(object);if(j.peer==peer){storage_.eraseMessage(peer,j.id);if(!j.message.empty())storage_.eraseMessage(peer,j.message);}};
  for(const auto& entry:openIndex_)hide(entry.second);
  auto pageId=previousPage_;std::set<std::string> seen;
  while(!pageId.empty()){protocol::check(seen.insert(pageId).second);auto page=readPage(pageId);for(const auto& entry:page.entries)hide(entry.second);pageId=page.previous;}
  for(const auto& pair:jobs_)if(pair.second->peer==peer){storage_.eraseMessage(peer,pair.first);if(!pair.second->message.empty())storage_.eraseMessage(peer,pair.second->message);}
 }
 HistoryPage historyFor(std::uint64_t account,const std::string& server,const std::string& cursor={},size_t limit=32,const std::string& peer={}){
  protocol::check(mailbox::account(account)&&protocol::id(server)&&limit&&limit<=32&&(peer.empty()||protocol::id(peer)));std::string pageId;size_t offset=0;
  if(cursor.empty()){
   auto entries=openIndex_;auto previous=previousPage_;if(legacyPreview_){entries.clear();for(const auto& item:jobs_)entries.push_back({item.second->time,item.first});std::sort(entries.begin(),entries.end());}
   sealPages(entries,previous);pageId=entries.empty()?previous:storePage(entries,previous);if(pageId.empty())return {};
  }else {protocol::check(cursor.size()>=66&&cursor.size()<=68&&cursor[64]==':'&&protocol::id(cursor.substr(0,64)));pageId=cursor.substr(0,64);auto number=cursor.substr(65);protocol::check(!number.empty()&&(number=="0"||number.front()!='0'));for(char c:number){protocol::check(c>='0'&&c<='9');offset=offset*10+static_cast<unsigned>(c-'0');}protocol::check(offset<=128);}
  auto page=readPage(pageId);protocol::check(offset<=page.entries.size());HistoryPage result;size_t inspected=0;while(offset<page.entries.size()&&inspected++<limit){auto id=page.entries[page.entries.size()-1-offset++].second;if(!jobs_.count(id))loadJob(id);auto visible=snapshotsFor(account,server,peer);auto row=std::find_if(visible.begin(),visible.end(),[&](const auto& s){return s.id==id&&(peer.empty()||s.peer==peer);});if(row!=visible.end()&&!storage_.localMessageHidden(row->peer,row->id)&&(row->message.empty()||!storage_.localMessageHidden(row->peer,row->message)))result.items.push_back(std::move(*row));}
  std::sort(result.items.begin(),result.items.end(),[](const auto& a,const auto& b){return std::tie(a.time,a.id)>std::tie(b.time,b.id);});
  result.more=offset<page.entries.size()||!page.previous.empty();if(result.more)result.cursor=offset<page.entries.size()?pageId+":"+std::to_string(offset):page.previous+":0";return result;
 }
 HistoryPage history(const std::string& cursor={},size_t limit=32,const std::string& peer={}){protocol::check(account_);return historyFor(account_,server_,cursor,limit,peer);}
 void tick();void reply(const Blob&);bool replyWindow(const std::vector<Blob>&);void failure(const Blob&);
};

inline void Application::tick(){
 ++fullTickCalls_;reconcileTerminalTransfers();
 if(!account_)return;
 auto now=io_.wall();
 std::vector<Request> timedOut;for(auto it=requests_.begin();it!=requests_.end();){if(it->dispatched&&now>=it->sentAt&&now-it->sentAt>=30000){timedOut.push_back(std::move(*it));it=requests_.erase(it);}else ++it;}for(const auto& q:timedOut){if(q.job.empty()){supported_=false;capability_=0;probeAt_=now;}else if(!canceledResult(job(q.job),q,false))pause(job(q.job),"Cache request timed out; durable attachment data retained");}
 if(!supported_){if(now>=probeAt_&&!busy("")){enqueue(nullptr,"cache_dm_capability");probeAt_=now+30000;}return;}
 // One snapshot avoids rebuilding all transfer rows for every historical job.
 // Receipt polling yields only to work that is currently eligible; paused
 // retries and unaccepted downloads cannot defer it indefinitely.
 std::set<std::string> handshakes;for(const auto& row:transfers_.snapshots())if(row.purpose==transfer::Purpose::DirectCacheCiphertext&&row.sending&&row.state!=transfer::State::Complete&&row.state!=transfer::State::Cancelled&&row.state!=transfer::State::Failed)handshakes.insert(row.metadata.id);
 bool productive=std::any_of(requests_.begin(),requests_.end(),[](const auto& q){return q.operation!="cache_dm_receipts";});
 if(!productive)productive=std::any_of(jobs_.begin(),jobs_.end(),[&](const auto& item){const auto& j=*item.second;return j.server==server_&&(j.sending?j.account:j.recipient)==account_&&now>=j.retry&&j.state!="failed"&&j.state!="canceled"&&!terminalCacheError(j)&&(j.sending?(!j.uploaded||!allStored(j)||handshakes.count(j.id)):(j.accepted&&!j.verified));});
 // One local encryption/decryption block globally per worker tick.
 std::vector<Job*> localWork;for(auto& item:jobs_){auto& j=*item.second;if(j.server==server_&&(j.sending?j.account:j.recipient)==account_&&((!terminalCacheError(j)&&(j.sealer||j.state=="verifying"))||(j.verified&&!j.outputNotified&&io_.verified)))localWork.push_back(&j);}
 if(!localWork.empty()){auto& j=*localWork[turn_++%localWork.size()];
  localStep(j);
 }
  for(auto& item:jobs_){auto& j=*item.second;if(requests_.size()>=24)break;
   if((j.state=="canceled"||(j.state=="failed"&&!j.body)||(!j.sending&&j.verified&&!j.outputToken.empty()))&&!j.cipherToken.empty()&&io_.discardCipher)try{
    transfers_.forgetCacheCiphertext(j.peer,j.id,transfer::Purpose::DirectCacheCiphertext);
    auto length=j.sourceSize;transfer::Metadata m=j.body?metadata(j):transfer::Metadata{j.id,"ciphertext.peerchat",group_attachments::HeaderBytes+length+std::max<std::uint64_t>(1,(length+group_attachments::BlockBytes-1)/group_attachments::BlockBytes)*group_attachments::TagBytes,{}};
    io_.discardCipher(j.cipherToken,m);j.cipherToken.clear();save();
   }catch(...){j.error=j.state=="canceled"?"Canceled; local encrypted file cleanup will retry":j.state=="failed"?"Preparation failed; encrypted temporary file cleanup will retry":"Verified file retained; encrypted temporary file cleanup will retry";}
   if(j.state=="canceled"&&j.server==server_&&(j.sending?j.account:j.recipient)==account_&&now>=j.retry&&!busy(j.id)){
    if(!j.lease.empty())enqueue(&j,"cache_dm_release",0,0,j.sending?Blob{1}:Blob{});
    else if(j.sending&&j.admitted&&!j.uploaded)enqueue(&j,"cache_dm_status");
    continue;
   }if(j.server!=server_||now<j.retry||busy(j.id)||j.state=="failed"||j.state=="canceled"||terminalCacheError(j)||j.sealer)continue;if((j.sending?j.account:j.recipient)!=account_)continue;
   // Retained terminal rows have no network work. Output notification is
   // handled separately above, and queued release replies remain independent.
   if((j.sending&&j.state=="complete"&&!j.recipients.empty()&&std::all_of(j.recipients.begin(),j.recipients.end(),[](const auto& recipient){return recipient.verified;}))||(!j.sending&&j.verified&&j.published))continue;
   // The current authority checks below still run whenever a receipt refresh
   // is actually issued, and every returned signed receipt is fully validated.
   if(productive&&j.sending&&j.uploaded&&allStored(j)&&!handshakes.count(j.id))continue;
  try{
   if(!j.body){if(j.sending&&j.input)enqueue(&j,"cache_dm_prepare");continue;}
   // A validated offer awaiting user acceptance has no work while its server
   // context is fresh. Rechecking its signatures on every relay/cache tick
   // held the shared native mutex even though no bytes could be transferred.
   // Acceptance still runs the full current-evidence checks below; expiry still
   // triggers the normal context refresh.
   if(!j.sending&&!j.accepted&&!j.noticeAck.empty()&&j.contextUntil>now)continue;
   auto evidence=evidence_();if(j.sending){auto a=parseAudience(j.audience,j.account,identity_.peerId,j.recipient);protocol::check(evidence.identity&&evidence.current);verifyAudience(a,evidence.identity);protocol::check(evidence.current(a.source,a.relationship,j.recipient));}else {if(j.contextUntil<=now){enqueue(&j,"cache_dm_context");continue;}auto authorized=authorizedEvidence(j);validateReceivedBody(*j.body,header(j),authorized);}
   if(j.sending){
    if(j.noticeDeadline<=now&&!allStored(j)){j.state="failed";j.error="Attachment notice deadline expired before every device stored its notice";save();continue;}
    if(j.admitted&&!j.earlyBlocked&&!allStored(j)){
     auto early=std::find_if(j.recipients.begin(),j.recipients.end(),[](const auto& p){return !p.stored;});enqueue(&j,"cache_dm_publish",0,0,early->notice);continue;
    }
    if(!j.uploaded||handshakes.count(j.id)){if(!j.route)enqueue(&j,"cache_dm_status");continue;}
    auto next=std::find_if(j.recipients.begin(),j.recipients.end(),[](const auto& p){return !p.stored;});if(next!=j.recipients.end()){enqueue(&j,"cache_dm_publish",0,0,next->notice);continue;}
    if(!std::all_of(j.recipients.begin(),j.recipients.end(),[](const auto& p){return p.verified;})){enqueue(&j,"cache_dm_receipts",0,0,Blob(j.cursor.begin(),j.cursor.end()));j.retry=now+5000;}
   }else if(j.verified){if(!j.published)enqueue(&j,"cache_dm_verified",0,0,j.verifiedAck);}
   else if(j.accepted&&j.state!="verifying"){
    if(j.lease.empty()){enqueue(&j,"cache_dm_context");continue;}
    auto rows=transfers_.snapshots();auto row=std::find_if(rows.begin(),rows.end(),[&](const auto& s){return s.metadata.id==j.id;});if(row==rows.end()||transfers_.hasPendingControl(j.id))continue;
    if(row->state==transfer::State::Complete){j.state="verifying";save();continue;}
    if(row->state==transfer::State::Transferring&&row->done==j.readOffset&&row->done<row->metadata.size)enqueue(&j,"cache_dm_read",row->done,std::min<std::uint64_t>(transfer::ExtendedChunkBytes,row->metadata.size-row->done));
   }
  }catch(...){pause(j,"Attachment authorization or protected storage unavailable");}
 }
}

inline void Application::reply(const Blob& wire){
 auto [generation,id,state,cacheVersion,dmVersion,offset,size,expires,complete,lease,descriptorWire,audience,data,newNotice,rows,cursor,more,bodyVersion]=decodeReply(wire);
 if(generation!=generation_||!account_)return;
 auto found=std::find_if(requests_.begin(),requests_.end(),[replyId=id](const auto& q){return q.id==replyId&&q.dispatched;});if(found==requests_.end())return;auto q=std::move(*found);requests_.erase(found);
 if(q.operation=="cache_dm_capability"){
  supported_=false;capability_=0;probeAt_=io_.wall()+10000;if(state!="accepted"||cacheVersion!=2||dmVersion!=1){if(state=="unsupported_request"||state=="cache_unsupported_operation"||state=="cache_dm_feature_gated"||state=="cache_dm_unsupported"||(state=="accepted"&&(cacheVersion!=2||dmVersion!=1)))capability_=2;return;}
  registrationBodyTarget_=bodyVersion;registerBodyCapability(1);return;
 }
 if(q.operation=="cache_dm_register"){
  const bool accepted=state=="accepted"&&cacheVersion==2&&dmVersion==1;
  protocol::Reader proof(q.data);protocol::check(proof.type()==protocol::FileTransfer&&proof.string()=="PeerChat/dm-cache-device/v1");proof.number();proof.string();proof.string();proof.number();auto registered=proof.number();protocol::check(registered==1||registered==2);
  if(accepted&&registered==1&&registrationBodyTarget_==2){supported_=false;capability_=0;registerBodyCapability(2);return;}
  supported_=accepted;capability_=supported_?1:0;probeAt_=io_.wall()+10000;return;
 }
 auto& j=job(q.job);if(j.server!=server_)return;if(canceledResult(j,q,state=="accepted",lease,state))return;
 // Fixed local labels only: never include provider text, keys or payload data.
 const char* stage="reply_validation";
 try{
  if(q.operation=="cache_dm_context"||q.operation=="cache_dm_prepare")protocol::check(io_.wall()>=q.sentAt&&io_.wall()-q.sentAt<30000);
  if(state!="accepted"){
   if(state=="cache_upload_incomplete"&&q.operation=="cache_dm_publish"&&j.sending){j.earlyBlocked=true;j.retry=0;return;}
   if(state=="cache_upload_incomplete"&&!j.sending){j.error.clear();j.retry=io_.wall()+1000;return;}
   if(state=="cache_lease_expired"||state=="cache_lease_invalid"){j.lease.clear();j.route=0;save();}
   if(q.operation=="cache_dm_status"&&!j.admitted&&state=="cache_attachment_unavailable"){enqueue(&j,"cache_dm_admit");return;}
   if(state=="cache_attachment_expired"||state=="cache_attachment_unavailable"){cacheUnavailable(j,state);return;}
   pause(j,state.size()<=96?state:"Cache rejected attachment operation");return;
  }
  protocol::check(cacheVersion==2&&dmVersion==1);if(j.body&&size)protocol::check(size==metadata(j).size);if(!descriptorWire.empty()&&j.body)protocol::check(descriptorWire==j.descriptor);if(!audience.empty()&&j.body)protocol::check(audience==j.audience);
  if(q.operation=="cache_dm_prepare"){stage="prepare_fields";protocol::check(!j.body&&j.input&&!audience.empty()&&expires>io_.wall()/1000);stage="prepare_audience_consistency";protocol::check(j.audience.empty()||j.audience==audience);auto selected=preparedAudience(j,audience,stage);if(selected!=audience){j.audience=std::move(selected);j.contextUntil=0;j.state="preflight";j.error.clear();j.retry=0;stage="prepare_selection_save";save();enqueue(&j,"cache_dm_prepare");return;}j.bodyVersion=bodyVersion;j.noticeDeadline=expires*1000;j.audience=std::move(audience);j.contextUntil=io_.wall()+30000;stage="prepare_save";save();beginSeal(j,stage);return;}
  if(q.operation=="cache_dm_admit"){j.admitted=true;save();enqueue(&j,"cache_dm_acquire");return;}
  if(q.operation=="cache_dm_status"){
   protocol::check(size==metadata(j).size&&offset<=size);j.admitted=true;if(!q.frame.empty()){protocol::Reader frame(q.frame);frame.number();auto kind=static_cast<transfer::Kind>(frame.number());if(kind==transfer::Kind::Finish){protocol::check(complete);receive(j,control(j,transfer::Kind::Complete));}else receive(j,control(j,transfer::Kind::Resume,offset));return;}if(complete){j.uploaded=true;j.state="notice_pending";save();if(uploadHandshake(j)){if(j.lease.empty())enqueue(&j,"cache_dm_acquire");else activate(j);}return;}save();if(j.lease.empty())enqueue(&j,"cache_dm_acquire");else activate(j);return;
  }
  if(q.operation=="cache_dm_context"){protocol::check(descriptorWire==j.descriptor&&audience==j.audience&&size==metadata(j).size&&expires>io_.wall()/1000);j.contextUntil=std::min<std::uint64_t>(io_.wall()+30000,expires*1000);auto evidence=authorizedEvidence(j);validateReceivedBody(*j.body,header(j),evidence);if(j.noticeAck.empty())acknowledgeNotice(j);if(!complete){j.contextUntil=std::min<std::uint64_t>(j.contextUntil,io_.wall()+1000);j.retry=io_.wall()+1000;j.error.clear();if(j.accepted)j.state="waiting_for_upload";save();return;}if(j.accepted&&!j.verified&&!j.route){if(j.lease.empty())enqueue(&j,"cache_dm_acquire");else activate(j);}return;}
  if(q.operation=="cache_dm_acquire"){protocol::check(protocol::id(lease,32));j.lease=lease;if(!j.sending&&!j.verified)j.state="downloading";save();activate(j);return;}
  if(q.operation=="cache_dm_append"){protocol::check(offset==q.offset+q.data.size());receive(j,control(j,transfer::Kind::Resume,offset));return;}
  if(q.operation=="cache_dm_complete"){protocol::check(size==metadata(j).size);if(!complete){enqueue(&j,"cache_dm_complete");return;}receive(j,control(j,transfer::Kind::Complete));j.uploaded=true;j.state="notice_pending";save();return;}
  if(q.operation=="cache_dm_read"){protocol::check(!data.empty()&&data.size()<=q.maximum&&q.offset+data.size()<=metadata(j).size);protocol::Writer frame(protocol::FileTransfer);frame.data=control(j,transfer::Kind::Chunk);frame.number(q.offset);frame.bytes(data);receive(j,frame.data);return;}
  if(q.operation=="cache_dm_progress"){protocol::check(offset==q.offset);j.readOffset=offset;if(offset==metadata(j).size)receive(j,control(j,transfer::Kind::Finish));return;}
  if(q.operation=="cache_dm_publish"){auto p=std::find_if(j.recipients.begin(),j.recipients.end(),[&](const auto& x){return x.notice==q.data;});protocol::check(p!=j.recipients.end());p->stored=true;j.state=j.uploaded?(allStored(j)?"available":"notice_pending"):"uploading";save();return;}
  if(q.operation=="cache_dm_verified"){j.published=true;j.error.clear();save();if(!j.lease.empty())enqueue(&j,"cache_dm_release");return;}
  if(q.operation=="cache_dm_release"){j.lease.clear();j.route=0;save();return;}
  if(q.operation=="cache_dm_receipts"){
   for(const auto& row:rows){auto p=std::find_if(j.recipients.begin(),j.recipients.end(),[&](const auto& x){auto h=mailbox::parse(x.notice).header;return h.recipientAccount==row.account&&h.recipientDevice==row.device;});protocol::check(p!=j.recipients.end()&&row.done<=metadata(j).size);p->done=row.done;if(row.verified){auto receipt=parseReceipt(row.receipt);auto h=mailbox::parse(p->notice).header;auto copy=decodeStoredBody(encodeBody(*j.body,header(j)).bytes,header(j));copy.mailboxBinding=mailbox::headerPayload(h);protocol::check(receipt.verifiedUtc<=io_.wall()+300000&&verifyReceipt(receipt,copy,h,evidence_()));p->receipt=row.receipt;p->verified=true;}}
   j.cursor=more?cursor:"";if(std::all_of(j.recipients.begin(),j.recipients.end(),[](const auto& p){return p.verified;}))j.state="complete";save();if(more){j.retry=0;enqueue(&j,"cache_dm_receipts",0,0,Blob(cursor.begin(),cursor.end()));}return;
  }
  protocol::check(false);
 }catch(...){pause(j,std::string("Attachment response failed validation or protected storage: ")+stage);throw;}
}
inline bool Application::replyWindow(const std::vector<Blob>& wires){
 protocol::check(!wires.empty()&&wires.size()<=8);size_t total=0;std::vector<Reply> replies;replies.reserve(wires.size());
 for(const auto& wire:wires){total+=wire.size();protocol::check(wire.size()<=8192&&total<=70000);replies.push_back(decodeReply(wire));}
 std::string selected;std::vector<const Request*> pending;std::set<std::string> ids;pending.reserve(replies.size());
 for(const auto& reply:replies){
  if(!account_||reply.generation!=generation_||reply.state!="accepted")return false;
  auto found=std::find_if(requests_.begin(),requests_.end(),[&](const auto& q){return q.id==reply.id&&q.dispatched;});if(found==requests_.end()||found->operation!="cache_dm_append")return false;
  if(selected.empty())selected=found->job;else if(selected!=found->job)return false;
  protocol::check(ids.insert(reply.id).second);pending.push_back(&*found);
 }
 auto& j=job(selected);if(j.server!=server_||j.account!=account_||!j.sending||!j.body||j.state=="canceled"||terminalCacheError(j)||!j.route||j.lease.empty()||j.version!=2)return false;
 try{
  std::vector<Blob> frames;frames.reserve(replies.size());
  for(size_t index=0;index<replies.size();++index){const auto& reply=replies[index];const auto& q=*pending[index];
   protocol::check(q.generation==generation_&&reply.cacheVersion==2&&reply.dmVersion==1&&(!reply.size||reply.size==metadata(j).size)&&reply.offset==q.offset+q.data.size());
   protocol::check((reply.descriptorWire.empty()||reply.descriptorWire==j.descriptor)&&(reply.audience.empty()||reply.audience==j.audience));
   protocol::Reader frame(q.frame);protocol::check(frame.type()==protocol::FileTransfer&&frame.number()==2&&frame.number()==static_cast<unsigned>(transfer::Kind::Chunk)&&frame.string()==j.id&&frame.number()==j.negotiation&&frame.number()==q.offset&&frame.bytes()==q.data);frame.end();
   frames.push_back(control(j,transfer::Kind::Resume,reply.offset));
  }
  transfers_.receiveDirectCacheResumeWindow(j.peer,metadata(j),j.body->manifest.size,frames,io_.monotonic());
 }catch(...){pause(j,"Attachment response failed validation or protected storage: append_window");throw;}
 // No request or observable progress leaves this owner before journal commit.
 requests_.erase(std::remove_if(requests_.begin(),requests_.end(),[&](const auto& q){return ids.count(q.id)!=0;}),requests_.end());return true;
}
inline void Application::failure(const Blob& wire){
 protocol::Reader r(wire);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/DMCacheFailure/v1");auto generation=r.number();auto id=r.string();protocol::check(r.string()=="network_unavailable");r.end();if(generation!=generation_||!account_)return;auto found=std::find_if(requests_.begin(),requests_.end(),[replyId=id](const auto& q){return q.id==replyId&&q.dispatched;});if(found==requests_.end())return;auto q=std::move(*found);requests_.erase(found);if(q.job.empty()){supported_=false;capability_=0;probeAt_=io_.wall()+10000;}else if(!canceledResult(job(q.job),q,false))pause(job(q.job),"Cache connection unavailable; durable attachment data retained");
}
}
