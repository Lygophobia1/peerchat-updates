#pragma once
#include "group_attachment_staging.hpp"
#include "group_attachment_key_envelope.hpp"
#include "windows_group_attachment_rpc.hpp"
#include <deque>
#include <filesystem>
namespace peerchat::group_attachments {
// Shared application owner, serviced by each platform's existing worker. TransferManager
// alone owns chunk/ACK/hash/journal state. This class translates its scoped frames
// to bounded authenticated cache requests; it never opens a socket or thread.
template<class Platform> class BasicApplication {
 using CacheStateStore=typename Platform::CacheStateStore;
 using HistoryStore=typename Platform::HistoryStore;
 using PreparedKeys=typename Platform::PreparedKeys;
 using StagedCipherSource=typename Platform::StagedCipherSource;
 using ReceiverFinalizer=typename Platform::ReceiverFinalizer;
 using VerifiedOutput=typename Platform::VerifiedOutput;
 using OutputExport=typename Platform::OutputExport;
 using FileSource=typename Platform::FileSource;
 using FileSink=typename Platform::FileSink;
 using Sha256=typename Platform::Sha256;
 using CleanupJournal=typename Platform::CleanupJournal;
 using V=rpc::Value;
public:
#ifdef PEERCHAT_ATTACHMENT_APPLICATION_TESTS
 inline static bool pauseAfterStaging=false,stagingPaused=false;
#endif
 struct Context {group_keys::RosterSnapshot roster;GroupState ledger;};
 using ContextLookup=std::function<Context(const std::string&,const group_keys::RosterSnapshot*)>;
 using KeyAccess=std::function<void(const std::function<void(DeviceKeyManager&,platform::HpkeProvider&,platform::IdentityProvider&)>&)>;
private:
 struct Recipient {std::uint64_t account=0,done=0;std::string device,state="pending",error;Blob binding,receipt;};
 struct Job {
  std::string id,local,server,peer,name,state="preparing",error,lease;bool sending=false,accepted=false,verified=false,published=false,keysLoaded=false,admitted=false,outputChecked=false,uploadComplete=false,outputRequested=false;
  std::uint64_t time=0,order=0,expires=0,route=0,retryAt=0,version=1,negotiation=0,readOffset=0,inputSize=0;unsigned failures=0;
  // outputPath is always the verified internal copy, never a user destination.
  Blob descriptor,sourceLocation,receipt,receivedEnvelope,outputToken,cipherLocation;std::wstring destination,outputPath;std::unique_ptr<Body> body;
  SecretRecord saved;std::uint64_t revision=0;std::optional<bool> savedActive;
  std::unique_ptr<transfer::Source> source;std::unique_ptr<FileSink> sink;
  std::unique_ptr<VerifiedOutput> output;std::unique_ptr<OutputExport> exportJob;std::wstring exportDestination;std::string exportError,exportId,exportState="none";Blob exportApproval,exportToken;bool exportCleaned=false;
  std::unique_ptr<Sha256> plainHash,cipherHash;std::unique_ptr<Sealer> sealer;std::unique_ptr<ReceiverFinalizer> finalizer;
  bool earlyBlocked=false;std::vector<Recipient> recipients;std::optional<V> context;size_t publishIndex=0;
 };
 struct Request {std::string id,job,operation;V value;std::uint64_t route=0,offset=0;Blob frame;bool dispatched=false;std::uint64_t dispatchedAt=0;};
 Platform platform_;Storage& storage_;const Profile& profile_;transfer::TransferManager& transfers_;CacheStateStore& journal_;ContextLookup context_;KeyAccess keys_;
 std::wstring directory_,cipherDirectory_,receivedDirectory_;StagingJournal staging_;std::vector<Staged> staged_;std::unique_ptr<HistoryStore> history_;std::unique_ptr<CleanupJournal> preparationCleanup_;std::uint64_t maintenanceAt_=0;
 std::function<Blob(transfer::Source&)> thumbnailer_;
 std::map<std::string,std::unique_ptr<Job>> jobs_;std::deque<Request> requests_;std::unique_ptr<PreparedKeys> prepared_;
 std::string referenceVisit_;
 std::uint64_t referenceBackfillBefore_=INT64_MAX,referenceBackfillAt_=0;
 std::string visibleGroup_,server_,cursor_,availability_="Waiting for authenticated cache capability",capabilityStage_="waiting",capabilityCode_;std::uint64_t account_=0,generation_=0,nextRoute_=1,pollAt_=0;size_t workTurn_=0;bool supported_=false;std::uint64_t historyCursor_=0;bool historyMore_=false;
 static std::uint64_t wall(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());}
 static Blob bytes(const std::string& s){return Blob(s.begin(),s.end());}
 static std::string peer(const std::string& server,const std::string& id){return Storage::publicKeyId(bytes("PeerChat/cache-route/v1/"+server+"/"+id));}
 static transfer::Metadata metadata(const Job& j){protocol::check(j.body!=nullptr);return {j.id,"ciphertext.peerchat",j.body->manifest.cipherSize(),j.body->manifest.cipherDigest};}
 static Blob control(const Job& j,transfer::Kind kind,std::uint64_t offset=0){protocol::Writer w(protocol::FileTransfer);w.number(j.version);w.number(static_cast<unsigned>(kind));w.string(j.id);if(j.version==2)w.number(j.negotiation);if(kind==transfer::Kind::Resume)w.number(offset);return w.data;}
 Job& job(const std::string& id){auto p=jobs_.find(id);protocol::check(p!=jobs_.end());return *p->second;}
 bool busy(const std::string& id)const{return std::any_of(requests_.begin(),requests_.end(),[&](const auto& r){return r.job==id;});}
 static bool active(const Job& j){
  if(j.exportState=="pending"||j.exportState=="copying"||j.exportState=="failed"||(j.exportState=="complete"&&!j.exportCleaned))return true;
  if(j.state=="canceled"){return !j.sourceLocation.empty()||!j.cipherLocation.empty()||!j.lease.empty()||(j.sending&&j.admitted&&!j.uploadComplete);}
  if(j.state=="expired"||j.state=="failed")return false;
  if(!j.sending)return (j.verified&&!j.cipherLocation.empty())||!j.body||(j.accepted&&!j.verified)||(j.verified&&!j.published);
  return !j.published||std::any_of(j.recipients.begin(),j.recipients.end(),[](const auto& p){return p.state!="verified";});
 }
 void save(){
  for(auto& pair:jobs_){auto& j=*pair.second;protocol::Writer w(protocol::FileTransfer);w.string(j.exportId.empty()?"PeerChat/AttachmentJob/v3":"PeerChat/AttachmentJob/v4");w.string(profile_.peerId);
   w.string(j.id);w.string(j.local);w.string(j.server);w.string(j.name);w.string(j.state);w.string(j.error);
   w.number(j.sending);w.number(j.accepted);w.number(j.verified);w.number(j.published);w.number(j.admitted);w.number(j.uploadComplete);
   w.number(j.time);w.number(j.order);w.number(j.expires);w.bytes(j.descriptor);w.bytes(j.sourceLocation);w.bytes(j.receipt);w.bytes(j.receivedEnvelope);w.bytes(j.outputToken);w.bytes(j.cipherLocation);w.text(j.destination);w.text(j.outputPath);
   auto body=j.body?encodeBody(*j.body):SecretRecord{};w.bytes(body.bytes);w.number(j.recipients.size());
   for(auto& p:j.recipients){w.number(p.account);w.string(p.device);w.string(p.state);w.string(p.error);w.number(p.done);w.bytes(p.receipt);}w.string(j.lease);w.number(j.inputSize);
   if(!j.exportId.empty()){w.text(j.exportDestination);w.string(j.exportId);w.string(j.exportState);w.string(j.exportError);w.bytes(j.exportApproval);w.bytes(j.exportToken);w.number(j.exportCleaned);}
   SecretRecord next(std::move(w.data));const auto nextActive=active(j);
   // Exact unchanged records need no protected history read or CAS. A changed
   // payload or active flag still follows the full existing validation/write.
   if(j.savedActive&&*j.savedActive==nextActive&&j.saved.bytes==next.bytes)continue;
   auto previous=history_->get(j.id);auto indexedTime=previous?previous->binding.time:j.time;
   j.revision=history_->save({j.id,j.local,j.server,indexedTime},j.saved.bytes,next.bytes,nextActive,j.revision);j.saved.bytes.swap(next.bytes);j.savedActive=nextActive;
  }
 }
 static void presentation(Body& body){
  auto extension=std::filesystem::path(unicode::decode(body.manifest.name)).extension().wstring();std::transform(extension.begin(),extension.end(),extension.begin(),::towlower);
  if(extension==L".png"||extension==L".jpg"||extension==L".jpeg"||extension==L".gif"||extension==L".webp"||extension==L".bmp"){body.presentation=Presentation::Picture;body.contentType=extension==L".png"?"image/png":extension==L".gif"?"image/gif":extension==L".webp"?"image/webp":extension==L".bmp"?"image/bmp":"image/jpeg";}
 }
 void loadRecord(typename HistoryStore::Record record){
  if(jobs_.count(record.binding.id))return;
  protocol::Reader r(record.payload.bytes);protocol::check(r.type()==protocol::FileTransfer);auto format=r.string();protocol::check((format=="PeerChat/AttachmentJob/v1"||format=="PeerChat/AttachmentJob/v2"||format=="PeerChat/AttachmentJob/v3"||format=="PeerChat/AttachmentJob/v4")&&r.string()==profile_.peerId);
  auto j=std::make_unique<Job>();j->id=r.string();j->local=r.string();j->server=r.string();j->name=r.string();j->state=r.string();j->error=r.string();
  j->sending=r.number()!=0;j->accepted=r.number()!=0;j->verified=r.number()!=0;j->published=r.number()!=0;j->admitted=r.number()!=0;j->uploadComplete=r.number()!=0;
  j->time=r.number();j->order=r.number();j->expires=r.number();j->descriptor=r.bytes();j->sourceLocation=r.bytes();j->receipt=r.bytes();j->receivedEnvelope=r.bytes();j->outputToken=r.bytes();j->cipherLocation=r.bytes();j->destination=r.text();j->outputPath=r.text();
  SecretRecord body(r.bytes());if(!body.bytes.empty())j->body=std::make_unique<Body>(decodeBody(body.bytes));
  auto count=r.number();protocol::check(count<=256);while(count--){Recipient p;p.account=r.number();p.device=r.string();p.state=r.string();p.error=r.string();p.done=r.number();p.receipt=r.bytes();j->recipients.push_back(std::move(p));}if(format!="PeerChat/AttachmentJob/v1"){j->lease=r.string();protocol::check(j->lease.empty()||protocol::id(j->lease,32));}if(format=="PeerChat/AttachmentJob/v3"||format=="PeerChat/AttachmentJob/v4")j->inputSize=r.number();else if(j->body)j->inputSize=j->body->manifest.size;
  if(format=="PeerChat/AttachmentJob/v4"){j->exportDestination=r.text();j->exportId=r.string();j->exportState=r.string();j->exportError=r.string();j->exportApproval=r.bytes();j->exportToken=r.bytes();j->exportCleaned=r.number()!=0;protocol::check(protocol::id(j->exportId,32)&&j->exportDestination.size()<=32768&&(j->exportState=="pending"||j->exportState=="copying"||j->exportState=="failed"||j->exportState=="complete"));j->outputRequested=(j->verified||j->sending)&&(j->exportState=="pending"||j->exportState=="copying"||(j->exportState=="complete"&&!j->exportCleaned));}
  r.end();
  protocol::check(j->id==record.binding.id&&j->local==record.binding.group&&j->server==record.binding.server&&j->name.size()<=1024);
  j->peer=peer(j->server,j->id);
  if(j->body){auto d=parseDescriptor(j->descriptor);protocol::check(d.attachment==j->id&&d.matches(j->body->manifest));}
  else if(j->state!="canceled"&&(j->sending||j->receivedEnvelope.empty())){
   auto staged=std::find_if(staged_.begin(),staged_.end(),[&](const auto& value){return value.manifest.binding.attachment==j->id;});
   if(j->sending&&staged!=staged_.end()){
    protocol::Writer writer(protocol::FileTransfer);writePrivateManifest(writer,staged->manifest);SecretRecord copy(std::move(writer.data));protocol::Reader manifest(copy.bytes);
    j->body=std::make_unique<Body>(Body{j->time,j->order,Presentation::File,"application/octet-stream",readPrivateManifest(manifest)});manifest.end();presentation(*j->body);
    j->sourceLocation=staged->location;j->descriptor=uploadDescriptor(j->body->manifest,j->id,profile_);j->state="uploading";j->error.clear();
   }else{j->state="failed";j->error="Preparation interrupted before immutable ciphertext was retained; select the original file again";}
  }
  if(j->verified&&!j->cipherLocation.empty())j->outputRequested=true;
  j->revision=record.revision;j->savedActive=record.active;j->saved.bytes.swap(record.payload.bytes);protocol::check(jobs_.emplace(j->id,std::move(j)).second);
 }
 void load(){
  history_=Platform::history(storage_,profile_,directory_);
  auto page=history_->page({},INT64_MAX,HistoryStore::MaxPage,HistoryStore::Filter::Active);protocol::check(!page.more);
  for(auto& row:page.items){auto record=history_->get(row.binding.id);protocol::check(record.has_value());loadRecord(std::move(*record));}
 }
 static bool localWork(const Job& j){return j.outputRequested||j.finalizer||(j.exportJob&&!j.exportJob->complete()&&!j.exportJob->failed());}
 typename HistoryStore::Page historyPage(const std::string& group,std::uint64_t before,size_t limit){
  protocol::check(protocol::id(group,32));auto page=history_->page(group,before,limit);std::set<std::string> keep;
  for(const auto& row:page.items)keep.insert(row.binding.id);
  for(auto it=jobs_.begin();it!=jobs_.end();){if(!keep.count(it->first)&&!active(*it->second)&&!localWork(*it->second)&&!busy(it->first))it=jobs_.erase(it);else ++it;}
  for(const auto& row:page.items)if(!jobs_.count(row.binding.id)){auto record=history_->get(row.binding.id);protocol::check(record.has_value());loadRecord(std::move(*record));}
  historyCursor_=page.cursor;historyMore_=page.more;return page;
 }
 void visibleHistory(const std::string& group){
  if(group.empty()||group==visibleGroup_)return;
  historyPage(group,INT64_MAX,HistoryStore::MaxPage);visibleGroup_=group;
 }
 void enqueue(Job* j,const std::string& operation,V value=V::map(),std::uint64_t offset=0,Blob frame={}){protocol::check(requests_.size()<32);auto id=Storage::newMessageId();value["type"]=V(operation);value["requestId"]=V(id);value["generation"]=V(generation_);requests_.push_back({id,j?j->id:std::string{},operation,std::move(value),j?j->route:0,offset,std::move(frame),false});}
 static V object(const std::string& id){auto v=V::map();v["id"]=V(id);return v;}
 bool canceledResult(Job& j,const Request& q,const V* value=nullptr){
  if(j.state!="canceled")return false;
  const auto state=value?value->string("state"):"network_unavailable";const bool accepted=state=="accepted";
  if(accepted&&q.operation=="cache_release"){j.lease.clear();j.admitted=false;j.route=0;j.error.clear();save();return true;}
  if(state=="cache_attachment_unavailable"||state=="cache_attachment_expired"){j.lease.clear();j.admitted=false;j.route=0;save();return true;}
  if(state=="cache_lease_expired"||state=="cache_lease_invalid")j.lease.clear();
  if(!accepted){j.retryAt=wall()+10000;j.error="Canceled; server cleanup will retry when available";save();return true;}
  if(accepted&&q.operation=="cache_acquire"){auto lease=value->string("lease");protocol::check(protocol::id(lease,32));j.lease=std::move(lease);}
  const bool admitted=accepted&&(q.operation=="cache_admit"||q.operation=="cache_status");if(admitted)j.admitted=true;
  if(!busy(j.id)){
   if(!j.lease.empty()){auto v=V::map();v["lease"]=V(j.lease);v["upload"]=V(j.sending);if(j.sending)v["abandonUpload"]=V(true);enqueue(&j,"cache_release",std::move(v));}
   else if(admitted&&j.sending&&!j.uploadComplete){auto v=object(j.id);v["upload"]=V(true);enqueue(&j,"cache_acquire",std::move(v));}
  }
  save();return true;
 }
 void failure(Job& j,const std::string& reason,bool retainLease=false){if(j.state=="canceled"){j.error="Canceled; server cleanup will retry when available";j.retryAt=wall()+10000;j.route=0;save();return;}j.error=reason;if(!retainLease)j.lease.clear();j.route=0;j.context.reset();j.retryAt=wall()+std::min<std::uint64_t>(60000,1000ull<<(std::min(++j.failures,6u)));j.state=(reason=="cache_attachment_expired"||reason=="cache_attachment_unavailable")?"expired":"paused";requests_.erase(std::remove_if(requests_.begin(),requests_.end(),[&](const auto& r){return r.job==j.id;}),requests_.end());save();}
 static bool sameRoster(const group_keys::RosterSnapshot& a,const group_keys::RosterSnapshot& b){
  if(a.legacyGroupId!=b.legacyGroupId||a.serverGroupId!=b.serverGroupId||a.distributorPeerId!=b.distributorPeerId||a.generation!=b.generation||a.creationProof!=b.creationProof||a.devices.size()!=b.devices.size())return false;
  for(const auto& x:a.devices){auto i=std::find_if(b.devices.begin(),b.devices.end(),[&](const auto& y){return x.accountId==y.accountId&&x.peerId==y.peerId&&x.role==y.role&&x.invited==y.invited;});if(i==b.devices.end())return false;}return true;
 }
 keys::Authority authority(const keys::Header& header,const V& value,Context& old,Context& current){auto original=rpc::roster(value.at("originalRoster"));auto latest=rpc::roster(value.at("currentRoster"));old=context_(original.legacyGroupId,&original);current=context_(latest.legacyGroupId,nullptr);protocol::check(sameRoster(current.roster,latest)&&sameRoster(old.roster,original)&&group_keys::GroupKeyManager::matchesSignedLedger(latest,current.ledger)&&header.recipientMembership==value.string("recipientMembership")&&value.integer("expiresUtc")<=253402300799ull);return {old.ledger,old.roster,current.ledger,current.roster,value.string("sourceMembership"),value.string("recipientMembership"),value.string("currentRecipientMembership"),value.string("currentSourceMembership"),value.integer("expiresUtc")*1000};}
 void receiveControl(Job& j,const Blob& frame){transfers_.receiveCacheCiphertext(j.peer,metadata(j),j.body->manifest.size,frame,monotonic());}
 static std::uint64_t monotonic(){return static_cast<std::uint64_t>(Platform::monotonic());}
 // This Offer is local to the admitted cache route, not a peer wire protocol.
 // Each acquired route establishes fresh negotiation credit. Keep one chunk in
 // flight: cache_read still waits for the durable ACK and cache_progress reply.
 void createOffer(Job& j){protocol::check(j.route);j.version=2;j.negotiation=j.route;auto m=metadata(j);auto frame=control(j,transfer::Kind::Offer);protocol::Writer w(protocol::FileTransfer);w.data=std::move(frame);w.string(m.name);w.number(m.size);w.bytes(Blob(m.digest.begin(),m.digest.end()));w.number(transfer::ExtendedChunkBytes);w.number(1);receiveControl(j,w.data);auto rows=transfers_.snapshots();auto row=std::find_if(rows.begin(),rows.end(),[&](const auto& x){return x.metadata.id==j.id;});if(row!=rows.end()&&row->state==transfer::State::AwaitingAcceptance)transfers_.accept(j.id,platform_.sink(receivedDirectory_,m,nullptr),monotonic());}
 void beginPreparation(Job& j,const Context& current,std::unique_ptr<transfer::Source> input){
  protocol::check(group_keys::GroupKeyManager::matchesSignedLedger(current.roster,current.ledger)&&group_keys::GroupKeyManager::currentMember(current.roster,account_,profile_.peerId));
  protocol::check(input&&input->size()<=transfer::MaxCachedFileBytes);j.source=std::move(input);j.inputSize=j.source->size();j.state="preparing";j.error.clear();j.name=j.source->name();Binding binding{j.id,current.roster.serverGroupId,profile_.peerId,account_,current.roster.generation};auto length=j.source->size();transfer::Metadata m{j.id,"ciphertext.peerchat",HeaderBytes+length+std::max<std::uint64_t>(1,(length+BlockBytes-1)/BlockBytes)*TagBytes,{}};j.sink=platform_.sink(cipherDirectory_,m,preparationCleanup_.get());j.plainHash=std::make_unique<Sha256>();j.cipherHash=std::make_unique<Sha256>();j.sealer=std::make_unique<Sealer>(*j.source,*j.sink,std::move(binding),*j.plainHash,*j.cipherHash);
 }
 void prepare(Job& j){if(!j.sealer)return;if(!j.sealer->step())return;auto manifest=j.sealer->takeManifest();j.sealer.reset();auto body=std::make_unique<Body>(Body{j.time,j.order,Presentation::File,"application/octet-stream",std::move(manifest)});presentation(*body);if(thumbnailer_)try{auto image=thumbnailer_(*j.source);if(attachment_thumbnails::valid(image))body->thumbnail=std::move(image);}catch(...){}
  j.sourceLocation=platform_.location(*j.sink);auto secret=encodeBody(*body);auto copy=decodeBody(secret.bytes);staged_.push_back({std::move(copy.manifest),j.sourceLocation,j.time,j.time+transfer::RetentionMilliseconds});staging_.save(staged_);
#ifdef PEERCHAT_ATTACHMENT_APPLICATION_TESTS
  if(pauseAfterStaging){pauseAfterStaging=false;stagingPaused=true;return;}
#endif
  j.descriptor=uploadDescriptor(body->manifest,j.id,profile_);j.body=std::move(body);j.state="uploading";save();j.sink->commit();j.sink.reset();j.source.reset();j.plainHash.reset();j.cipherHash.reset();startUpload(j);
 }
 void retireStaging(const std::string& id){if(std::none_of(staged_.begin(),staged_.end(),[&](const auto& value){return value.manifest.binding.attachment==id;}))return;std::vector<Staged> retained;for(auto& value:staged_)if(value.manifest.binding.attachment!=id)retained.push_back(std::move(value));staged_.swap(retained);staging_.save(staged_);}
 void startUpload(Job& j){auto rows=transfers_.snapshots();if(std::any_of(rows.begin(),rows.end(),[&](const auto& s){return s.metadata.id==j.id;})){retireStaging(j.id);return;}auto copy=decodeBody(encodeBody(*j.body).bytes);Staged staged{std::move(copy.manifest),j.sourceLocation,j.time,j.time+transfer::RetentionMilliseconds};transfers_.offerCacheCiphertext(j.peer,metadata(j),j.body->manifest.size,platform_.staged(staged,profile_.peerId),monotonic());retireStaging(j.id);}
 void verify(Job& j){
  if(!j.finalizer){
   if(j.sending){auto copy=decodeBody(encodeBody(*j.body).bytes);Staged staged{std::move(copy.manifest),j.sourceLocation,j.time,j.time+transfer::RetentionMilliseconds};j.finalizer=platform_.finalizer(journal_,platform_.staged(staged,profile_.peerId),j.body->manifest,receivedDirectory_);}
   else{protocol::check(!j.cipherLocation.empty());transfer::DurableState d;d.purpose=transfer::Purpose::GroupCacheCiphertext;d.state=transfer::State::Complete;d.metadata=metadata(j);d.done=d.metadata.size;d.location=j.cipherLocation;j.finalizer=platform_.finalizer(journal_,d,j.body->manifest,j.destination);}
  }
  if(!j.finalizer->step())return;
  protocol::check(j.finalizer->verified());j.outputPath=j.finalizer->path();j.outputToken=j.finalizer->token();
  if(!j.sending){j.verified=true;j.state="complete";if(j.receipt.empty()){protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/group-attachment-verified/v1");w.bytes(j.descriptor);w.number(account_);w.string(profile_.peerId);w.bytes(profile_.publicKey);w.number(wall());w.bytes(signDevicePayload(w.data,profile_));j.receipt=std::move(w.data);}}
  save();journal_.forgetOutput(j.body->manifest,j.outputToken);j.finalizer.reset();j.outputRequested=true;
 }
 void outputStep(Job& j){
  if(j.outputToken.empty()){protocol::check(j.sending&&j.body);verify(j);return;}
  if(!j.output)j.output=platform_.output(j.body->manifest,j.outputToken);
  if(!j.output->verified()){j.output->step();return;}
  j.outputRequested=false;j.outputChecked=true;
  if(j.exportState=="pending"||j.exportState=="copying"||(j.exportState=="complete"&&!j.exportCleaned)){
   if(!j.exportJob){j.exportJob=std::make_unique<OutputExport>(*j.output,j.exportDestination,j.exportId,j.exportApproval,j.exportToken);j.exportToken=j.exportJob->resumeToken();j.exportState="copying";save();}
   if(j.exportJob->step()){j.exportState="complete";j.exportError.clear();save();j.exportJob->finishCleanup();j.exportCleaned=true;save();}
  }else if(j.exportJob&&!j.exportJob->complete())j.exportJob->step();
 }

 void selectExport(Job& j,const std::wstring& path,Blob approval={}){
  auto selected=std::filesystem::path(path);protocol::check(selected.is_absolute()&&selected==selected.lexically_normal()&&transfer::safeName(unicode::encode(selected.filename().wstring())));
  typename Platform::Directories hold(selected.parent_path());if(!j.exportError.empty()&&j.error==j.exportError)j.error.clear();
  if(!j.exportToken.empty()&&j.exportState!="complete"&&j.exportDestination==path&&(j.exportApproval==approval||Platform::publishedExportMatches(j.exportDestination,j.exportId,j.exportApproval,j.exportToken))){j.exportJob.reset();j.exportState="pending";j.exportError.clear();j.outputRequested=j.verified||j.sending;return;}
  if(!j.exportToken.empty()&&!j.exportCleaned){auto backup=std::filesystem::path(j.exportDestination).parent_path()/unicode::decode(j.exportId+".peerchat-replaced");Platform::requireMissing(backup);j.exportJob.reset();Platform::cancelPendingExport(j.exportDestination,j.exportId,j.exportApproval,j.exportToken);}
  if(approval.empty()){Platform::requireMissing(path);}
  // Only local protected job state contains this choice. Authenticated metadata
  // and ciphertext, signed receipts and transfer ACKs are unchanged.
  j.exportJob.reset();j.exportDestination=path;j.exportApproval=std::move(approval);j.exportToken.clear();j.exportId=Storage::newMessageId();j.exportState="pending";j.exportCleaned=false;j.exportError.clear();j.outputRequested=j.verified||j.sending;
 }
 static std::pair<std::wstring,Blob> selection(const std::wstring& encoded){auto bytes=rpc::unbase64(unicode::encode(encoded),65536,false);protocol::Reader r(bytes);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/DownloadSelection/v1");auto path=r.text();auto approval=r.bytes();r.end();return {std::move(path),std::move(approval)};}

public:
#if defined(__ANDROID__)
 // Local timeline suppression does not own the lifetime of admitted work.
 bool hasActiveTransfer(const std::string& id)const{auto found=jobs_.find(id);return found!=jobs_.end()&&(active(*found->second)||busy(id)||localWork(*found->second));}
#endif
 void thumbnailer(std::function<Blob(transfer::Source&)> value){thumbnailer_=std::move(value);}
 void visitPublicReferences(const std::function<void(const Blob&)>& visit){
  if(!account_)return;
  if(!jobs_.empty()){auto it=jobs_.upper_bound(referenceVisit_);if(it==jobs_.end())it=jobs_.begin();
   for(size_t n=0;n<8&&n<jobs_.size();++n){auto& j=*it->second;referenceVisit_=it->first;if(!j.descriptor.empty()){auto d=parseDescriptor(j.descriptor);if(storage_.groupHistoryReference(d.group,d.message).empty())visit(j.descriptor);}if(++it==jobs_.end())it=jobs_.begin();}}
  if(wall()<referenceBackfillAt_)return;
  // Read existing completed attachment history in bounded pages, without loading
  // jobs, reopening downloads, or copying any private body into the reference.
  auto page=history_->page({},referenceBackfillBefore_,8);for(const auto& item:page.items){auto record=history_->get(item.binding.id);protocol::check(record.has_value());protocol::Reader r(record->payload.bytes);protocol::check(r.type()==protocol::FileTransfer);auto format=r.string();protocol::check((format=="PeerChat/AttachmentJob/v1"||format=="PeerChat/AttachmentJob/v2"||format=="PeerChat/AttachmentJob/v3"||format=="PeerChat/AttachmentJob/v4")&&r.string()==profile_.peerId);protocol::check(r.string()==item.binding.id&&r.string()==item.binding.group&&r.string()==item.binding.server);for(unsigned n=0;n<3;++n)r.string();for(unsigned n=0;n<9;++n)r.number();auto descriptor=r.bytes();if(!descriptor.empty()){auto d=parseDescriptor(descriptor);if(storage_.groupHistoryReference(d.group,d.message).empty())visit(descriptor);}}
  referenceBackfillBefore_=page.more?page.cursor:INT64_MAX;if(!page.more)referenceBackfillAt_=wall()+60000;
 }
 BasicApplication(Storage& storage,const Profile& profile,transfer::TransferManager& transfers,CacheStateStore& journal,std::wstring directory,ContextLookup context,KeyAccess keys):platform_(storage,profile,directory),storage_(storage),profile_(profile),transfers_(transfers),journal_(journal),context_(std::move(context)),keys_(std::move(keys)),directory_(std::move(directory)),staging_(storage,profile.peerId){auto directories=platform_.directories(directory_);cipherDirectory_=directories.first;receivedDirectory_=directories.second;staged_=staging_.load();load();preparationCleanup_=platform_.cleanup(directory_);std::set<std::string> retained;for(auto& staged:staged_)retained.insert(staged.manifest.binding.attachment);for(auto& pair:jobs_)if(pair.second->sending&&pair.second->body)retained.insert(pair.first);preparationCleanup_->recover(retained);}
 std::unique_ptr<transfer::Source> reopen(const transfer::DurableState& d){if(!jobs_.count(d.metadata.id)){auto record=history_->get(d.metadata.id);protocol::check(record.has_value());loadRecord(std::move(*record));}auto& j=job(d.metadata.id);protocol::check(j.body&&j.sending);auto copy=decodeBody(encodeBody(*j.body).bytes);Staged staged{std::move(copy.manifest),j.sourceLocation,j.time,j.time+transfer::RetentionMilliseconds};return platform_.staged(staged,profile_.peerId);}
 std::uint64_t route(const transfer::Snapshot& s)const{auto j=jobs_.find(s.metadata.id);return j!=jobs_.end()&&j->second->server==server_&&account_&&!j->second->lease.empty()&&(!j->second->sending||j->second->published||j->second->earlyBlocked)?j->second->route:0;}
 size_t appendCredit(const transfer::Snapshot& s)const{
  if(!s.sending||!route(s)||requests_.size()>=24)return 0;
  size_t pending=0;for(const auto& q:requests_)if(q.job==s.metadata.id&&q.operation=="cache_append")++pending;
  return pending>=8?0:std::min(size_t(8)-pending,size_t(24)-requests_.size());
 }
 bool send(const transfer::Snapshot& s,const Blob& frame){if(requests_.size()>=24)return false;auto& j=job(s.metadata.id);if(!route(s))return false;protocol::Reader r(frame);protocol::check(r.type()==protocol::FileTransfer);j.version=r.number();auto kind=static_cast<transfer::Kind>(r.number());protocol::check(r.string()==j.id);j.negotiation=j.version==2?r.number():0;auto value=V::map();value["lease"]=V(j.lease);
  if(j.sending){if(kind==transfer::Kind::Offer){enqueue(&j,"cache_status",object(j.id),0,frame);return true;}if(kind==transfer::Kind::Chunk){auto offset=r.number();auto data=r.bytes();r.end();value["offset"]=V(offset);value["data"]=V(rpc::base64(data));enqueue(&j,"cache_append",std::move(value),offset+data.size(),frame);return true;}if(kind==transfer::Kind::Finish){value["bounded"]=V(true);enqueue(&j,"cache_complete",std::move(value),0,frame);return true;}}
  else {if(kind==transfer::Kind::Resume){auto offset=r.number();r.end();value["offset"]=V(offset);enqueue(&j,"cache_progress",std::move(value),offset);return true;}if(kind==transfer::Kind::Complete){j.state="verifying";save();return true;}}
  if(kind==transfer::Kind::Cancel){value["upload"]=V(j.sending);if(j.sending&&j.state=="canceled")value["abandonUpload"]=V(true);enqueue(&j,"cache_release",std::move(value));return true;}return false;
 }
 // Definitions below keep UI commands, scheduler and response transitions local.
 void eraseLocalHistory(const std::string& group,const std::string& id={}){
  protocol::check(protocol::id(group,32));
  if(!id.empty()){
   storage_.eraseGroupMessage(group,id);if(protocol::id(id,64))return;
   auto record=history_->get(id);
   if(record){protocol::Reader r(record->payload.bytes);protocol::check(r.type()==protocol::FileTransfer);auto format=r.string();protocol::check(format.rfind("PeerChat/AttachmentJob/v",0)==0);protocol::check(r.string()==profile_.peerId&&r.string()==id&&r.string()==group);for(unsigned i=0;i<4;++i)r.string();for(unsigned i=0;i<9;++i)r.number();auto descriptor=r.bytes();if(!descriptor.empty())storage_.eraseGroupMessage(group,parseDescriptor(descriptor).message);}
   return;
  }
  std::uint64_t before=INT64_MAX;
  for(;;){auto page=history_->page(group,before,HistoryStore::MaxPage);for(const auto& row:page.items)storage_.eraseGroupMessage(group,row.binding.id);if(!page.more)break;protocol::check(page.cursor>0&&page.cursor<before);before=page.cursor;}
  for(const auto& pair:jobs_)if(pair.second->local==group)storage_.eraseGroupMessage(group,pair.first);
 }
 std::wstring command(unsigned,const std::string&,const std::string&,const std::wstring&);
 // Only an already verified internal plaintext copy may be previewed. Rehash
 // the bounded read so replacement between verification and opening fails closed.
 std::optional<Blob> textPreview(const std::string& id,std::uint64_t limit){
  if(!jobs_.count(id))return std::nullopt;
  auto& j=job(id);
  protocol::check(j.body&&j.output&&j.output->verified()&&j.body->manifest.size<=limit);
  auto source=platform_.source(j.output->path());protocol::check(source->size()==j.body->manifest.size);
  Sha256 hash;Blob plain;plain.reserve(static_cast<size_t>(source->size()));
  for(std::uint64_t at=0;at<source->size();at+=transfer::ChunkBytes){auto bytes=source->read(at,static_cast<size_t>(std::min<std::uint64_t>(transfer::ChunkBytes,source->size()-at)));hash.update(bytes.data(),bytes.size());plain.insert(plain.end(),bytes.begin(),bytes.end());}
  protocol::check(hash.finish()==j.body->manifest.plainDigest);unicode::decode(std::string(plain.begin(),plain.end()));return plain;
 }
 Blob network(unsigned,const Blob&);
 void tick();
private:
 void reply(Request&,const V&);
 V snapshot(const std::string&);
};

template<class Platform> inline void BasicApplication<Platform>::reply(Request& q,const V& value){
 if(q.operation=="cache_pending_keys"&&value.kind==6&&value.has("state")){
  auto state=value.string("state");
  capabilityCode_=state;capabilityStage_="rejected";
  if(state=="cache_device_session_required"||state=="session_invalid"){supported_=false;capabilityStage_="device_verification_required";availability_="Device verification required for encrypted group attachments. Use Attach to verify this Windows device; existing messages and keys are unchanged.";pollAt_=wall()+30000;return;}
  if(state=="cache_disabled"||state=="cache_feature_gated"||state=="cache_storage_unavailable"){supported_=false;availability_=state=="cache_storage_unavailable"?"Server encrypted attachment storage is unavailable":"Group attachments require a compatible server with its encrypted cache enabled";pollAt_=wall()+30000;return;}
 }
 protocol::check(value.string("type")=="cache_result"&&value.string("requestId")==q.id&&value.string("operation")==q.operation&&value.integer("cacheVersion")==1);
 if(q.operation=="cache_pending_keys")capabilityStage_=value.string("state")=="accepted"?"ready":"rejected";
 if(!q.job.empty()&&canceledResult(job(q.job),q,&value))return;
 const auto state=value.string("state");
 if(state=="cache_lease_expired"&&q.operation=="cache_release"&&!q.job.empty()){
  auto& released=job(q.job);released.lease.clear();released.route=0;released.error.clear();save();return;
 }
 if(state=="cache_upload_incomplete"&&!q.job.empty()){
  auto& waiting=job(q.job);if(waiting.sending){waiting.earlyBlocked=true;waiting.context.reset();waiting.keysLoaded=false;waiting.retryAt=0;}
  else{waiting.error.clear();waiting.retryAt=wall()+1000;if(waiting.accepted)waiting.state="waiting_for_upload";}save();return;
 }
 if(q.operation=="cache_pending_keys"){supported_=state=="accepted";availability_=supported_?"":state;}if(state!="accepted"){if(q.operation=="cache_status"&&q.frame.empty()&&state=="cache_attachment_unavailable"&&!job(q.job).admitted){auto v=V::map();v["descriptor"]=V(rpc::base64(job(q.job).descriptor,false));enqueue(&job(q.job),"cache_admit",std::move(v));return;}if(!q.job.empty()){auto& j=job(q.job);if(j.verified&&(q.operation=="cache_verified"||q.operation=="cache_release")){j.error=state;j.retryAt=wall()+30000;if(state=="cache_attachment_expired"||state=="cache_attachment_unavailable"||state=="cache_group_not_authorized"||state=="cache_original_membership_required"){j.published=true;j.lease.clear();j.route=0;}save();return;}failure(j,state);}else pollAt_=wall()+10000;return;}
 if(q.operation=="cache_pending_keys"){
  protocol::check(value.at("items").kind==5&&value.at("items").array.size()<=8);for(auto& row:value.at("items").array){auto wire=rpc::unbase64(row.string("envelope"),keys::MaxWire);auto envelope=keys::parse(wire);auto d=parseDescriptor(envelope.header.descriptor);protocol::check(d.attachment==row.string("id")&&envelope.header.recipient.device==profile_.peerId&&envelope.header.recipient.accountId==account_&&Storage::publicKeyId(wire)==row.string("digest"));if(!jobs_.count(d.attachment)){auto stored=history_->get(d.attachment);if(stored)loadRecord(std::move(*stored));}if(jobs_.count(d.attachment)){auto& old=job(d.attachment);protocol::check(old.server==server_&&old.descriptor==d.wire&&(old.sending||old.receivedEnvelope==wire));if(!old.sending&&old.body&&!busy(old.id)){auto ack=object(old.id);ack["digest"]=V(Storage::publicKeyId(old.receivedEnvelope));enqueue(&old,"cache_key_ack",std::move(ack));}continue;}protocol::check(history_->count({},HistoryStore::Filter::Active)<HistoryStore::MaxActive);auto j=std::make_unique<Job>();j->id=d.attachment;j->server=server_;j->peer=peer(server_,d.attachment);j->descriptor=d.wire;j->receivedEnvelope=std::move(wire);auto current=context_(d.group,nullptr);j->local=current.roster.legacyGroupId;j->name="Encrypted attachment";j->time=envelope.header.created;j->state="available";jobs_.emplace(j->id,std::move(j));save();}cursor_=value.string("cursor");if(!value.at("more").flag()){cursor_.clear();pollAt_=wall()+5000;}else pollAt_=0;return;
 }
 auto& j=job(q.job);
 if(q.operation=="cache_admit"){j.admitted=true;save();j.state="uploading";j.error.clear();auto request=object(j.id);request["upload"]=V(true);enqueue(&j,"cache_acquire",std::move(request));return;}
 if(q.operation=="cache_acquire"){j.lease=value.string("lease");protocol::check(protocol::id(j.lease,32));if(!j.sending&&!j.verified)j.state="downloading";save();j.route=nextRoute_++;transfers_.cacheCapability(transfer::Purpose::GroupCacheCiphertext,j.peer,j.id,j.route,true);if(j.sending)startUpload(j);else createOffer(j);return;}
 if(q.operation=="cache_status"){protocol::check(value.integer("ciphertextSize")==metadata(j).size);if(q.frame.empty()){j.admitted=true;save();auto request=object(j.id);request["upload"]=V(true);enqueue(&j,"cache_acquire",std::move(request));}else receiveControl(j,control(j,transfer::Kind::Resume,value.integer("durableOffset")));return;}
 if(q.operation=="cache_append"){protocol::check(value.integer("durableOffset")==q.offset);receiveControl(j,control(j,transfer::Kind::Resume,q.offset));return;}
 if(q.operation=="cache_complete"){if(!value.at("ciphertextComplete").flag()){enqueue(&j,"cache_complete",std::move(q.value),0,q.frame);return;}receiveControl(j,control(j,transfer::Kind::Complete));j.uploadComplete=true;j.state="available";j.error.clear();save();return;}
 if(q.operation=="cache_context"){
  protocol::check(rpc::unbase64(value.string("descriptor"),2048)==j.descriptor&&value.string("id")==j.id);protocol::check(value.integer("expiresUtc")<=253402300799ull);j.context=value;j.expires=value.integer("expiresUtc")*1000;
  if(!j.sending&&!j.body){auto envelope=keys::parse(j.receivedEnvelope);protocol::check(value.string("recipientMembership")==envelope.header.recipientMembership);Context old,current;auto admission=authority(envelope.header,value,old,current);keys_([&](DeviceKeyManager& manager,platform::HpkeProvider& hpke,platform::IdentityProvider&){j.body=std::make_unique<Body>(keys::open(envelope,admission,profile_.peerId,account_,manager,hpke,wall()));});j.name=j.body->manifest.name;j.time=j.body->time;j.order=j.body->order;storage_.observeLogicalOrder(j.order);j.state="available";j.error.clear();save();auto ack=object(j.id);ack["digest"]=V(Storage::publicKeyId(j.receivedEnvelope));enqueue(&j,"cache_key_ack",std::move(ack));}
  return;
 }
 if(q.operation=="cache_recipient_keys"){
  protocol::check(value.at("recipients").kind==5&&value.at("recipients").array.size()<=256&&j.context.has_value());auto previous=std::move(j.recipients);j.recipients.clear();j.publishIndex=0;j.keysLoaded=true;
  for(auto& row:value.at("recipients").array){Recipient p;p.account=row.integer("accountId");p.device=row.string("device");if(p.device==profile_.peerId)continue;auto existing=std::find_if(previous.begin(),previous.end(),[&](const auto& old){return old.account==p.account&&old.device==p.device;});if(existing!=previous.end()&&(existing->state=="verified"||existing->state=="available"||existing->state=="downloading")){j.recipients.push_back(std::move(*existing));continue;}if(row.string("keyState")!="available"){p.state="unavailable";p.error="Verified attachment recipient key unavailable";j.recipients.push_back(std::move(p));continue;}
   p.binding=rpc::unbase64(row.string("binding"),2048);auto binding=mailbox::parseBinding(p.binding);protocol::check(binding.accountId==p.account&&binding.device==p.device);keys::Header h{j.descriptor,row.string("sourceMembership"),row.string("membership"),binding,wall(),j.expires};auto target=*j.context;target["recipientMembership"]=V(h.recipientMembership);target["currentRecipientMembership"]=V(h.recipientMembership);Context old,current;auto admission=authority(h,target,old,current);admission.bodyVersion=row.has("attachmentBodyVersion")?row.integer("attachmentBodyVersion"):1;protocol::check(admission.bodyVersion==1||admission.bodyVersion==2);Blob wire;auto copy=decodeBody(encodeBody(*j.body).bytes);Staged staged{std::move(copy.manifest),j.sourceLocation,j.time,j.time+transfer::RetentionMilliseconds};keys_([&](DeviceKeyManager&,platform::HpkeProvider& hpke,platform::IdentityProvider& identity){wire=prepared_->ensure(staged,h,*j.body,admission,identity,hpke,wall());});p.binding=std::move(wire);p.state="pending";j.recipients.push_back(std::move(p));
  }save();return;
 }
 if(q.operation=="cache_publish_key"){protocol::check(j.publishIndex<j.recipients.size());j.recipients[j.publishIndex++].state="available";save();return;}
 if(q.operation=="cache_key_ack")return;
 if(q.operation=="cache_read"){auto data=rpc::unbase64(value.string("data"),65536);protocol::check(!data.empty()&&q.offset+data.size()<=metadata(j).size);auto frame=control(j,transfer::Kind::Chunk);protocol::Writer w(protocol::FileTransfer);w.data=std::move(frame);w.number(q.offset);w.bytes(data);receiveControl(j,w.data);return;}
 if(q.operation=="cache_progress"){j.readOffset=q.offset;if(q.offset==metadata(j).size)receiveControl(j,control(j,transfer::Kind::Finish));return;}
 if(q.operation=="cache_release"){j.lease.clear();j.route=0;save();return;}
 if(q.operation=="cache_verified"){j.error.clear();j.published=true;save();if(!j.lease.empty()){auto release=V::map();release["lease"]=V(j.lease);release["upload"]=V(false);enqueue(&j,"cache_release",std::move(release));}return;}
 if(q.operation=="cache_receipts"){
  protocol::check(value.at("items").kind==5&&value.at("items").array.size()<=8);for(auto& item:value.at("items").array){auto recipient=std::find_if(j.recipients.begin(),j.recipients.end(),[&](const auto& p){return p.device==item.string("device");});if(recipient==j.recipients.end())continue;recipient->done=item.integer("durableOffset");protocol::check(recipient->done<=metadata(j).size);if(item.at("plaintextVerified").flag()){auto wire=rpc::unbase64(item.string("receipt"),4096);protocol::check(Storage::publicKeyId(wire)==item.string("receiptDigest"));protocol::Reader r(wire);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/group-attachment-verified/v1"&&r.bytes()==j.descriptor&&r.number()==recipient->account&&r.string()==recipient->device);auto identity=r.bytes();auto at=r.number();auto end=r.cursor;auto sig=r.bytes();r.end();protocol::check(Storage::publicKeyId(identity)==recipient->device&&at>0&&at<=wall()+300000&&verifyDevicePayload(Blob(wire.begin(),wire.begin()+end),sig,identity));auto current=context_(j.local,nullptr);auto member=current.ledger.device(recipient->account,recipient->device);protocol::check(member&&member->publicKey==identity);recipient->receipt=std::move(wire);recipient->state="verified";}else if(recipient->done)recipient->state="downloading";}if(std::all_of(j.recipients.begin(),j.recipients.end(),[](const auto& p){return p.state=="verified";}))j.state="complete";save();if(value.at("more").flag()){auto request=object(j.id);request["after"]=V(value.string("cursor"));enqueue(&j,"cache_receipts",std::move(request));}else j.retryAt=wall()+5000;return;
 }
 throw std::runtime_error("Unsupported attachment response");
}

template<class Platform> inline Blob BasicApplication<Platform>::network(unsigned operation,const Blob& input){
 if(operation==0){protocol::Reader r(input);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/AttachmentSession/v1");auto account=r.number(),generation=r.number();auto server=r.string();r.end();protocol::check(!account||(generation&&protocol::id(server)));if(account==account_&&generation==generation_&&server==server_)return {};requests_.clear();prepared_.reset();account_=account;generation_=generation;server_=std::move(server);cursor_.clear();pollAt_=0;supported_=false;availability_="Waiting for authenticated cache capability";capabilityStage_="waiting";capabilityCode_.clear();for(auto& pair:jobs_){pair.second->route=0;pair.second->context.reset();pair.second->keysLoaded=false;}if(account_)prepared_=Platform::preparedKeys(storage_,profile_,directory_,server_);return {};}
 if(operation==1){protocol::check(input.empty());for(auto& q:requests_)if(!q.dispatched){q.dispatched=true;q.dispatchedAt=wall();if(q.operation=="cache_pending_keys")capabilityStage_="request_dispatched";return bytes(rpc::json(q.value));}return bytes("{}");}
 protocol::check((operation==2||operation==3)&&input.size()<=100000);protocol::Reader r(input);protocol::check(r.type()==protocol::FileTransfer&&r.string()==(operation==2?"PeerChat/AttachmentResponse/v1":"PeerChat/AttachmentFailure/v1"));auto generation=r.number();auto id=r.string();if(generation!=generation_||!account_)return {};auto found=std::find_if(requests_.begin(),requests_.end(),[&](const auto& q){return q.id==id&&q.dispatched;});if(found==requests_.end())return {};auto q=std::move(*found);requests_.erase(found);
 try{if(operation==3){auto reason=r.string();r.end();protocol::check(reason=="network_unavailable");if(!q.job.empty()){auto& j=job(q.job);if(canceledResult(j,q))return {};if(q.operation=="cache_key_ack"||j.verified){j.error="Cache connection unavailable; local verified data retained";j.retryAt=wall()+10000;save();}else failure(j,"Cache connection unavailable; waiting to retry",true);}else {supported_=false;capabilityStage_="network_unavailable";capabilityCode_="network_unavailable";availability_="Encrypted attachment capability could not be reached. Check the connection and retry Attach.";pollAt_=wall()+10000;}return {};}auto value=rpc::read(r);r.end();if(!q.job.empty()){auto& j=job(q.job);if(j.server!=server_||(q.route&&q.route!=j.route))return {};}reply(q,value);}catch(const std::exception&){if(!q.job.empty())failure(job(q.job),"Attachment validation or protected storage unavailable");else {supported_=false;capabilityStage_="invalid_response";capabilityCode_="cache_response_invalid";availability_="Encrypted attachment capability response failed validation. Reconnect and retry Attach.";pollAt_=wall()+10000;}throw;}return {};
}

template<class Platform> inline void BasicApplication<Platform>::tick(){
 if(!account_)return;
 auto now=wall();std::vector<Job*> work;
 // Only capability probes are expired here. Transfer retries remain owned by
 // TransferManager; late probe responses cannot claim a newer request ID.
 for(auto it=requests_.begin();it!=requests_.end();){if(it->job.empty()&&it->dispatched&&it->dispatchedAt&&now>=it->dispatchedAt&&now-it->dispatchedAt>=30000){it=requests_.erase(it);supported_=false;capabilityStage_="probe_timeout";capabilityCode_="cache_probe_timeout";availability_="Encrypted attachment capability request timed out. Check the server connection and retry Attach.";pollAt_=now+10000;}else ++it;}
 // A borrowed cancellation request can lose its response during disconnect.
 // Retain cleanup intent, but release the request slot after a bounded wait.
 std::vector<Request> canceledTimeouts;
 for(auto it=requests_.begin();it!=requests_.end();){if(!it->job.empty()&&it->dispatched&&it->dispatchedAt&&now>=it->dispatchedAt&&now-it->dispatchedAt>=30000&&job(it->job).state=="canceled"){canceledTimeouts.push_back(std::move(*it));it=requests_.erase(it);}else ++it;}
 for(const auto& q:canceledTimeouts)canceledResult(job(q.job),q);
 for(auto& pair:jobs_)if(pair.second->server==server_&&(pair.second->sealer||pair.second->state=="verifying"||pair.second->outputRequested||(pair.second->verified&&(pair.second->exportState=="pending"||pair.second->exportState=="copying"))||(pair.second->exportJob&&!pair.second->exportJob->complete()&&!pair.second->exportJob->failed())))work.push_back(pair.second.get());
 if(!work.empty()){auto& j=*work[workTurn_++%work.size()];bool preparing=static_cast<bool>(j.sealer);try{const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(4);if(j.sealer){for(unsigned n=0;n<32&&j.sealer&&std::chrono::steady_clock::now()<until;++n)prepare(j);}else if(j.state=="verifying"){for(unsigned n=0;n<32&&j.state=="verifying"&&std::chrono::steady_clock::now()<until;++n)verify(j);}else outputStep(j);}catch(const std::exception& error){if(preparing){j.sealer.reset();j.source.reset();j.sink.reset();j.state="failed";j.error="Attachment preparation failed; select the original file to retry";save();}else if(j.state=="verifying"){j.state="failed";j.error="Attachment plaintext verification or commit failed";save();}else if(j.outputRequested||j.exportJob||j.exportState=="pending"||j.exportState=="copying"){j.outputRequested=false;j.outputChecked=false;j.finalizer.reset();j.exportError=j.output&&j.output->verified()?Platform::saveFailureReason(error):"Retained internal copy could not be reverified; saving is blocked. Retry Save preserves the stored download.";j.exportState="failed";j.error=j.exportError;save();}else failure(j,"Attachment preparation or protected storage unavailable");}}
 for(auto& pair:jobs_){auto& j=*pair.second;
  if((j.state=="canceled"||(j.verified&&j.output&&j.output->verified()))&&(!j.sourceLocation.empty()||!j.cipherLocation.empty()))try{
   transfers_.forgetCacheCiphertext(j.peer,j.id,transfer::Purpose::GroupCacheCiphertext);retireStaging(j.id);
   protocol::check(bool(j.body));auto m=metadata(j);
   if(!j.sourceLocation.empty())platform_.discardCipher(j.sourceLocation,m);
   if(!j.cipherLocation.empty())platform_.discardCipher(j.cipherLocation,m);
   j.sourceLocation.clear();j.cipherLocation.clear();save();
  }catch(...){j.error=j.state=="canceled"?"Canceled; local encrypted file cleanup will retry":"Verified file retained; encrypted temporary file cleanup will retry";}
  if(j.state=="canceled"&&j.server==server_&&supported_&&now>=j.retryAt&&!busy(j.id)&&requests_.size()<24){
   if(!j.lease.empty()){auto v=V::map();v["lease"]=V(j.lease);v["upload"]=V(j.sending);if(j.sending)v["abandonUpload"]=V(true);enqueue(&j,"cache_release",std::move(v));}
   else if(j.sending&&j.admitted&&!j.uploadComplete)enqueue(&j,"cache_status",object(j.id));
   continue;
  }
  if(j.server!=server_||j.state=="canceled"||j.state=="expired"||j.state=="failed"||j.sealer||j.state=="verifying")continue;
  try{
   if(j.verified){if(!j.published&&!busy(j.id)&&now>=j.retryAt){auto v=object(j.id);v["receipt"]=V(rpc::base64(j.receipt));enqueue(&j,"cache_verified",std::move(v));}continue;}
   // Local completion never depends on reacquiring an expired/offline lease.
   if(!j.sending&&j.accepted&&j.body){for(auto& d:transfers_.completedRecords())if(d.metadata.id==j.id&&!d.sending){j.cipherLocation=d.location;j.state="verifying";save();break;}if(j.state=="verifying")continue;}
   if(busy(j.id)||now<j.retryAt)continue;
   if(j.expires&&now>=j.expires&&j.lease.empty()){j.state="expired";j.error="Cached attachment expired; completed local copies are preserved";save();continue;}
   if(!j.body){enqueue(&j,"cache_context",object(j.id));continue;}
   if(!j.lease.empty()&&!j.route&&!j.uploadComplete){j.route=nextRoute_++;transfers_.cacheCapability(transfer::Purpose::GroupCacheCiphertext,j.peer,j.id,j.route,true);if(j.sending)startUpload(j);else createOffer(j);continue;}
   if(j.sending){
    if(j.uploadComplete&&j.published&&std::all_of(j.recipients.begin(),j.recipients.end(),[](const auto& p){return p.state=="verified";}))continue;
    if(!j.uploadComplete){auto rows=transfers_.snapshots();for(auto& row:rows)if(row.metadata.id==j.id&&row.state==transfer::State::Complete){j.uploadComplete=true;save();break;}}
    if(j.uploadComplete||(j.admitted&&!j.earlyBlocked&&!j.published)){
     if(j.uploadComplete&&!j.lease.empty()){auto v=V::map();v["lease"]=V(j.lease);v["upload"]=V(true);enqueue(&j,"cache_release",std::move(v));continue;}
     if(!j.context){enqueue(&j,"cache_context",object(j.id));continue;}
     if(!j.keysLoaded){enqueue(&j,"cache_recipient_keys",object(j.id));continue;}
     if(!j.published){
      while(j.publishIndex<j.recipients.size()&&j.recipients[j.publishIndex].state!="pending")++j.publishIndex;
      if(j.publishIndex==j.recipients.size()){j.published=true;j.state=j.uploadComplete?"available":"uploading";save();}
      else {auto v=object(j.id);v["envelope"]=V(rpc::base64(j.recipients[j.publishIndex].binding));enqueue(&j,"cache_publish_key",std::move(v));}
     }else{
      if(std::any_of(j.recipients.begin(),j.recipients.end(),[](const auto& p){return p.state=="unavailable";})){j.keysLoaded=false;j.published=false;j.retryAt=now+10000;}
      auto v=object(j.id);v["after"]=V("");enqueue(&j,"cache_receipts",std::move(v));
     }continue;
    }
    if(j.lease.empty())enqueue(&j,"cache_status",object(j.id));
    continue;
   }
   if(!j.accepted)continue;
   if(j.lease.empty()){auto v=object(j.id);v["upload"]=V(false);enqueue(&j,"cache_acquire",std::move(v));continue;}
   auto rows=transfers_.snapshots();auto row=std::find_if(rows.begin(),rows.end(),[&](const auto& x){return x.metadata.id==j.id;});
   if(row==rows.end()||transfers_.hasPendingControl(j.id))continue;
   if(row->state==transfer::State::Transferring&&row->done==j.readOffset&&row->done<row->metadata.size){auto v=V::map();v["lease"]=V(j.lease);v["offset"]=V(row->done);v["maximum"]=V(static_cast<std::uint64_t>(std::min<std::uint64_t>(transfer::ExtendedChunkBytes,row->metadata.size-row->done)));enqueue(&j,"cache_read",std::move(v),row->done);}
  }catch(const std::exception&){failure(j,"Attachment validation or protected storage unavailable");}
 }
 if(prepared_&&now>=maintenanceAt_){std::set<std::string> protectedIds;for(auto& pair:jobs_)if(active(*pair.second))protectedIds.insert(pair.first);prepared_->expire(protectedIds,now,4);for(auto& pair:jobs_)if(pair.second->sealer||pair.second->body)protectedIds.insert(pair.first);for(auto& staged:staged_)protectedIds.insert(staged.manifest.binding.attachment);preparationCleanup_->recover(protectedIds);maintenanceAt_=now+60000;}
 if(now>=pollAt_&&requests_.size()<24&&!busy("")){auto v=V::map();v["after"]=V(cursor_);enqueue(nullptr,"cache_pending_keys",std::move(v));pollAt_=now+5000;}
}

template<class Platform> inline typename BasicApplication<Platform>::V BasicApplication<Platform>::snapshot(const std::string& local){auto root=V::map();root["supported"]=V(account_&&supported_);root["reason"]=V(account_?availability_:"Sign in to an authenticated device session");auto list=V::list();auto rows=transfers_.snapshots(monotonic());for(auto& pair:jobs_){auto& j=*pair.second;if(!local.empty()&&j.local!=local)continue;if(storage_.localMessageHidden(j.local,j.id,true)||(!j.descriptor.empty()&&storage_.localMessageHidden(j.local,parseDescriptor(j.descriptor).message,true)))continue;auto v=V::map();v["id"]=V(j.id);v["logicalId"]=V(j.id);v["group"]=V(j.local);v["name"]=V(j.name);v["thumbnail"]=V(j.body?rpc::base64(j.body->thumbnail,false):"");v["path"]=V(j.output&&j.output->verified()?unicode::encode(j.output->path()):"");v["canOpen"]=V(j.body&&(j.sending?(!j.sourceLocation.empty()||!j.outputToken.empty()):j.verified));v["outputPending"]=V(j.outputRequested);v["exportState"]=V(j.exportState);v["exportPath"]=V(j.exportState=="complete"?unicode::encode(j.exportDestination):"");v["exportError"]=V(j.exportError);v["error"]=V(j.error);v["time"]=V(j.time);v["state"]=V(!j.sending&&!j.body&&j.state=="available"?"verifying_offer":j.state);v["sending"]=V(j.sending);v["plaintextVerified"]=V(j.verified);v["sourceVerified"]=V(j.sending&&j.output&&j.output->verified());v["transferSize"]=V(j.body?j.body->manifest.cipherSize():0);v["size"]=V(j.body?j.body->manifest.size:j.sealer?j.sealer->size():j.inputSize);v["done"]=V((j.verified||j.uploadComplete)&&j.body?j.body->manifest.cipherSize():std::uint64_t(0));v["senderPeer"]=V(j.body?j.body->manifest.binding.sender:profile_.peerId);v["preparationRequired"]=V(j.state=="failed"&&j.sending&&!j.body&&j.descriptor.empty()&&!j.admitted);v["canRetry"]=V(j.state=="paused"||(j.state=="failed"&&j.sending&&!j.body&&j.descriptor.empty()&&!j.admitted));v["canCancel"]=V(!j.verified&&!j.uploadComplete&&j.state!="canceled"&&j.state!="failed"&&j.state!="expired");for(auto& row:rows)if(row.metadata.id==j.id){v["done"]=V(row.done);if(row.state==transfer::State::Suspended){v["state"]=V("paused");v["error"]=V(row.error);}}auto recipients=V::list();for(auto& p:j.recipients){auto x=V::map();x["account"]=V(p.account);x["device"]=V(p.device);x["name"]=V(p.device.substr(0,12));x["state"]=V(p.state);x["done"]=V(p.done);x["error"]=V(p.error);recipients.array.push_back(std::move(x));}v["recipients"]=std::move(recipients);list.array.push_back(std::move(v));}root["items"]=std::move(list);root["historyCursor"]=V(historyCursor_?std::to_string(historyCursor_):"");root["historyMore"]=V(historyMore_);return root;}

template<class Platform> inline std::wstring BasicApplication<Platform>::command(unsigned operation,const std::string& group,const std::string& id,const std::wstring& path){
 if(operation==0){visibleHistory(group);auto value=snapshot(group);value["capabilityStage"]=V(capabilityStage_);value["capabilityCode"]=V(capabilityCode_);value["capabilityGeneration"]=V(generation_);return unicode::decode(rpc::json(value));}
 if(operation==8){
  std::uint64_t before=INT64_MAX;
  if(!id.empty()){protocol::check(id.size()<=19&&id.front()!='0');before=0;for(char c:id){protocol::check(c>='0'&&c<='9'&&before<=(static_cast<std::uint64_t>(INT64_MAX)-(c-'0'))/10);before=before*10+c-'0';}protocol::check(before>0);}
  auto page=historyPage(group,before,32);visibleGroup_=group;auto value=snapshot(group);auto& items=value["items"].array;
  items.erase(std::remove_if(items.begin(),items.end(),[&](const auto& row){return std::none_of(page.items.begin(),page.items.end(),[&](const auto& entry){return entry.binding.id==row.string("id");});}),items.end());
  return unicode::decode(rpc::json(value));
 }
 if(operation==1||operation==7){protocol::check(account_&&supported_&&protocol::id(server_)&&history_->count({},HistoryStore::Filter::Active)<HistoryStore::MaxActive);auto current=context_(group,nullptr);protocol::check(group_keys::GroupKeyManager::matchesSignedLedger(current.roster,current.ledger)&&group_keys::GroupKeyManager::currentMember(current.roster,account_,profile_.peerId));auto j=std::make_unique<Job>();j->id=Storage::newMessageId();j->local=current.roster.legacyGroupId;j->server=server_;j->peer=peer(server_,j->id);j->sending=true;j->time=wall();j->order=storage_.nextLogicalOrder();
  if(operation==1)j->source=platform_.source(path);else {struct Memory final:transfer::Source {SecretRecord value;std::string filename;Memory(Blob b,std::string n):value(std::move(b)),filename(std::move(n)){}std::uint64_t size()const override{return value.bytes.size();}std::string name()const override{return filename;}Blob read(std::uint64_t offset,size_t count)override{protocol::check(offset<=value.bytes.size()&&count<=transfer::ChunkBytes&&count<=value.bytes.size()-offset);return Blob(value.bytes.begin()+offset,value.bytes.begin()+offset+count);}};auto png=rpc::unbase64(unicode::encode(path),16*1024*1024,false);protocol::check(png.size()>=8&&Blob(png.begin(),png.begin()+8)==Blob({137,80,78,71,13,10,26,10})&&transfer::safeName(id));j->source=std::make_unique<Memory>(std::move(png),id);}
  beginPreparation(*j,current,std::move(j->source));auto result=j->id;jobs_.emplace(result,std::move(j));try{save();}catch(...){jobs_.erase(result);throw;}return unicode::decode(result);
 }
 if(!jobs_.count(id)){auto stored=history_->get(id);protocol::check(stored.has_value()&&stored->binding.group==group&&stored->binding.server==server_);loadRecord(std::move(*stored));}auto& j=job(id);protocol::check(j.local==group&&j.server==server_);
 if(operation==2||operation==9||operation==10){protocol::check(j.body&&!j.sending&&!j.verified&&!j.accepted&&!path.empty());if(operation==9||operation==10){auto chosen=operation==10?selection(path):std::make_pair(path,Blob{});selectExport(j,chosen.first,std::move(chosen.second));j.destination=receivedDirectory_;}else j.destination=path;j.outputPath.clear();j.accepted=true;j.state="downloading";j.retryAt=0;save();return L"ok";}
 if(operation==11){protocol::check(j.body&&(j.sending||j.verified));auto chosen=selection(path);selectExport(j,chosen.first,std::move(chosen.second));if(j.output&&j.output->failed())j.output.reset();j.outputRequested=true;save();return L"ok";}
 if(operation==3){protocol::check(!j.verified);if(j.finalizer)j.finalizer->cancel();j.finalizer.reset();j.outputRequested=false;for(const auto& row:transfers_.retainedRecords())if(row.metadata.id==j.id&&!row.sending)j.cipherLocation=row.location;if(j.sealer)j.sealer->cancel();j.sealer.reset();j.source.reset();j.sink.reset();transfers_.cancel(j.id);j.state="canceled";if(j.sending&&std::any_of(requests_.begin(),requests_.end(),[&](const auto& q){return q.job==j.id&&q.operation=="cache_admit"&&q.dispatched;}))j.admitted=true;requests_.erase(std::remove_if(requests_.begin(),requests_.end(),[&](const auto& q){return q.job==j.id&&!q.dispatched;}),requests_.end());if(!j.lease.empty()&&!busy(j.id)){auto v=V::map();v["lease"]=V(j.lease);v["upload"]=V(j.sending);if(j.sending)v["abandonUpload"]=V(true);enqueue(&j,"cache_release",std::move(v));}save();return L"ok";}
 if(operation==4){
  if(j.state=="failed"&&j.sending&&!j.body&&j.descriptor.empty()&&!j.admitted&&!path.empty()){
   protocol::check(account_&&supported_);auto current=context_(group,nullptr);auto input=platform_.source(path);protocol::check(input->name()==j.name&&input->size()==j.inputSize);
   beginPreparation(j,current,std::move(input));save();return L"ok";
  }
  protocol::check(j.state=="paused"&&j.body);j.retryAt=0;j.error.clear();j.lease.clear();j.route=0;j.state=j.sending?"uploading":j.accepted?"downloading":"available";save();return L"ok";}
 if(operation==6){protocol::check(j.body&&(j.sending||j.verified));if(j.output&&j.output->verified())return j.output->path();if(j.output&&j.output->failed())j.output.reset();j.outputRequested=true;j.exportError.clear();return L"";}
 if(operation==5){
  protocol::check(j.body&&(j.sending||j.verified)&&!path.empty());
  if(j.exportJob){if(j.exportDestination!=path&&(j.exportJob->complete()||j.exportJob->failed()))j.exportJob.reset();else{protocol::check(j.exportDestination==path&&!j.exportJob->failed());return j.exportJob->complete()?L"ok":L"";}}
  if(!j.output||!j.output->verified()){j.outputRequested=true;return L"";}
  selectExport(j,path);j.outputRequested=true;save();return L"";
 }
 throw std::runtime_error("Unsupported group attachment operation");
}
}
