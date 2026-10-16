// Audit observations only: no production delivery semantics are changed here.
// Reuse the accepted fixture's real CNG identity/key and shipping HPKE adapters.
#define main acceptedMailboxProtocolFixtureMain
#include "mailbox_protocol_test.cpp"
#undef main
#include "../platform/windows_sql_database.hpp"

static void auditNeed(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
template<class F>static void auditReject(F operation,const char* reason){bool failed=false;try{operation();}catch(const std::exception&){failed=true;}auditNeed(failed,reason);}
struct AuditFaultState {bool commit=false;std::string objectPrefix;};
struct AuditStatement final:platform::SqlStatement {
    std::unique_ptr<platform::SqlStatement> inner;AuditFaultState& fault;bool object;std::string key;
    AuditStatement(std::unique_ptr<platform::SqlStatement> value,AuditFaultState& state,bool objects):inner(std::move(value)),fault(state),object(objects){}
    void bindText(int i,const std::string& value)override{if(i==1)key=value;inner->bindText(i,value);}
    void bindBlob(int i,const Blob& value)override{inner->bindBlob(i,value);}
    void bindNumber(int i,std::int64_t value)override{inner->bindNumber(i,value);}
    bool row()override{if(object&&!fault.objectPrefix.empty()&&key.find(fault.objectPrefix)==0){fault.objectPrefix.clear();throw std::runtime_error("audit injected protected-object failure");}return inner->row();}
    Blob bytes(int i)override{return inner->bytes(i);}std::string text(int i)override{return inner->text(i);}std::int64_t number(int i)override{return inner->number(i);}
};
struct AuditDatabase final:platform::SqlDatabase {
    std::shared_ptr<platform::SqlDatabase> inner;AuditFaultState fault;
    explicit AuditDatabase(const std::wstring& path):inner(platform::openWindowsSqlDatabase(path)){}
    void execute(const std::string& sql)override{if(sql=="COMMIT"&&fault.commit){fault.commit=false;throw std::runtime_error("audit injected pre-commit failure");}inner->execute(sql);}
    std::unique_ptr<platform::SqlStatement> prepare(const std::string& sql)override{return std::make_unique<AuditStatement>(inner->prepare(sql),fault,sql.find("INSERT INTO objects(")==0);}
};

int main(int argc,char** argv){try{
    auditNeed(argc==2,"Pass isolated fixture directory");const auto root=std::filesystem::path(argv[1]);
    auto senderDb=std::make_shared<AuditDatabase>((root/L"sender").wstring());auto receiverDb=std::make_shared<AuditDatabase>((root/L"receiver").wstring());
    Storage senderStore(senderDb),receiverStore(receiverDb),siblingStore((root/L"sibling").wstring());
    Signing sender(senderStore.profile()),receiver(receiverStore.profile()),sibling(siblingStore.profile());
    Contact target{receiver.profile.peerId,L"Audit recipient",receiver.publicKey(),{},true},other{sibling.profile.peerId,L"Audit sibling",sibling.publicKey(),{},true},origin{sender.profile.peerId,L"Audit sender",sender.publicKey(),{},true};
    senderStore.saveContact(target);senderStore.saveContact(other);receiverStore.saveContact(origin);
    reliable::AuthenticatedFriendSnapshot friends;friends.replace(7,1,{{8,target.peerId,{target.peerId,other.peerId}}});
    delivery::DeliveryManager outgoing(senderStore,sender.profile.peerId),incoming(receiverStore,receiver.profile.peerId);
    RecipientFixture recipientKey;auto hpke=platform::createShippingHpke();
    const auto now=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    mailbox::RecipientBinding binding;binding.accountId=8;binding.device=target.peerId;binding.keyId=Storage::newMessageId();binding.generation=1;binding.createdUtc=now;binding.storageAllowed=true;binding.publicKey=recipientKey.key->publicKey();binding.signature=receiver.sign(mailbox::bindingPayload(binding));
    auto siblingBinding=binding;siblingBinding.device=other.peerId;siblingBinding.keyId=Storage::newMessageId();siblingBinding.signature=sibling.sign(mailbox::bindingPayload(siblingBinding));
    RegistryFixture registry;DeviceKeyMetadata key;key.id=binding.keyId;key.providerAlias=recipientKey.alias;key.algorithm=DeviceKeyManager::recipientSuite;key.generation=1;key.createdUtcMilliseconds=now;key.state=DeviceKeyState::Active;key.registration=DeviceKeyRegistration::Accepted;key.publicKey=binding.publicKey;key.signedBinding=mailbox::serializeBinding(binding);registry.entries.push_back(key);
    platform::WindowsRecipientKeyProvider provider;DeviceKeyManager keys(receiver,provider,registry);
    auto queue=[&]{return reliable::queueText(senderStore,sender.profile,&target,target.peerId,Storage::newMessageId(),L"isolated audit payload",now,&friends);};
    auto message=queue();auto wire=outgoing.prepareMailbox(message.id,7,8,binding,target,sender,*hpke,now,true,false);

    // Before commit: caller receives no receipt, protected row and outbox roll back.
    receiverDb->fault.commit=true;
    auditReject([&]{incoming.acceptMailbox(wire,7,origin,binding,keys,receiver,*hpke,now);},"pre-commit failure unexpectedly returned receipt");
    auditNeed(!receiverStore.message(message.id)&&receiverStore.mailboxReceipt(message.id,origin.peerId,delivery::DeliveryManager::mailboxDigest(wire)).empty(),"failed import produced durable row/receipt");
    auto received=incoming.acceptMailbox(wire,7,origin,binding,keys,receiver,*hpke,now);
    auditNeed(received.inserted&&!received.receipt.empty(),"valid import failed");
    // Drop the returned ACK, then reopen the protected receiver before retry.
    {Storage reopened((root/L"receiver").wstring());delivery::DeliveryManager retry(reopened,target.peerId);auto duplicate=retry.acceptMailbox(wire,7,origin,binding,keys,receiver,*hpke,now);auditNeed(!duplicate.inserted&&duplicate.receipt==received.receipt,"lost ACK/reopen did not preserve idempotence and exact receipt");}
    std::cout<<"SAFE: failed commit returns no receipt; lost ACK + protected reopen retries one durable message with identical signed receipt\n";

    // A committed receive whose receipt write fails must remain recoverable.
    auto receiptGap=queue();auto receiptWire=outgoing.prepareMailbox(receiptGap.id,7,8,binding,target,sender,*hpke,now,true,false);
    receiverDb->fault.objectPrefix="mailbox-receipt/";
    auditReject([&]{incoming.acceptMailbox(receiptWire,7,origin,binding,keys,receiver,*hpke,now);},"receipt persistence fault did not occur");
    auditNeed(receiverStore.message(receiptGap.id).has_value(),"receipt failure lost committed message");
    auto receiptRetry=incoming.acceptMailbox(receiptWire,7,origin,binding,keys,receiver,*hpke,now);
    auditNeed(!receiptRetry.inserted&&!receiptRetry.receipt.empty(),"post-commit receipt retry failed");
    std::cout<<"SAFE: post-commit receipt failure retains message and retry produces durable receipt without duplicate import\n";

    // After server acceptance/local Stored but before upload marker persistence:
    // the immutable envelope still drives a safe identical upload retry.
    senderDb->fault.objectPrefix="mailbox-uploaded/";
    auditReject([&]{outgoing.mailboxStored(message.id,target.peerId,wire);},"upload-marker fault did not occur");
    auditNeed(senderStore.message(message.id)->delivery==Delivery::Stored&&!senderStore.mailboxUploadAccepted(message.id,target.peerId)&&!senderStore.pendingMailboxUploads().empty(),"Stored/marker interruption lost pending ciphertext");
    outgoing.mailboxStored(message.id,target.peerId,wire);auditNeed(senderStore.mailboxUploadAccepted(message.id,target.peerId),"exact retry did not settle upload marker");
    std::cout<<"SAFE: interrupted local Stored marker retains immutable upload retry\n";

    // Actual shared owner records no timestamp/deadline per submitted message.
    // A valid first-device ACK removes the logical outbox before any sibling
    // envelope exists; a later normal fanout cannot create that missing copy.
    auto live=queue();auditNeed(outgoing.authorizeTransport({900},target.peerId)&&outgoing.submittedMessage(target.peerId,live.id,{900}),"live admission fixture failed");
    auditNeed(outgoing.acknowledgeMessage(target.peerId,{900},live.id),"authenticated first-device ACK failed");
    auto late=outgoing.prepareMailbox(live.id,7,8,siblingBinding,other,sender,*hpke,now,true,false,&target);
    auditNeed(late.empty()&&senderStore.mailboxEnvelope(live.id,other.peerId).empty(),"current live-first policy unexpectedly fans out after Delivered");
    std::cout<<"OBSERVED POLICY GAP: first live-device ACK suppresses an unprepared sibling copy; Delivered is not all-device delivery\n";

    // A key rotates before first server store. Current preparation returns the
    // old immutable wire, which production server rejects as a new retired-key upload.
    auto pending=queue();auto stale=outgoing.prepareMailbox(pending.id,7,8,binding,target,sender,*hpke,now,true,false);
    auto rotated=binding;rotated.keyId=Storage::newMessageId();rotated.generation=2;rotated.signature=receiver.sign(mailbox::bindingPayload(rotated));
    auditReject([&]{outgoing.prepareMailbox(pending.id,7,8,rotated,target,sender,*hpke,now,true,false);},"rotated key attempt was not isolated");
    auditNeed(senderStore.mailboxEnvelope(pending.id,target.peerId)==stale&&senderStore.mailboxIssue(pending.id,target.peerId)=="key_changed_requires_reconciliation","key-rotation did not preserve immutable wire with actionable issue");
    auditReject([&]{outgoing.prepareMailbox(pending.id,7,8,rotated,target,sender,*hpke,now+mailbox::MaxTtlMilliseconds,true,false);},"expired immutable envelope was not rejected");
    const auto uploads=senderStore.pendingMailboxUploads();auditNeed(std::any_of(uploads.begin(),uploads.end(),[&](const auto& row){return row.id==pending.id;}),"expired envelope no longer appears as pending");
    auditNeed(senderStore.mailboxIssue(pending.id,target.peerId)=="attempt_expired_requires_review","expiry lacks actionable issue");
    std::cout<<"SAFE: stale-key and expired attempts preserve immutable ciphertext and durable actionable state\n";

    auto stalled=queue();auditNeed(outgoing.authorizeTransport({901},target.peerId)&&outgoing.submittedMessage(target.peerId,stalled.id,{901},1000),"deadline admission failed");
    auditNeed(!outgoing.messageAckExpired({901},30999)&&outgoing.messageAckExpired({901},31000),"application ACK deadline boundary incorrect");
    auditNeed(senderStore.message(stalled.id)->delivery==Delivery::Queued,"timeout changed durable delivery state");
    auditNeed(outgoing.acknowledgeMessage(target.peerId,{901},stalled.id)&&!outgoing.messageAckExpired({901},31000),"late valid ACK did not settle exact submission");

    // Bounded local queue durability only, not a network throughput benchmark.
    reliable::AuthenticatedFriendSnapshot burstFriends;burstFriends.replace(7,2,{{8,target.peerId,{target.peerId}},{9,other.peerId,{other.peerId}}});
    const auto targetBefore=senderStore.pending(target.peerId,1000).size(),otherBefore=senderStore.pending(other.peerId,1000).size();
    for(unsigned i=0;i<500;++i){auto& contact=(i%2)?other:target;reliable::queueText(senderStore,sender.profile,&contact,contact.peerId,Storage::newMessageId(),L"bounded audit burst",now+i,&burstFriends);}
    {Storage reopened((root/L"sender").wstring());auditNeed(reopened.pending(target.peerId,1000).size()==targetBefore+250&&reopened.pending(other.peerId,1000).size()==otherBefore+250,"two-conversation burst lost durable queued rows");}
    std::cout<<"SAFE: 500 local durable queued messages across two conversations survive protected reopen; no network performance claim\n";
    std::set<std::string> reached;for(unsigned page=0;page<10;++page)for(const auto& row:outgoing.mailboxCandidates())reached.insert(row.id);
    auditNeed(reached.size()>=500,"rotating native candidate pages starve messages beyond first 64");
    std::cout<<"SAFE: bounded rotating native pages reach all 500 queued messages beyond the first 64\n";
    hpke->shutdown();return 0;
}catch(const std::exception& error){std::cerr<<"AUDIT FIXTURE FAILED: "<<error.what()<<'\n';return 1;}}
