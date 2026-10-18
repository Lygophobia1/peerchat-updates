// Audit observations only: no production delivery semantics are changed here.
// Reuse the accepted fixture's real CNG identity/key and shipping HPKE adapters.
#define main acceptedMailboxProtocolFixtureMain
#include "mailbox_protocol_test.cpp"
#undef main
#include "../platform/windows_sql_database.hpp"
#include "../delivery_status.hpp"

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
    const auto settledCandidates=senderStore.mailboxCandidates(0);
    auditNeed(std::none_of(settledCandidates.begin(),settledCandidates.end(),[&](const auto& row){return row.id==message.id;}),"stored envelopes cause repeated upload polling while awaiting recipient ACK");
    std::cout<<"SAFE: interrupted local Stored marker retains immutable upload retry\n";

    // Actual shared owner records no timestamp/deadline per submitted message.
    // A valid first-device ACK removes the logical outbox before any sibling
    // envelope exists; a later normal fanout cannot create that missing copy.
    auto live=queue();auditNeed(outgoing.authorizeTransport({900},target.peerId)&&outgoing.submittedMessage(target.peerId,live.id,{900}),"live admission fixture failed");
    auditNeed(outgoing.acknowledgeMessage(target.peerId,{900},live.id),"authenticated first-device ACK failed");
    auto late=outgoing.prepareMailbox(live.id,7,8,siblingBinding,other,sender,*hpke,now,true,false,&target);
    auditNeed(late.empty()&&senderStore.mailboxEnvelope(live.id,other.peerId).empty(),"current live-first policy unexpectedly fans out after Delivered");
    std::cout<<"OBSERVED POLICY GAP: first live-device ACK suppresses an unprepared sibling copy; Delivered is not all-device delivery\n";

    // Authenticated account-service eligibility is installed before the send.
    // The manager intersects it with current pins, then atomically stores the
    // logical body and immutable independent target manifest.
    const auto steady=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    mailbox::Writer eligibility("PeerChat/mailbox-eligibility-snapshot/v1");
    eligibility.number(7,8);eligibility.number(1,8);eligibility.text(sender.profile.peerId);
    eligibility.number(8,8);eligibility.number(1,4);eligibility.text(Storage::newMessageId());
    eligibility.number(1,8);eligibility.number(now,8);eligibility.number(now+120000,8);
    std::vector<std::pair<std::string,Blob>> eligibleRows{{target.peerId,mailbox::serializeBinding(binding)},
        {other.peerId,mailbox::serializeBinding(siblingBinding)}};
    std::sort(eligibleRows.begin(),eligibleRows.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    eligibility.number(eligibleRows.size(),1);
    for(const auto& row:eligibleRows){eligibility.text(row.first);eligibility.number(1,1);eligibility.field(row.second);}
    auditNeed(outgoing.installEligibility(eligibility.bytes,7,1,[&](const std::string& device){return friends.accountFor(device);},
        senderStore.contacts(),now,steady),"authenticated eligibility installation failed");
    auto rejectEligibility=[&](std::uint64_t account,std::uint64_t generation,std::uint64_t clock,const char* stage){
        auditReject([&]{outgoing.installEligibility(eligibility.bytes,account,generation,[&](const std::string& device){return friends.accountFor(device);},senderStore.contacts(),clock,steady);},"invalid eligibility was accepted");
        auditNeed(outgoing.eligibilityDiagnostic().find(std::string("native_eligibility=")+stage+" ")!=std::string::npos,"eligibility validation boundary was not retained");
    };
    rejectEligibility(9,1,now,"local_account_mismatch");
    rejectEligibility(7,2,now,"friend_generation_mismatch");
    rejectEligibility(7,1,now-1,"snapshot_issued_in_future");
    rejectEligibility(7,1,now+120000,"snapshot_expired");
    auditNeed(outgoing.installEligibility(eligibility.bytes,7,1,[&](const std::string& device){return friends.accountFor(device);},senderStore.contacts(),now,steady),"rejected snapshot corrupted prior verified cache");
    auditNeed(outgoing.eligibilityDiagnostic().find("native_eligibility=cached_replay ")!=std::string::npos,"verified snapshot replay diagnostic missing");
    StoredMessage targeted{Storage::newMessageId(),sender.profile.peerId,target.peerId,L"one logical body, two devices",now,
        Delivery::Queued,0,senderStore.nextLogicalOrder()};
    auditNeed(outgoing.queueMessageWithCachedSnapshot(targeted,7,8,1,now,steady),"targeted queue failed");
    auto frozen=senderStore.deliveryTargets(targeted.id);
    auditNeed(frozen&&!frozen->selectionUnresolved&&frozen->targets.size()==2&&
        senderStore.pending(target.peerId).back().id==targeted.id&&senderStore.pending(other.peerId).back().id==targeted.id,
        "independent target rows were not atomically frozen");
    auto targetedWire=outgoing.prepareMailbox(targeted.id,7,8,binding,target,sender,*hpke,now,true,false);
    auditNeed(!targetedWire.empty(),"frozen verified target failed mailbox preparation");
    auditNeed(outgoing.noteMailboxStoreFailure(targetedWire,"quota_exceeded",now+60000,now)&&
        senderStore.targetRetry(targeted.id,target.peerId)->retryUtcMs==now+60000&&
        !outgoing.eligibleForMailbox(targeted.id,target.peerId,binding,now),
        "exact target retry refusal was not protected or enforced");
    {Storage reopened((root/L"sender").wstring());auditNeed(reopened.targetRetry(targeted.id,target.peerId)->retryUtcMs==now+60000,
        "target retry deadline did not survive restart");}
    auto tamperedTargeted=targetedWire;tamperedTargeted.back()^=1;
    auditReject([&]{outgoing.noteMailboxStoreFailure(tamperedTargeted,"quota_exceeded",now+60000,now);},
        "tampered envelope changed target retry metadata");
    auditReject([&]{outgoing.noteMailboxStoreFailure(targetedWire,"unbounded_reason",now+60000,now);},
        "unknown refusal reason changed target retry metadata");
    auditNeed(senderStore.message(targeted.id)->delivery!=Delivery::Delivered,
        "retry metadata fabricated an acknowledgement");
    outgoing.mailboxStored(targeted.id,target.peerId,targetedWire);
    auditNeed(!senderStore.targetRetry(targeted.id,target.peerId),"accepted exact store retained obsolete retry delay");
    auditNeed(senderStore.pending(target.peerId).back().delivery==Delivery::Stored&&
        senderStore.pending(other.peerId).back().delivery==Delivery::Queued,
        "aggregate Stored incorrectly hid an unuploaded sibling target");
    auditReject([&]{senderStore.acknowledge(targeted.id,Delivery::Delivered);},"public aggregate ACK bypassed target verification");
    auditNeed(outgoing.authorizeTransport({902},target.peerId)&&outgoing.submittedMessage(target.peerId,targeted.id,{902}),
        "first independent live target submission failed");
    senderDb->fault.commit=true;
    auditReject([&]{outgoing.acknowledgeMessage(target.peerId,{902},targeted.id);},"target ACK commit fault did not fail");
    auto afterFailedAck=senderStore.deliveryTargets(targeted.id);
    auditNeed(std::all_of(afterFailedAck->targets.begin(),afterFailedAck->targets.end(),
        [](const auto& item){return item.status==DeliveryTargetStatus::Pending;}),
        "failed target ACK committed protected state");
    auditNeed(outgoing.acknowledgeMessage(target.peerId,{902},targeted.id),"first exact target ACK failed");
    auditNeed(senderStore.message(targeted.id)->delivery!=Delivery::Delivered&&
        senderStore.pending(other.peerId).back().id==targeted.id,
        "first target ACK incorrectly settled sibling or aggregate");
    {Storage reopened((root/L"sender").wstring());auto retained=reopened.deliveryTargets(targeted.id);
        auditNeed(retained&&retained->targets.size()==2&&reopened.message(targeted.id)->delivery!=Delivery::Delivered,
            "target manifest or partial state did not survive restart");
        auto projection=delivery_status::read(reopened,sender.profile.peerId,{*reopened.message(targeted.id)});
        auditNeed(projection.size()==1&&!projection.at(targeted.id).selectionUnresolved&&
            std::count_if(projection.at(targeted.id).targets.begin(),projection.at(targeted.id).targets.end(),
                [](const auto& row){return row.status==DeliveryTargetStatus::Acknowledged;})==1,
            "restarted partial delivery projection lost authenticated per-device ACK");
        auditNeed(delivery_status::read(reopened,target.peerId,{*reopened.message(targeted.id)}).empty(),
            "other-device history import invented local receipt authority");
        protocol::Writer page(protocol::Text);delivery_status::append(page,projection);protocol::Reader parsed(page.data);
        auditNeed(parsed.string()=="PeerChat/DeliveryTargetsSnapshot/v1"&&parsed.number()==1&&
            parsed.string()==targeted.id&&parsed.number()==0&&parsed.number()==2,"JNI delivery extension header mismatch");
        for(const auto& row:projection.at(targeted.id).targets)auditNeed(parsed.string()==row.device&&
            parsed.number()==static_cast<unsigned>(row.status),"JNI delivery extension target mismatch");
        parsed.end();
    }
    outgoing.invalidateEligibility(7,2);
    auditNeed(outgoing.pendingMessages(other.peerId).empty()&&
        !senderStore.pending(other.peerId).empty()&&
        !outgoing.eligibleForMailbox(targeted.id,other.peerId,siblingBinding,now),
        "stale friendship generation retained active sibling routing");
    friends.replace(7,2,{{8,target.peerId,{target.peerId,other.peerId}}});
    mailbox::Writer refreshed("PeerChat/mailbox-eligibility-snapshot/v1");
    refreshed.number(7,8);refreshed.number(2,8);refreshed.text(sender.profile.peerId);
    refreshed.number(8,8);refreshed.number(1,4);refreshed.text(Storage::newMessageId());
    refreshed.number(2,8);refreshed.number(now,8);refreshed.number(now+120000,8);
    refreshed.number(eligibleRows.size(),1);
    for(const auto& row:eligibleRows){refreshed.text(row.first);refreshed.number(1,1);refreshed.field(row.second);}
    auditNeed(outgoing.installEligibility(refreshed.bytes,7,2,[&](const std::string& device){return friends.accountFor(device);},
        senderStore.contacts(),now,steady)&&!outgoing.pendingMessages(other.peerId).empty(),
        "fresh authenticated eligibility did not resume same frozen sibling");
    friends.replace(9,3,{{8,target.peerId,{target.peerId,other.peerId}}});
    outgoing.invalidateEligibility(9,3);
    mailbox::Writer switched("PeerChat/mailbox-eligibility-snapshot/v1");
    switched.number(9,8);switched.number(3,8);switched.text(sender.profile.peerId);
    switched.number(8,8);switched.number(1,4);switched.text(Storage::newMessageId());
    switched.number(3,8);switched.number(now,8);switched.number(now+120000,8);
    switched.number(eligibleRows.size(),1);
    for(const auto& row:eligibleRows){switched.text(row.first);switched.number(1,1);switched.field(row.second);}
    auditNeed(outgoing.installEligibility(switched.bytes,9,3,[&](const std::string& device){return friends.accountFor(device);},
        senderStore.contacts(),now,steady)&&outgoing.pendingMessages(other.peerId).empty()&&
        outgoing.authorizeTransport({905},other.peerId)&&!outgoing.submittedMessage(other.peerId,targeted.id,{905}),
        "new local account reactivated an old account's frozen target");
    auditReject([&]{outgoing.noteMailboxStoreFailure(targetedWire,"quota_exceeded",now+60000,now);},
        "different local account changed old target retry metadata");
    friends.replace(7,4,{{8,target.peerId,{target.peerId,other.peerId}}});
    outgoing.invalidateEligibility(7,4);
    mailbox::Writer restored("PeerChat/mailbox-eligibility-snapshot/v1");
    restored.number(7,8);restored.number(4,8);restored.text(sender.profile.peerId);
    restored.number(8,8);restored.number(1,4);restored.text(Storage::newMessageId());
    restored.number(4,8);restored.number(now,8);restored.number(now+120000,8);
    restored.number(eligibleRows.size(),1);
    for(const auto& row:eligibleRows){restored.text(row.first);restored.number(1,1);restored.field(row.second);}
    auditNeed(outgoing.installEligibility(restored.bytes,7,4,[&](const std::string& device){return friends.accountFor(device);},
        senderStore.contacts(),now,steady)&&!outgoing.pendingMessages(other.peerId).empty(),
        "original account did not recover its own frozen sibling");
    auditNeed(outgoing.authorizeTransport({903},other.peerId)&&outgoing.submittedMessage(other.peerId,targeted.id,{903}),
        "sibling independent live target submission failed");
    auditNeed(outgoing.acknowledgeMessage(other.peerId,{903},targeted.id)&&
        senderStore.message(targeted.id)->delivery==Delivery::Delivered&&
        senderStore.pending(other.peerId).empty(),"all exact ACKs did not settle aggregate and outbox");
    auto completeProjection=delivery_status::read(senderStore,sender.profile.peerId,{*senderStore.message(targeted.id)});
    auditNeed(std::all_of(completeProjection.at(targeted.id).targets.begin(),completeProjection.at(targeted.id).targets.end(),
        [](const auto& row){return row.status==DeliveryTargetStatus::Acknowledged;}),"complete projection lost device ACKs");
    auto targetedHistory=senderStore.history(target.peerId,1000);
    auditNeed(std::count_if(targetedHistory.begin(),targetedHistory.end(),[&](const auto& row){return row.id==targeted.id;})==1,
        "independent copies duplicated canonical logical history");
    std::cout<<"SAFE: verified frozen sibling targets keep one logical history row; exact ACKs atomically settle aggregate only after all targets\n";

    outgoing.invalidateEligibility(7,5);
    StoredMessage unresolved{Storage::newMessageId(),sender.profile.peerId,target.peerId,L"unresolved selection",now,
        Delivery::Queued,0,senderStore.nextLogicalOrder()};
    auditNeed(outgoing.queueMessageWithCachedSnapshot(unresolved,7,8,5,now,steady),"unresolved queue failed");
    auditNeed(senderStore.deliveryTargets(unresolved.id)->selectionUnresolved,"missing fresh snapshot became resolved");
    auditNeed(outgoing.authorizeTransport({904},target.peerId)&&outgoing.submittedMessage(target.peerId,unresolved.id,{904})&&
        outgoing.acknowledgeMessage(target.peerId,{904},unresolved.id),"unresolved primary live delivery failed");
    const auto pendingAfterUnresolvedAck=senderStore.pending(target.peerId);
    auditNeed(senderStore.message(unresolved.id)->delivery==Delivery::Sent&&
        std::none_of(pendingAfterUnresolvedAck.begin(),pendingAfterUnresolvedAck.end(),
            [&](const auto& row){return row.id==unresolved.id;})&&
        senderStore.mailboxDiagnostics().find("selection=unresolved")!=std::string::npos,
        "unresolved primary ACK falsely claimed all-device delivery or repeated live route");
    auto unresolvedProjection=delivery_status::read(senderStore,sender.profile.peerId,{*senderStore.message(unresolved.id)});
    auditNeed(unresolvedProjection.at(unresolved.id).selectionUnresolved&&
        unresolvedProjection.at(unresolved.id).targets.front().status==DeliveryTargetStatus::Acknowledged,
        "unresolved selection was hidden by a received canonical copy");
    StoredMessage unresolvedOffline{Storage::newMessageId(),sender.profile.peerId,target.peerId,L"unresolved offline fallback",now,
        Delivery::Queued,0,senderStore.nextLogicalOrder()};
    auditNeed(outgoing.queueMessageWithCachedSnapshot(unresolvedOffline,7,8,5,now,steady),"unresolved offline queue failed");
    auto offlineWire=outgoing.prepareMailbox(unresolvedOffline.id,7,8,binding,target,sender,*hpke,now,true,false);
    auditNeed(!offlineWire.empty()&&outgoing.prepareMailbox(unresolvedOffline.id,7,8,siblingBinding,other,
        sender,*hpke,now,true,false,&target).empty(),"unresolved selection allowed a sibling or blocked canonical fallback");
    outgoing.mailboxStored(unresolvedOffline.id,target.peerId,offlineWire);
    auto offlineReceive=incoming.acceptMailbox(offlineWire,7,origin,binding,keys,receiver,*hpke,now);
    auditNeed(offlineReceive.inserted&&!offlineReceive.receipt.empty()&&
        outgoing.acknowledgeMailbox(offlineReceive.receipt,8,target),"unresolved canonical mailbox receipt failed");
    auditNeed(senderStore.deliveryTargets(unresolvedOffline.id)->selectionUnresolved&&
        senderStore.message(unresolvedOffline.id)->delivery!=Delivery::Delivered,
        "unresolved canonical receipt falsely claimed complete target selection");
    std::cout<<"SAFE: absent fresh eligibility preserves canonical live ACK without a false all-device Delivered result\n";

    // A key rotates before first server store. Current preparation returns the
    // old immutable wire, which production server rejects as a new retired-key upload.
    auto pending=queue();auto stale=outgoing.prepareMailbox(pending.id,7,8,binding,target,sender,*hpke,now,true,false);
    auto rotated=binding;rotated.keyId=Storage::newMessageId();rotated.generation=2;rotated.signature=receiver.sign(mailbox::bindingPayload(rotated));
    auditReject([&]{outgoing.prepareMailbox(pending.id,7,8,rotated,target,sender,*hpke,now,true,false);},"rotated key attempt was not isolated");
    auditNeed(senderStore.mailboxEnvelope(pending.id,target.peerId)==stale&&senderStore.mailboxIssue(pending.id,target.peerId)=="key_changed_requires_reconciliation","key-rotation did not preserve immutable wire with actionable issue");
    auditReject([&]{outgoing.prepareMailbox(pending.id,7,8,rotated,target,sender,*hpke,now+mailbox::MaxTtlMilliseconds,true,false);},"expired immutable envelope was not rejected");
    const auto uploads=senderStore.pendingMailboxUploads();auditNeed(std::any_of(uploads.begin(),uploads.end(),[&](const auto& row){return row.id==pending.id;}),"expired envelope no longer appears as pending");
    auditNeed(senderStore.mailboxIssue(pending.id,target.peerId)=="attempt_expired_requires_review","expiry lacks actionable issue");
    auditNeed(senderStore.mailboxDiagnostics().find("attempt_expired_requires_review")!=std::string::npos,"durable action required state is not available in diagnostics");
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
