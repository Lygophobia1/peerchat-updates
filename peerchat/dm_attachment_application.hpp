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
 };
 using EvidenceLookup=std::function<Evidence()>;
 using KeyAccess=std::function<void(const std::string&,const std::function<void(platform::HpkeProvider&,platform::IdentityProvider&,const mailbox::RecipientBinding&,const std::shared_ptr<platform::RecipientKeyHandle>&)>&)>;
 struct Snapshot {std::string id,message,peer,name,state,error;bool sending=false,verified=false,uploadComplete=false,noticesStored=false;std::uint64_t size=0,done=0,time=0,order=0;};
 struct HistoryPage {std::vector<Snapshot> items;std::string cursor;bool more=false;};
private:
 struct Recipient {Blob notice,receipt;bool stored=false,verified=false;std::uint64_t done=0;};
 struct Job {
  std::string id,message,peer,server,state="preflight",error,lease,cursor;std::uint64_t account=0,recipient=0,time=0,order=0,route=0,version=2,negotiation=0,readOffset=0,retry=0,contextUntil=0,noticeDeadline=0;
  bool sending=false,accepted=false,admitted=false,uploaded=false,verified=false,published=false,committed=false,outputNotified=false;
  Blob audience,descriptor,cipherToken,outputToken,envelope,noticeAck,verifiedAck,sourceToken;std::string sourceName;std::uint64_t sourceSize=0;SecretRecord saved;std::unique_ptr<Body> body;std::vector<Recipient> recipients;
  std::unique_ptr<transfer::Source> input;std::unique_ptr<transfer::Sink> sink,plain;
  std::unique_ptr<transfer::Hash> ph,ch;std::unique_ptr<group_attachments::Sealer> sealer;std::unique_ptr<Finalizer> finalizer;
 };
 struct Request {std::string id,job,operation;std::uint64_t generation=0,offset=0,maximum=0;Blob data,frame;bool dispatched=false;std::uint64_t sentAt=0;};
 Storage& storage_;const Profile& identity_;transfer::TransferManager& transfers_;Platform io_;EvidenceLookup evidence_;KeyAccess keys_;
 using Indexed=std::pair<std::uint64_t,std::string>;
 struct IndexPage {std::string previous;std::vector<Indexed> entries;};
 std::vector<Indexed> openIndex_;std::set<std::string> working_;std::string previousPage_;bool legacyPreview_=false;
 std::map<std::string,std::unique_ptr<Job>> jobs_;std::deque<Request> requests_;SecretRecord previous_;
 std::uint64_t account_=0,generation_=0,nextRoute_=1,probeAt_=0,turn_=0;std::string server_;bool supported_=false;unsigned capability_=0;
 Job& job(const std::string& id){if(!jobs_.count(id))loadJob(id);return *jobs_.at(id);}
 bool busy(const std::string& id)const{return std::any_of(requests_.begin(),requests_.end(),[&](const auto& r){return r.job==id;});}
 static transfer::Metadata metadata(const Job& j){protocol::check(bool(j.body));return {j.id,"ciphertext.peerchat",j.body->manifest.cipherSize(),j.body->manifest.cipherDigest};}
 static Blob digest(const Blob& wire){auto hex=Storage::publicKeyId(wire);Blob b;for(size_t i=0;i<hex.size();i+=2)b.push_back(static_cast<unsigned char>(std::stoul(hex.substr(i,2),nullptr,16)));return b;}
 mailbox::Header header(const Job& j)const{return mailbox::parse(j.envelope).header;}
 static bool allStored(const Job& j){return !j.recipients.empty()&&std::all_of(j.recipients.begin(),j.recipients.end(),[](const auto& r){return r.stored;});}
 void enqueue(Job* j,std::string operation,std::uint64_t offset=0,std::uint64_t maximum=0,Blob data={},Blob frame={}){
  if(j&&terminalCacheError(*j)&&operation!="cache_dm_release")return;
  protocol::check(account_&&requests_.size()<24);requests_.push_back({Storage::newMessageId(),j?j->id:"",std::move(operation),generation_,offset,maximum,std::move(data),std::move(frame),false});
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
   auto body=j.body?encodeBody(*j.body,header(j)):SecretRecord{};w.bytes(body.bytes);w.number(j.recipients.size());for(const auto& r:j.recipients){w.bytes(r.notice);w.bytes(r.receipt);w.number(r.stored);w.number(r.verified);w.number(r.done);}
  }
  SecretRecord next(std::move(w.data));protocol::check(next.bytes.size()<=960*1024);storage_.saveDirectAttachmentState(identity_.peerId,previous_.bytes,next.bytes);previous_.bytes.swap(next.bytes);
 }

 static bool terminalCacheError(const Job& j){return j.error=="cache_attachment_expired"||j.error=="cache_attachment_unavailable";}
 static bool active(const Job& j){if(j.state=="failed"||j.state=="canceled"||terminalCacheError(j))return false;return j.sending?(!j.uploaded||!allStored(j)):(j.noticeAck.empty()||(j.accepted&&!j.verified)||(j.verified&&!j.published));}
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
   auto body=j.body?encodeBody(*j.body,header(j)):SecretRecord{};w.bytes(body.bytes);w.number(j.recipients.size());for(const auto& r:j.recipients){w.bytes(r.notice);w.bytes(r.receipt);w.number(r.stored);w.number(r.verified);w.number(r.done);}
   SecretRecord next(std::move(w.data));protocol::check(next.bytes.size()<=900*1024);if(next.bytes==j.saved.bytes)continue;
   auto entries=openIndex_;auto working=working_;auto previousPage=previousPage_;if(j.saved.bytes.empty())entries.push_back({j.time,j.id});if(active(j))working.insert(j.id);else working.erase(j.id);protocol::check(working.size()<=32);sealPages(entries,previousPage);
   protocol::Writer ix(protocol::FileTransfer);ix.string("PeerChat/DMCacheHead/v1");ix.string(identity_.peerId);ix.string(previousPage);ix.number(entries.size());for(const auto& e:entries){ix.string(e.second);ix.number(e.first);}ix.number(working.size());for(const auto& id:working)ix.string(id);protocol::check(ix.data.size()<=65536);
   SecretRecord nextIndex(std::move(ix.data));storage_.saveDirectAttachmentJob(identity_.peerId,j.id,j.saved.bytes,next.bytes,previous_.bytes,nextIndex.bytes);j.saved.bytes.swap(next.bytes);previous_.bytes.swap(nextIndex.bytes);openIndex_=std::move(entries);working_=std::move(working);previousPage_=std::move(previousPage);
  }
 }
 std::unique_ptr<Job> readJob(protocol::Reader& r,bool modern){
  auto flag=[&](){auto n=r.number();protocol::check(n<=1);return n==1;};
  auto j=std::make_unique<Job>();j->id=r.string();j->message=r.string();j->peer=r.string();j->server=r.string();j->state=r.string();j->error=r.string();j->account=r.number();j->recipient=r.number();j->time=r.number();j->order=r.number();j->sending=flag();j->accepted=flag();j->admitted=flag();j->uploaded=flag();j->verified=flag();j->published=flag();j->committed=flag();j->noticeDeadline=r.number();j->audience=r.bytes();j->descriptor=r.bytes();j->cipherToken=r.bytes();j->outputToken=r.bytes();j->envelope=r.bytes();j->noticeAck=r.bytes();j->verifiedAck=r.bytes();
  if(modern){j->sourceToken=r.bytes();j->sourceName=r.string();j->sourceSize=r.number();j->lease=r.string();protocol::check(j->sourceToken.size()<=4096&&j->sourceSize<=transfer::MaxFileBytes&&(j->sourceName.empty()||transfer::safeName(j->sourceName))&&(j->lease.empty()||protocol::id(j->lease,32)));}
  SecretRecord plain(r.bytes());if(!plain.bytes.empty())j->body=std::make_unique<Body>(decodeStoredBody(plain.bytes,header(*j)));auto recipients=r.number();protocol::check(recipients<=32);while(recipients--){Recipient p;p.notice=r.bytes();p.receipt=r.bytes();p.stored=flag();p.verified=flag();p.done=r.number();j->recipients.push_back(std::move(p));}
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
 void pause(Job& j,const std::string& reason){if(!terminalCacheError(j)){j.error=reason;j.retry=io_.wall()+10000;if(!j.verified)j.state="paused";}j.route=0;j.contextUntil=0;save();}
 bool terminalTransfersReconciled_=false;
 void reconcileTerminalTransfers(){if(terminalTransfersReconciled_)return;for(const auto& d:transfers_.retainedRecords())if(d.purpose==transfer::Purpose::DirectCacheCiphertext&&d.state!=transfer::State::Complete&&d.state!=transfer::State::Failed&&d.state!=transfer::State::Cancelled){auto& j=job(d.metadata.id);if(terminalCacheError(j)){protocol::check(j.body&&j.peer==d.peer&&metadata(j)==d.metadata);transfers_.retireDirectCacheCiphertext(j.peer,j.id,j.error);}}terminalTransfersReconciled_=true;}
 void cacheUnavailable(Job& j,const std::string& reason){transfers_.retireDirectCacheCiphertext(j.peer,j.id,reason);j.error=reason;j.state=j.verified?"complete":"failed";j.retry=0;j.route=0;j.contextUntil=0;requests_.erase(std::remove_if(requests_.begin(),requests_.end(),[&](const auto& q){return q.job==j.id;}),requests_.end());save();}
 Evidence authorizedEvidence(const Job& j){
  auto evidence=evidence_();protocol::check(evidence.identity&&evidence.current&&j.contextUntil>io_.wall());auto mapping=evidence.current;auto relationship=j.body?j.body->manifest.binding.relationship:parseAudience(j.audience,j.account,identity_.peerId,j.recipient).relationship;
  evidence.current=[mapping,relationship](const auto& b,const std::string& rel,std::uint64_t other){return rel==relationship&&mapping(b,rel,other);};return evidence;
 }
 void acknowledgeNotice(Job& j){auto evidence=authorizedEvidence(j);validateBody(*j.body,header(j),&evidence);storage_.observeLogicalOrder(j.order);mailbox::Receipt ack;ack.recipientAccount=j.recipient;ack.recipientDevice=identity_.peerId;ack.senderDevice=header(j).senderDevice;ack.messageId=j.message;ack.envelopeDigest=digest(j.envelope);ack.signature=signDevicePayload(mailbox::receiptPayload(ack),identity_);auto previous=j.noticeAck;auto state=j.state;j.noticeAck=mailbox::serializeReceipt(ack);j.state=j.accepted?"downloading":"available";try{save();}catch(...){j.noticeAck=std::move(previous);j.state=state;j.contextUntil=0;throw;}}
 void beginSeal(Job& j){
  // Reserve enough protected-state capacity for every immutable notice BEFORE
  // encrypting any bytes. Keep room for receipts and storage encryption overhead.
  auto count=parseAudience(j.audience,j.account,identity_.peerId,j.recipient).recipients.size();std::uint64_t reserve=(j.audience.size()+8192)*(count+3);if(reserve>850*1024){j.state="failed";j.error="Protected attachment state capacity is full; no ciphertext was prepared";j.input.reset();save();return;}
  auto a=parseAudience(j.audience,j.account,identity_.peerId,j.recipient);auto evidence=authorizedEvidence(j);verifyAudience(a,evidence.identity);protocol::check(evidence.current(a.source,a.relationship,j.recipient));for(const auto& r:a.recipients)protocol::check(evidence.current(r,a.relationship,j.account));
  group_attachments::Binding b;b.attachment=j.id;b.sender=identity_.peerId;b.account=j.account;b.direct=true;b.recipientAccount=j.recipient;b.relationship=a.relationship;b.audienceDigest=Storage::publicKeyId(j.audience);
  auto length=j.input->size();transfer::Metadata m{j.id,"ciphertext.peerchat",group_attachments::HeaderBytes+length+std::max<std::uint64_t>(1,(length+group_attachments::BlockBytes-1)/group_attachments::BlockBytes)*group_attachments::TagBytes,{}};
  if(j.cipherToken.empty())j.sink=io_.stage(m);else {protocol::check(bool(io_.resumeStage));j.sink=io_.resumeStage(m,j.cipherToken);}protocol::check(bool(j.sink));j.cipherToken=io_.location(*j.sink);protocol::check(!j.cipherToken.empty());save();j.ph=io_.hash();j.ch=io_.hash();protocol::check(j.ph&&j.ch);j.sealer=std::make_unique<group_attachments::Sealer>(*j.input,*j.sink,std::move(b),*j.ph,*j.ch);j.state="preparing";save();
 }
 void prepare(Job& j){
  if(!j.sealer->step())return;
  auto manifest=j.sealer->takeManifest();j.sealer.reset();j.descriptor=descriptor(manifest,j.message,identity_);auto a=parseAudience(j.audience,j.account,identity_.peerId,j.recipient);
  auto body=std::make_unique<Body>(Body{j.time,j.order,"application/octet-stream",std::move(manifest),j.descriptor,j.audience,{}});
  // Freeze every exact HPKE notice before ciphertext becomes publishable.
  keys_(a.source.keyId,[&](auto& hpke,auto& signer,const auto& local,const auto&){protocol::check(mailbox::serializeBinding(local)==mailbox::serializeBinding(a.source));auto evidence=evidence_();for(const auto& recipient:a.recipients){mailbox::Header h;h.senderAccount=j.account;h.senderDevice=identity_.peerId;h.recipientAccount=j.recipient;h.recipientDevice=recipient.device;h.keyId=recipient.keyId;h.keyGeneration=recipient.generation;h.messageId=j.message;h.createdUtc=j.time;h.expiresUtc=std::min(j.time+mailbox::MaxTtlMilliseconds,j.noticeDeadline);protocol::check(h.expiresUtc>io_.wall());h.logicalOrder=j.order;h.storageAllowed=true;body->mailboxBinding=mailbox::headerPayload(h);auto plain=encodeBody(*body,h);auto envelope=mailbox::seal(h,plain.bytes,recipient,evidence.identity(j.recipient,recipient.device),verifyDevicePayload,Storage::publicKeyId,signer,hpke);j.recipients.push_back({mailbox::serialize(envelope),{},false,false,0});}});
  protocol::check(!j.recipients.empty());j.envelope=j.recipients.front().notice;body->mailboxBinding=mailbox::headerPayload(header(j));j.body=std::move(body);j.cipherToken=io_.location(*j.sink);protocol::check(!j.cipherToken.empty());j.state="uploading";save();j.sink->commit();j.committed=true;save();j.sink.reset();j.input.reset();j.ph.reset();j.ch.reset();
 }
 void upload(Job& j){auto rows=transfers_.snapshots();if(std::none_of(rows.begin(),rows.end(),[&](const auto& r){return r.metadata.id==j.id;}))transfers_.offerDirectCacheCiphertext(j.peer,metadata(j),j.body->manifest.size,io_.reopen(j.cipherToken,metadata(j)),io_.monotonic());}
 bool uploadHandshake(const Job& j)const{auto rows=transfers_.snapshots();return std::any_of(rows.begin(),rows.end(),[&](const auto& r){return r.metadata.id==j.id&&r.sending&&r.state!=transfer::State::Complete&&r.state!=transfer::State::Cancelled&&r.state!=transfer::State::Failed;});}
 void activate(Job& j){protocol::check(!j.lease.empty()&&j.server==server_&&(j.sending?j.account:j.recipient)==account_);j.route=nextRoute_++;transfers_.capability(j.peer,j.route,true);if(j.sending)upload(j);else offer(j);}
 void offer(Job& j){j.version=2;j.negotiation=j.route;auto m=metadata(j);protocol::Writer w(protocol::FileTransfer);w.data=control(j,transfer::Kind::Offer);w.string(m.name);w.number(m.size);w.bytes(Blob(m.digest.begin(),m.digest.end()));w.number(transfer::ChunkBytes);w.number(1);receive(j,w.data);auto rows=transfers_.snapshots();auto row=std::find_if(rows.begin(),rows.end(),[&](const auto& r){return r.metadata.id==j.id;});if(row!=rows.end()&&row->state==transfer::State::AwaitingAcceptance)transfers_.accept(j.id,io_.receiveSink(m),io_.monotonic());}
 void finalize(Job& j){
  if(!j.finalizer){auto rows=transfers_.completedRecords();auto r=std::find_if(rows.begin(),rows.end(),[&](const auto& d){return d.metadata.id==j.id&&d.purpose==transfer::Purpose::DirectCacheCiphertext;});protocol::check(r!=rows.end());j.cipherToken=r->location;j.input=io_.reopen(j.cipherToken,metadata(j));if(!j.plain)j.plain=io_.plainSink(j.body->manifest,j.outputToken);protocol::check(j.input&&j.plain);j.outputToken=io_.location(*j.plain);protocol::check(!j.outputToken.empty());j.ph=io_.hash();j.ch=io_.hash();protocol::check(j.ph&&j.ch);save();j.finalizer=std::make_unique<Finalizer>(*j.input,*j.plain,j.body->manifest,*j.ph,*j.ch);}
  if(!j.finalizer->step())return;
  j.finalizer.reset();j.verified=true;j.state="complete";j.verifiedAck=serializeReceipt(verifiedReceipt(*j.body,header(j),io_.wall(),identity_));save();j.plain.reset();j.input.reset();j.ph.reset();j.ch.reset();
 }
public:
 Application(Storage& storage,const Profile& profile,transfer::TransferManager& transfers,Platform io,EvidenceLookup evidence,KeyAccess keys):storage_(storage),identity_(profile),transfers_(transfers),io_(std::move(io)),evidence_(std::move(evidence)),keys_(std::move(keys)){protocol::check(io_.wall&&io_.monotonic&&io_.hash&&io_.stage&&io_.receiveSink&&io_.location&&io_.reopen&&io_.plainSink&&evidence_&&keys_);load();}
 void session(std::uint64_t account,std::uint64_t generation,const std::string& server){protocol::check(!account||(mailbox::account(account)&&generation&&protocol::id(server)));if(account==account_&&generation==generation_&&server==server_)return;requests_.clear();account_=account;generation_=generation;server_=server;supported_=false;capability_=0;probeAt_=0;for(auto& x:jobs_){x.second->route=0;x.second->contextUntil=0;}}
 std::string start(const std::string& peer,std::uint64_t recipient,std::unique_ptr<transfer::Source> source){protocol::check(account_&&supported_&&protocol::id(peer)&&mailbox::account(recipient)&&recipient!=account_&&source&&source->size()<=transfer::MaxFileBytes&&transfer::safeName(source->name()));room();auto j=std::make_unique<Job>();j->id=Storage::newMessageId();j->message=Storage::newMessageId();j->peer=peer;j->server=server_;j->account=account_;j->recipient=recipient;j->sending=true;j->time=io_.wall();j->order=storage_.nextLogicalOrder();j->sourceToken=source->resumeToken();j->sourceName=source->name();j->sourceSize=source->size();protocol::check(legacyPreview_||(!j->sourceToken.empty()&&j->sourceToken.size()<=4096&&bool(io_.reopenPlain)));j->input=std::move(source);auto id=j->id;jobs_.emplace(id,std::move(j));try{save();enqueue(&job(id),"cache_dm_prepare");}catch(...){jobs_.erase(id);throw;}return id;}
 void accept(const std::string& id){auto& j=job(id);protocol::check(!terminalCacheError(j)&&!j.sending&&j.body&&!j.noticeAck.empty()&&!j.verified&&j.server==server_&&j.recipient==account_);j.accepted=true;j.state="downloading";j.retry=0;save();}
 void accept(const std::string& id,std::unique_ptr<transfer::Sink> sink){protocol::check(bool(sink));accept(id);job(id).plain=std::move(sink);}
 void cancel(const std::string& id){auto& j=job(id);protocol::check(account_&&j.server==server_&&(j.sending?j.account:j.recipient)==account_&&!j.verified);if(j.sealer)j.sealer->cancel();if(j.finalizer)j.finalizer->cancel();j.sealer.reset();j.finalizer.reset();j.input.reset();j.sink.reset();j.plain.reset();transfers_.cancel(id);requests_.erase(std::remove_if(requests_.begin(),requests_.end(),[&](const auto& r){return r.job==id;}),requests_.end());j.state="canceled";save();if(!j.lease.empty()&&account_)enqueue(&j,"cache_dm_release");}
 bool supported()const{return account_&&supported_;}
 // 0 waiting/unavailable, 1 negotiated+registered, 2 explicit incompatible.
 unsigned capabilityState()const{return account_?capability_:0;}
 Blob completedLocationFor(std::uint64_t account,const std::string& server,const std::string& id){protocol::check(mailbox::account(account)&&protocol::id(server));auto& j=job(id);protocol::check(j.verified&&j.server==server&&(j.sending?j.account:j.recipient)==account);return j.outputToken;}
 Blob completedLocation(const std::string& id){protocol::check(account_);return completedLocationFor(account_,server_,id);}
 Blob completedLocation(const std::string& id)const{auto p=jobs_.find(id);protocol::check(account_&&p!=jobs_.end()&&p->second->verified&&p->second->server==server_&&(p->second->sending?p->second->account:p->second->recipient)==account_);return p->second->outputToken;}
 // nullopt is ordinary mailbox text; an empty Blob is an attachment pending
 // authenticated context. Malformed or unsupported typed bodies fail closed.
 std::optional<Blob> dispatchNotice(const Blob& wire){auto envelope=mailbox::parse(wire);bool attachment=false;keys_(envelope.header.keyId,[&](auto& hpke,auto&,const auto& local,const auto& handle){auto evidence=evidence_();protocol::check(bool(evidence.identity));auto sender=evidence.identity(envelope.header.senderAccount,envelope.header.senderDevice);SecretRecord plain(mailbox::open(envelope,local,envelope.header.senderAccount,sender,verifyDevicePayload,Storage::publicKeyId,handle,hpke,io_.wall()));mailbox::Writer prefix("PeerChat/mailbox-text/v1");if(plain.bytes.size()>=prefix.bytes.size()&&std::equal(prefix.bytes.begin(),prefix.bytes.end(),plain.bytes.begin())){mailbox::parseTextBody(plain.bytes);return;}protocol::Reader r(plain.bytes);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/dm-attachment-private/v1");attachment=true;});if(!attachment)return std::nullopt;return receiveNotice(wire);}
 std::unique_ptr<transfer::Source> reopen(const transfer::DurableState& d){auto& j=job(d.metadata.id);protocol::check(d.purpose==transfer::Purpose::DirectCacheCiphertext&&j.sending&&metadata(j)==d.metadata);return io_.reopen(j.cipherToken,d.metadata);}
 std::uint64_t route(const transfer::Snapshot& s)const{auto p=jobs_.find(s.metadata.id);return s.purpose==transfer::Purpose::DirectCacheCiphertext&&p!=jobs_.end()&&p->second->server==server_&&account_&&(p->second->sending?p->second->account:p->second->recipient)==account_&&!p->second->lease.empty()?p->second->route:0;}
 bool send(const transfer::Snapshot& s,const Blob& frame){if(requests_.size()>=24||!route(s)||busy(s.metadata.id))return false;auto& j=job(s.metadata.id);protocol::Reader r(frame);protocol::check(r.type()==protocol::FileTransfer);j.version=r.number();auto kind=static_cast<transfer::Kind>(r.number());protocol::check(r.string()==j.id);j.negotiation=j.version==2?r.number():0;
  if(j.sending){if(kind==transfer::Kind::Offer){enqueue(&j,"cache_dm_status",0,0,{},frame);return true;}if(kind==transfer::Kind::Chunk){auto offset=r.number();auto data=r.bytes();r.end();enqueue(&j,"cache_dm_append",offset,0,std::move(data),frame);return true;}if(kind==transfer::Kind::Finish){enqueue(&j,j.uploaded?"cache_dm_status":"cache_dm_complete",0,0,{},frame);return true;}}
  else {if(kind==transfer::Kind::Resume){auto offset=r.number();r.end();enqueue(&j,"cache_dm_progress",offset);return true;}if(kind==transfer::Kind::Complete){j.state="verifying";save();return true;}}
  if(kind==transfer::Kind::Cancel){enqueue(&j,"cache_dm_release");return true;}return false;
 }
 Blob nextCommand(){for(auto& q:requests_)if(!q.dispatched){protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/DMCacheCommand/v1");w.number(q.generation);w.string(q.id);w.string(q.operation);Job* j=q.job.empty()?nullptr:&job(q.job);w.number(j?j->recipient:0);w.string(j?j->id:"");w.string(j?j->lease:"");w.number(q.offset);w.number(q.maximum);w.number(j&&j->sending);w.bytes(j?j->descriptor:Blob{});w.bytes(j?j->audience:Blob{});w.bytes(q.data);protocol::check(w.data.size()<=100000);q.dispatched=true;q.sentAt=io_.wall();return w.data;}return {};}
 // Empty result means pending authenticated server context: NO mailbox ACK.
 // EvidenceLookup.current supplies current device/key/friend mapping. Only an
 // exact, recent response on this session proves the relationship incarnation.
 Blob receiveNotice(const Blob& wire){protocol::check(account_&&supported_);auto envelope=mailbox::parse(wire);protocol::check(envelope.header.recipientAccount==account_&&envelope.header.recipientDevice==identity_.peerId);std::unique_ptr<Body> body;keys_(envelope.header.keyId,[&](auto& hpke,auto&,const auto& local,const auto& handle){auto evidence=evidence_();protocol::check(evidence.identity&&evidence.current);auto sender=evidence.identity(envelope.header.senderAccount,envelope.header.senderDevice);SecretRecord plain(mailbox::open(envelope,local,envelope.header.senderAccount,sender,verifyDevicePayload,Storage::publicKeyId,handle,hpke,io_.wall()));body=std::make_unique<Body>(decodeStoredBody(plain.bytes,envelope.header));auto a=parseAudience(body->audience,envelope.header.senderAccount,envelope.header.senderDevice,envelope.header.recipientAccount);verifyAudience(a,evidence.identity);protocol::check(parseDescriptor(body->descriptor).identity==sender&&evidence.current(a.source,a.relationship,envelope.header.recipientAccount));auto target=std::find_if(a.recipients.begin(),a.recipients.end(),[&](const auto& b){return mailbox::serializeBinding(b)==mailbox::serializeBinding(local);});protocol::check(target!=a.recipients.end()&&evidence.current(*target,a.relationship,envelope.header.senderAccount));});protocol::check(bool(body));auto id=body->manifest.binding.attachment;if(!jobs_.count(id)&&!legacyPreview_){SecretRecord existing(storage_.directAttachmentJob(identity_.peerId,id));if(!existing.bytes.empty())loadJob(id);}auto old=jobs_.find(id);if(old!=jobs_.end()){auto& j=*old->second;protocol::check(!j.sending&&j.server==server_&&j.envelope==wire);if(j.contextUntil>io_.wall()&&!j.noticeAck.empty())return j.noticeAck;if(!busy(j.id))enqueue(&j,"cache_dm_context");return {};}room();for(const auto& item:jobs_)protocol::check(item.second->message!=envelope.header.messageId);
  auto j=std::make_unique<Job>();j->id=id;j->message=envelope.header.messageId;j->peer=envelope.header.senderDevice;j->server=server_;j->account=envelope.header.senderAccount;j->recipient=account_;j->time=body->time;j->order=body->order;j->state="available";j->audience=body->audience;j->descriptor=body->descriptor;j->body=std::move(body);j->envelope=wire;
  j->state="awaiting_context";jobs_.emplace(id,std::move(j));try{save();}catch(...){jobs_.erase(id);throw;}enqueue(&job(id),"cache_dm_context");return {};
 }
 std::vector<Snapshot> snapshotsFor(std::uint64_t account,const std::string& server,const std::string& peer={})const{protocol::check(!account||(mailbox::account(account)&&protocol::id(server)&&(peer.empty()||protocol::id(peer))));std::vector<Snapshot> out;auto transfers=transfers_.snapshots();for(const auto& item:jobs_){const auto& j=*item.second;if(!account||(!j.sending&&j.noticeAck.empty())||(j.sending?j.account:j.recipient)!=account||j.server!=server||(!peer.empty()&&j.peer!=peer))continue;Snapshot s{j.id,j.message,j.peer,j.body?j.body->manifest.name:j.input?j.input->name():!j.sourceName.empty()?j.sourceName:"Encrypted attachment",j.state,j.error,j.sending,j.verified,j.uploaded,allStored(j),j.body?j.body->manifest.size:j.input?j.input->size():j.sourceSize,0,j.time,j.order};if(j.verified||j.uploaded)s.done=s.size;for(const auto& t:transfers)if(t.metadata.id==j.id&&j.body)s.done=std::min(s.size,t.done>group_attachments::HeaderBytes?(t.done-group_attachments::HeaderBytes)*s.size/std::max<std::uint64_t>(1,t.metadata.size-group_attachments::HeaderBytes):0);out.push_back(std::move(s));}return out;}
 std::vector<Snapshot> snapshots()const{return snapshotsFor(account_,server_);}
 HistoryPage historyFor(std::uint64_t account,const std::string& server,const std::string& cursor={},size_t limit=32,const std::string& peer={}){
  protocol::check(mailbox::account(account)&&protocol::id(server)&&limit&&limit<=32&&(peer.empty()||protocol::id(peer)));std::string pageId;size_t offset=0;
  if(cursor.empty()){
   auto entries=openIndex_;auto previous=previousPage_;if(legacyPreview_){entries.clear();for(const auto& item:jobs_)entries.push_back({item.second->time,item.first});std::sort(entries.begin(),entries.end());}
   sealPages(entries,previous);pageId=entries.empty()?previous:storePage(entries,previous);if(pageId.empty())return {};
  }else {protocol::check(cursor.size()>=66&&cursor.size()<=68&&cursor[64]==':'&&protocol::id(cursor.substr(0,64)));pageId=cursor.substr(0,64);auto number=cursor.substr(65);protocol::check(!number.empty()&&(number=="0"||number.front()!='0'));for(char c:number){protocol::check(c>='0'&&c<='9');offset=offset*10+static_cast<unsigned>(c-'0');}protocol::check(offset<=128);}
  auto page=readPage(pageId);protocol::check(offset<=page.entries.size());HistoryPage result;size_t inspected=0;while(offset<page.entries.size()&&inspected++<limit){auto id=page.entries[page.entries.size()-1-offset++].second;if(!jobs_.count(id))loadJob(id);auto visible=snapshotsFor(account,server,peer);auto row=std::find_if(visible.begin(),visible.end(),[&](const auto& s){return s.id==id&&(peer.empty()||s.peer==peer);});if(row!=visible.end())result.items.push_back(std::move(*row));}
  std::sort(result.items.begin(),result.items.end(),[](const auto& a,const auto& b){return std::tie(a.time,a.id)>std::tie(b.time,b.id);});
  result.more=offset<page.entries.size()||!page.previous.empty();if(result.more)result.cursor=offset<page.entries.size()?pageId+":"+std::to_string(offset):page.previous+":0";return result;
 }
 HistoryPage history(const std::string& cursor={},size_t limit=32,const std::string& peer={}){protocol::check(account_);return historyFor(account_,server_,cursor,limit,peer);}
 void tick();void reply(const Blob&);void failure(const Blob&);
};

inline void Application::tick(){
 reconcileTerminalTransfers();
 if(!account_)return;
 auto now=io_.wall();
 for(auto it=requests_.begin();it!=requests_.end();){if(it->dispatched&&now>=it->sentAt&&now-it->sentAt>=30000){auto id=it->job;it=requests_.erase(it);if(id.empty()){supported_=false;capability_=0;probeAt_=now;}else pause(job(id),"Cache request timed out; durable attachment data retained");}else ++it;}
 if(!supported_){if(now>=probeAt_&&!busy("")){enqueue(nullptr,"cache_dm_capability");probeAt_=now+30000;}return;}
 // One local encryption/decryption block globally per worker tick.
 std::vector<Job*> localWork;for(auto& item:jobs_){auto& j=*item.second;if(j.server==server_&&(j.sending?j.account:j.recipient)==account_&&((!terminalCacheError(j)&&(j.sealer||j.state=="verifying"))||(j.verified&&!j.outputNotified&&io_.verified)))localWork.push_back(&j);}
 if(!localWork.empty()){auto& j=*localWork[turn_++%localWork.size()];
  try{if(j.sealer)prepare(j);else if(j.state=="verifying")finalize(j);else if(j.verified&&!j.outputNotified&&io_.verified){io_.verified(j.body->manifest,j.outputToken);j.outputNotified=true;}}
  catch(...){j.sealer.reset();j.finalizer.reset();j.sink.reset();j.input.reset();j.plain.reset();if(!j.verified){j.state="failed";j.error="Attachment encryption, integrity or protected storage failed";}else if(!terminalCacheError(j))j.error="Verified internal copy retained; output action failed";save();}
 }
  for(auto& item:jobs_){auto& j=*item.second;if(requests_.size()>=24)break;if(j.server!=server_||now<j.retry||busy(j.id)||j.state=="failed"||j.state=="canceled"||terminalCacheError(j)||j.sealer)continue;if((j.sending?j.account:j.recipient)!=account_)continue;
   // Retained terminal rows have no network work. Output notification is
   // handled separately above, and queued release replies remain independent.
   if((j.sending&&j.state=="complete"&&!j.recipients.empty()&&std::all_of(j.recipients.begin(),j.recipients.end(),[](const auto& recipient){return recipient.verified;}))||(!j.sending&&j.verified&&j.published))continue;
  try{
   if(!j.body){if(j.sending&&j.input)enqueue(&j,"cache_dm_prepare");continue;}
   auto evidence=evidence_();if(j.sending){auto a=parseAudience(j.audience,j.account,identity_.peerId,j.recipient);protocol::check(evidence.identity&&evidence.current);verifyAudience(a,evidence.identity);protocol::check(evidence.current(a.source,a.relationship,j.recipient));}else {if(j.contextUntil<=now){enqueue(&j,"cache_dm_context");continue;}auto authorized=authorizedEvidence(j);validateBody(*j.body,header(j),&authorized);}
   if(j.sending){
    if(j.noticeDeadline<=now&&!allStored(j)){j.state="failed";j.error="Attachment notice deadline expired before every device stored its notice";save();continue;}
    if(!j.uploaded||uploadHandshake(j)){if(!j.route)enqueue(&j,"cache_dm_status");continue;}
    auto next=std::find_if(j.recipients.begin(),j.recipients.end(),[](const auto& p){return !p.stored;});if(next!=j.recipients.end()){enqueue(&j,"cache_dm_publish",0,0,next->notice);continue;}
    if(!std::all_of(j.recipients.begin(),j.recipients.end(),[](const auto& p){return p.verified;})){enqueue(&j,"cache_dm_receipts",0,0,Blob(j.cursor.begin(),j.cursor.end()));j.retry=now+5000;}
   }else if(j.verified){if(!j.published)enqueue(&j,"cache_dm_verified",0,0,j.verifiedAck);}
   else if(j.accepted&&j.state!="verifying"){
    if(j.lease.empty()){enqueue(&j,"cache_dm_context");continue;}
    auto rows=transfers_.snapshots();auto row=std::find_if(rows.begin(),rows.end(),[&](const auto& s){return s.metadata.id==j.id;});if(row==rows.end()||transfers_.hasPendingControl(j.id))continue;
    if(row->state==transfer::State::Complete){j.state="verifying";save();continue;}
    if(row->state==transfer::State::Transferring&&row->done==j.readOffset&&row->done<row->metadata.size)enqueue(&j,"cache_dm_read",row->done,std::min<std::uint64_t>(transfer::ChunkBytes,row->metadata.size-row->done));
   }
  }catch(...){pause(j,"Attachment authorization or protected storage unavailable");}
 }
}

inline void Application::reply(const Blob& wire){
 protocol::check(wire.size()<=100000);protocol::Reader r(wire);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/DMCacheReply/v1");auto generation=r.number();auto id=r.string();auto state=r.string();auto cacheVersion=r.number(),dmVersion=r.number(),offset=r.number(),size=r.number(),expires=r.number(),complete=r.number();auto lease=r.string();auto descriptorWire=r.bytes(),audience=r.bytes(),data=r.bytes();auto newNotice=r.number(),count=r.number();protocol::check(complete<=1&&newNotice<=1&&count<=8&&expires<=253402300799ull&&(lease.empty()||protocol::id(lease,32))&&descriptorWire.size()<=2048&&audience.size()<=32768);
 struct Row{std::uint64_t account=0,done=0;std::string device;bool verified=false;Blob receipt;};std::vector<Row> rows;while(count--){Row row;row.account=r.number();row.device=r.string();row.done=r.number();auto verified=r.number();protocol::check(mailbox::account(row.account)&&protocol::id(row.device)&&verified<=1);row.verified=verified;row.receipt=r.bytes();protocol::check(row.receipt.size()<=4096);rows.push_back(std::move(row));}auto cursor=r.string();auto more=r.number();r.end();protocol::check(more<=1&&(cursor.empty()||protocol::id(cursor)));
 if(generation!=generation_||!account_)return;
 auto found=std::find_if(requests_.begin(),requests_.end(),[&](const auto& q){return q.id==id&&q.dispatched;});if(found==requests_.end())return;auto q=std::move(*found);requests_.erase(found);
 if(q.operation=="cache_dm_capability"){
  supported_=false;capability_=0;probeAt_=io_.wall()+10000;if(state!="accepted"||cacheVersion!=2||dmVersion!=1){if(state=="unsupported_request"||state=="cache_unsupported_operation"||state=="cache_dm_feature_gated"||state=="cache_dm_unsupported"||(state=="accepted"&&(cacheVersion!=2||dmVersion!=1)))capability_=2;return;}
  keys_("",[&](auto&,auto& signer,const auto& local,const auto&){protocol::check(local.accountId==account_&&local.device==identity_.peerId&&local.storageAllowed&&signer.publicKey()==identity_.publicKey);protocol::Writer proof(protocol::FileTransfer);proof.string("PeerChat/dm-cache-device/v1");proof.number(account_);proof.string(identity_.peerId);proof.string(local.keyId);proof.number(local.generation);proof.number(1);proof.number(1);proof.bytes(identity_.publicKey);proof.bytes(signer.sign(proof.data));enqueue(nullptr,"cache_dm_register",0,0,std::move(proof.data));});return;
 }
 if(q.operation=="cache_dm_register"){supported_=state=="accepted"&&cacheVersion==2&&dmVersion==1;capability_=supported_?1:0;probeAt_=io_.wall()+10000;return;}
 auto& j=job(q.job);if(j.server!=server_)return;
 try{
  if(q.operation=="cache_dm_context"||q.operation=="cache_dm_prepare")protocol::check(io_.wall()>=q.sentAt&&io_.wall()-q.sentAt<30000);
  if(state!="accepted"){
   if(state=="cache_lease_expired"||state=="cache_lease_invalid"){j.lease.clear();j.route=0;save();}
   if(q.operation=="cache_dm_status"&&!j.admitted&&state=="cache_attachment_unavailable"){enqueue(&j,"cache_dm_admit");return;}
   if(state=="cache_attachment_expired"||state=="cache_attachment_unavailable"){cacheUnavailable(j,state);return;}
   pause(j,state.size()<=96?state:"Cache rejected attachment operation");return;
  }
  protocol::check(cacheVersion==2&&dmVersion==1);if(j.body&&size)protocol::check(size==metadata(j).size);if(!descriptorWire.empty()&&j.body)protocol::check(descriptorWire==j.descriptor);if(!audience.empty()&&j.body)protocol::check(audience==j.audience);
  if(q.operation=="cache_dm_prepare"){protocol::check(!j.body&&j.input&&!audience.empty()&&expires>io_.wall()/1000);protocol::check(j.audience.empty()||j.audience==audience);j.noticeDeadline=expires*1000;j.audience=std::move(audience);j.contextUntil=io_.wall()+30000;save();beginSeal(j);return;}
  if(q.operation=="cache_dm_admit"){j.admitted=true;save();enqueue(&j,"cache_dm_acquire");return;}
  if(q.operation=="cache_dm_status"){
   protocol::check(size==metadata(j).size&&offset<=size);j.admitted=true;if(!q.frame.empty()){protocol::Reader frame(q.frame);frame.number();auto kind=static_cast<transfer::Kind>(frame.number());if(kind==transfer::Kind::Finish){protocol::check(complete);receive(j,control(j,transfer::Kind::Complete));}else receive(j,control(j,transfer::Kind::Resume,offset));return;}if(complete){j.uploaded=true;j.state="notice_pending";save();if(uploadHandshake(j)){if(j.lease.empty())enqueue(&j,"cache_dm_acquire");else activate(j);}return;}save();if(j.lease.empty())enqueue(&j,"cache_dm_acquire");else activate(j);return;
  }
  if(q.operation=="cache_dm_context"){protocol::check(descriptorWire==j.descriptor&&audience==j.audience&&complete&&size==metadata(j).size&&expires>io_.wall()/1000);j.contextUntil=std::min<std::uint64_t>(io_.wall()+30000,expires*1000);auto evidence=authorizedEvidence(j);validateBody(*j.body,header(j),&evidence);if(j.noticeAck.empty())acknowledgeNotice(j);if(j.accepted&&!j.verified&&!j.route){if(j.lease.empty())enqueue(&j,"cache_dm_acquire");else activate(j);}return;}
  if(q.operation=="cache_dm_acquire"){protocol::check(protocol::id(lease,32));j.lease=lease;save();activate(j);return;}
  if(q.operation=="cache_dm_append"){protocol::check(offset==q.offset+q.data.size());receive(j,control(j,transfer::Kind::Resume,offset));return;}
  if(q.operation=="cache_dm_complete"){protocol::check(complete&&size==metadata(j).size);receive(j,control(j,transfer::Kind::Complete));j.uploaded=true;j.state="notice_pending";save();return;}
  if(q.operation=="cache_dm_read"){protocol::check(!data.empty()&&data.size()<=q.maximum&&q.offset+data.size()<=metadata(j).size);protocol::Writer frame(protocol::FileTransfer);frame.data=control(j,transfer::Kind::Chunk);frame.number(q.offset);frame.bytes(data);receive(j,frame.data);return;}
  if(q.operation=="cache_dm_progress"){protocol::check(offset==q.offset);j.readOffset=offset;if(offset==metadata(j).size)receive(j,control(j,transfer::Kind::Finish));return;}
  if(q.operation=="cache_dm_publish"){auto p=std::find_if(j.recipients.begin(),j.recipients.end(),[&](const auto& x){return x.notice==q.data;});protocol::check(p!=j.recipients.end());p->stored=true;j.state=allStored(j)?"available":"notice_pending";save();return;}
  if(q.operation=="cache_dm_verified"){j.published=true;j.error.clear();save();if(!j.lease.empty())enqueue(&j,"cache_dm_release");return;}
  if(q.operation=="cache_dm_release"){j.lease.clear();j.route=0;save();return;}
  if(q.operation=="cache_dm_receipts"){
   for(const auto& row:rows){auto p=std::find_if(j.recipients.begin(),j.recipients.end(),[&](const auto& x){auto h=mailbox::parse(x.notice).header;return h.recipientAccount==row.account&&h.recipientDevice==row.device;});protocol::check(p!=j.recipients.end()&&row.done<=metadata(j).size);p->done=row.done;if(row.verified){auto receipt=parseReceipt(row.receipt);auto h=mailbox::parse(p->notice).header;auto copy=decodeStoredBody(encodeBody(*j.body,header(j)).bytes,header(j));copy.mailboxBinding=mailbox::headerPayload(h);protocol::check(receipt.verifiedUtc<=io_.wall()+300000&&verifyReceipt(receipt,copy,h,evidence_()));p->receipt=row.receipt;p->verified=true;}}
   j.cursor=more?cursor:"";if(std::all_of(j.recipients.begin(),j.recipients.end(),[](const auto& p){return p.verified;}))j.state="complete";save();if(more){j.retry=0;enqueue(&j,"cache_dm_receipts",0,0,Blob(cursor.begin(),cursor.end()));}return;
  }
  protocol::check(false);
 }catch(...){pause(j,"Attachment response failed validation or protected storage");throw;}
}
inline void Application::failure(const Blob& wire){
 protocol::Reader r(wire);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/DMCacheFailure/v1");auto generation=r.number();auto id=r.string();protocol::check(r.string()=="network_unavailable");r.end();if(generation!=generation_||!account_)return;auto found=std::find_if(requests_.begin(),requests_.end(),[&](const auto& q){return q.id==id&&q.dispatched;});if(found==requests_.end())return;auto q=std::move(*found);requests_.erase(found);if(q.job.empty()){supported_=false;capability_=0;probeAt_=io_.wall()+10000;}else pause(job(q.job),"Cache connection unavailable; durable attachment data retained");
}
}
