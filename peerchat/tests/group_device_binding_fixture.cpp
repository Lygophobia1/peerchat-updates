#include "../group_device_binding.hpp"
#include "../group_device_reconciliation.hpp"
#include "../group_device_replica.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace peerchat;
static void write(const std::filesystem::path& path,const Blob& bytes){std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());if(!file)throw std::runtime_error("Fixture write failed");}
static std::string hex(const Blob& bytes){std::string result;const char* digits="0123456789abcdef";for(auto b:bytes){result+=digits[b>>4];result+=digits[b&15];}return result;}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Fixture directory required");
    std::filesystem::path root(argv[1]);
    Storage ownerStore((root/L"owner").wstring()),targetStore((root/L"target").wstring()),memberStore((root/L"member").wstring()),siblingStore((root/L"sibling").wstring()),extraStore((root/L"extra").wstring());
    auto owner=ownerStore.profile(),target=targetStore.profile(),member=memberStore.profile(),sibling=siblingStore.profile(),extra=extraStore.profile();
    auto registration=group_devices::identityRegistration(1,target);
    auto identity=group_devices::readIdentityRegistration(registration);
    if(identity.account!=1||identity.device!=target.peerId||identity.publicKey!=target.publicKey)throw std::runtime_error("Identity proof roundtrip failed");
    write(root/L"identity.bin",registration);
    write(root/L"identity-wrong-account.bin",group_devices::identityRegistration(2,target));
    auto badRegistration=registration;badRegistration.back()^=1;bool invalid=false;
    try{group_devices::readIdentityRegistration(badRegistration);}catch(const std::exception&){invalid=true;}
    if(!invalid)throw std::runtime_error("Damaged identity registration accepted");
    GroupEvent create;create.groupId=newGroupId();create.logicalOrder=1;create.kind=GroupEventKind::Create;
    create.actorAccountId=create.subjectAccountId=1;create.actorPeerId=create.subjectPeerId=owner.peerId;
    create.subjectPublicKey=owner.publicKey;create.subjectName=L"Owner";create.value=L"Device proof fixtures";signGroupEvent(create,owner);
    write(root/L"create.bin",encodeGroupEvent(create));
    auto otherCreation=create;otherCreation.groupId=newGroupId();signGroupEvent(otherCreation,owner);
    write(root/L"other-create.bin",encodeGroupEvent(otherCreation));
    auto text=std::string("PeerChat group roster v1|")+create.groupId+"|"+owner.peerId;
    auto server=hex(digestDevicePayload(Blob(text.begin(),text.end()))).substr(0,32);
    std::map<std::string,Blob> evidence;
    auto proof=[&](const char* name,const Profile& actor,const Profile& destination,std::uint64_t account,std::uint64_t generation,bool revoke=false){
        GroupEvent event;event.groupId=create.groupId;event.logicalOrder=generation+1;event.kind=revoke?GroupEventKind::RevokeDevice:GroupEventKind::AuthorizeDevice;
        event.actorAccountId=event.subjectAccountId=account;event.actorPeerId=actor.peerId;event.subjectPeerId=destination.peerId;
        if(!revoke)event.subjectPublicKey=destination.publicKey;
        event.value=revoke?L"PeerChat/group-device-revocation/v1":L"PeerChat/group-device-authorization/v1";signGroupEvent(event,actor);
        auto wire=group_devices::approvalProof(server,generation,event,destination.publicKey,group_devices::identityProof(account,destination),actor);
        auto parsed=group_devices::readApproval(wire);
        if(parsed.serverGroup!=server||parsed.generation!=generation||parsed.account!=account||encodeGroupEvent(parsed.event)!=encodeGroupEvent(event))
            throw std::runtime_error("Native signed approval roundtrip failed");
        auto damaged=wire;damaged.back()^=1;bool rejected=false;
        try{group_devices::readApproval(damaged);}catch(const std::exception&){rejected=true;}
        if(!rejected)throw std::runtime_error("Native approval parser accepted damaged signature");
        write(root/name,wire);
        evidence.emplace(name,std::move(wire));
    };
    proof("approve.bin",owner,target,1,1);proof("stale.bin",owner,extra,1,1);
    proof("revoke.bin",owner,target,1,2,true);proof("reenroll.bin",owner,target,1,3);
    for(std::uint64_t i=0;i<40;++i)proof(("window-"+std::to_string(i)+".bin").c_str(),owner,target,1,i+1,i%2!=0);
    proof("wrong-account.bin",owner,target,2,1);
    proof("revoke-primary.bin",target,owner,1,2,true);
    for(std::uint64_t i=0;i<8;++i){
        Storage deviceStore((root/ (L"bounded-device-"+std::to_wstring(i))).wstring());
        proof(("bounded-"+std::to_string(i)+".bin").c_str(),owner,deviceStore.profile(),1,i+1);
    }
    proof("member.bin",member,sibling,2,3);proof("member-rejoin.bin",member,sibling,2,7);
    // Re-sign the outer wrapper so rejection must come from the inner boundary,
    // not merely from a broken approver signature.
    GroupEvent valid;valid.groupId=create.groupId;valid.logicalOrder=2;valid.kind=GroupEventKind::AuthorizeDevice;
    valid.actorAccountId=valid.subjectAccountId=1;valid.actorPeerId=owner.peerId;valid.subjectPeerId=target.peerId;
    valid.subjectPublicKey=target.publicKey;valid.value=L"PeerChat/group-device-authorization/v1";signGroupEvent(valid,owner);
    auto targetProof=group_devices::identityProof(1,target);
    auto validWire=group_devices::approvalProof(server,1,valid,target.publicKey,targetProof,owner);
    group_keys::RosterSnapshot previous{create.groupId,server,owner.peerId,1,{{1,owner.peerId,GroupRole::Owner,false}}};
    auto rooted=previous;rooted.creationProof=encodeGroupEvent(create);
    group_keys::GroupKeyManager roots(ownerStore,1,owner.peerId);roots.apply(rooted);
    auto rejectRoot=[&](group_keys::RosterSnapshot candidate){bool rejected=false;try{roots.apply(candidate);}catch(const std::exception&){rejected=true;}
        if(!rejected||roots.roster(server)->creationProof!=rooted.creationProof)throw std::runtime_error("Unverified root changed authoritative roster");};
    auto badRoot=rooted;badRoot.creationProof.back()^=1;rejectRoot(badRoot);
    badRoot=rooted;badRoot.creationProof.clear();rejectRoot(badRoot);
    badRoot=rooted;badRoot.creationProof=encodeGroupEvent(otherCreation);rejectRoot(badRoot);
    badRoot=rooted;badRoot.generation=2;badRoot.creationProof.back()^=1;rejectRoot(badRoot);
    if(group_keys::verifiedCreation(server,create.groupId,rooted.creationProof).eventId!=create.eventId)throw std::runtime_error("Verified root changed creation event");
    auto current=previous;current.generation=2;current.devices.push_back({1,target.peerId,GroupRole::Owner,false});
    GroupLedger signedLedger(true);if(!signedLedger.append(create))throw std::runtime_error("Reconciliation create failed");
    auto checkRejection=[&](const group_keys::RosterSnapshot& snapshot,group_devices::Reconciliation reason){
        auto isolated=signedLedger;auto result=group_devices::reconcileApproval(previous,snapshot,validWire,isolated);
        if(result!=reason||isolated.state().device(1,target.peerId))throw std::runtime_error("Invalid roster mutated enrolled ledger");
    };
    auto wrong=current;wrong.generation=3;checkRejection(wrong,group_devices::Reconciliation::StaleGeneration);
    wrong=current;wrong.devices.back().accountId=2;checkRejection(wrong,group_devices::Reconciliation::RosterMismatch);
    wrong=current;wrong.devices.back().role=GroupRole::Admin;checkRejection(wrong,group_devices::Reconciliation::RosterMismatch);
    wrong=current;wrong.serverGroupId=std::string(32,'f');checkRejection(wrong,group_devices::Reconciliation::WrongGroup);
    GroupLedger closedLedger;closedLedger.append(create);
    if(group_devices::reconcileApproval(previous,current,validWire,closedLedger)!=group_devices::Reconciliation::LedgerRejected)
        throw std::runtime_error("Reconciliation enabled production enrollment implicitly");
    if(group_devices::reconcileApproval(previous,current,validWire,signedLedger)!=group_devices::Reconciliation::Accepted||!signedLedger.state().device(1,target.peerId))
        throw std::runtime_error("Exact double-signed consecutive roster reconciliation failed");
    if(group_devices::reconcileApproval(previous,current,validWire,signedLedger)==group_devices::Reconciliation::Accepted)
        throw std::runtime_error("Replayed enrollment changed current ledger");
    auto duplicate=current;duplicate.devices.back()=duplicate.devices.front();
    if(group_keys::GroupKeyManager::matchesSignedLedger(duplicate,signedLedger.state()))
        throw std::runtime_error("Duplicate roster slot replaced a distinct enrolled device");
    auto retired=previous;retired.generation=3;
    if(group_devices::reconcileApproval(current,retired,evidence.at("revoke.bin"),signedLedger)!=group_devices::Reconciliation::Accepted||signedLedger.state().device(1,target.peerId))
        throw std::runtime_error("Exact device revocation did not reconcile");
    auto restored=current;restored.generation=4;
    {
        const std::vector<group_devices::AuthenticatedTransition> chain={
            {previous,current,validWire},{current,retired,evidence.at("revoke.bin")},
            {retired,restored,evidence.at("reenroll.bin")}};
        auto terminal=restored;terminal.creationProof=rooted.creationProof;
        auto run=[&](const std::wstring& name,const std::vector<group_devices::AuthenticatedTransition>& page,bool enabled,bool expected){
            Storage database((root/name).wstring());database.appendGroupEvent(create);
            group_keys::GroupKeyManager keys(database,1,owner.peerId,true);keys.apply(rooted);
            GroupLedger ledger;ledger.append(create);bool accepted=true;
            try{group_devices::Replica::applyAuthenticated(database,keys,terminal,&ledger,enabled,page);}catch(...){accepted=false;}
            if(accepted!=expected)throw std::runtime_error("Authenticated epoch recovery result mismatch");
            if(expected){
                if(ledger.events().size()!=4||!ledger.state().device(1,target.peerId)||keys.roster(server)->generation!=4)
                    throw std::runtime_error("Missing epochs did not converge to authenticated terminal roster");
                auto recovered=group_devices::Replica::restoreStored(database,create.groupId);
                if(recovered.events().size()!=4||!recovered.state().device(1,target.peerId))throw std::runtime_error("Recovery page did not survive reopen");
            }else if(database.groupEvents(create.groupId).size()!=1||ledger.events().size()!=1||keys.roster(server))
                throw std::runtime_error("Rejected recovery page partially committed or retained authority");
        };
        run(L"recovery-valid",chain,true,true);run(L"recovery-disabled",chain,false,false);
        auto invalidChain=chain;invalidChain.back().proof.back()^=1;run(L"recovery-bad-last",invalidChain,true,false);
        invalidChain=chain;invalidChain.erase(invalidChain.begin()+1);run(L"recovery-missing-epoch",invalidChain,true,false);
        invalidChain=chain;invalidChain[1].before.distributorPeerId=target.peerId;run(L"recovery-discontinuity",invalidChain,true,false);
        invalidChain=chain;invalidChain.back().after.generation=5;run(L"recovery-wrong-terminal",invalidChain,true,false);
        invalidChain=chain;invalidChain[1].after.devices.front().role=GroupRole::Admin;run(L"recovery-role-substitution",invalidChain,true,false);
        invalidChain=chain;invalidChain.resize(33,chain.front());run(L"recovery-page-bound",invalidChain,true,false);
        auto changed=create;changed.value=L"Other signed root";signGroupEvent(changed,owner);
        invalidChain=chain;invalidChain[1].after.creationProof=encodeGroupEvent(changed);run(L"recovery-conflicting-root",invalidChain,true,false);
        std::vector<GroupEvent> metadata={create};for(const auto& step:chain)metadata.push_back(group_devices::readApproval(step.proof).event);
        auto bootstrap=[&](const std::wstring& name,std::vector<GroupEvent> records,std::vector<group_devices::AuthenticatedTransition> proofs,
                           group_keys::RosterSnapshot final,bool enabled,bool expected){
            Storage database((root/name).wstring());group_keys::GroupKeyManager keys(database,1,target.peerId,true);GroupLedger live;bool accepted=true;
            try{group_devices::Replica::bootstrapAuthenticated(database,keys,final,records,proofs,live,enabled);}catch(...){accepted=false;}
            if(accepted!=expected)throw std::runtime_error("Authenticated sibling bootstrap result mismatch");
            if(expected){
                auto reopened=group_devices::Replica::restoreStored(database,create.groupId);
                if(!live.state().device(1,target.peerId)||reopened.events().size()!=4||!reopened.state().device(1,target.peerId)||
                    !live.state().messages.empty()||!database.groupKey(create.groupId,1).empty())throw std::runtime_error("Sibling bootstrap lost membership or imported historical content/key");
            }else if(live.state().created||keys.roster(server)||!database.groupEvents(create.groupId).empty())
                throw std::runtime_error("Failed bootstrap partially persisted membership");
        };
        bootstrap(L"bootstrap-verified",metadata,chain,terminal,true,true);
        bootstrap(L"bootstrap-disabled",metadata,chain,terminal,false,false);
        auto missingProofs=chain;missingProofs.pop_back();bootstrap(L"bootstrap-missing-proof",metadata,missingProofs,terminal,true,false);
        auto wrongTerminal=terminal;wrongTerminal.generation=5;bootstrap(L"bootstrap-epoch-substitution",metadata,chain,wrongTerminal,true,false);
        auto badMetadata=metadata;badMetadata.front()=changed;bootstrap(L"bootstrap-wrong-root",badMetadata,chain,terminal,true,false);
        auto tamperedProofs=chain;tamperedProofs.back().proof.back()^=1;bootstrap(L"bootstrap-tamper",metadata,tamperedProofs,terminal,true,false);
        auto historical=create;historical.kind=GroupEventKind::Message;historical.logicalOrder=5;historical.subjectAccountId=0;
        historical.subjectPeerId.clear();historical.subjectPublicKey.clear();historical.subjectName.clear();historical.value.clear();historical.message=L"Historical content must not enroll";signGroupEvent(historical,owner);
        badMetadata=metadata;badMetadata.push_back(historical);bootstrap(L"bootstrap-no-old-history",badMetadata,chain,terminal,true,false);
        bootstrap(L"bootstrap-revoked-recipient",metadata,chain,retired,true,false);
        // Failure at the LAST persisted row must roll back the earlier rows too.
        Storage database((root/L"recovery-persistence-rollback").wstring());database.appendGroupEvent(create);
        const auto last=group_devices::readApproval(chain.back().proof).event;
        database.commitGroupReplicaReceipt(last,Blob{0});
        group_keys::GroupKeyManager keys(database,1,owner.peerId,true);keys.apply(rooted);GroupLedger ledger;ledger.append(create);bool rejected=false;
        try{group_devices::Replica::applyAuthenticated(database,keys,terminal,&ledger,true,chain);}catch(...){rejected=true;}
        if(!rejected||keys.roster(server)||ledger.events().size()!=1||database.groupEvents(create.groupId).size()!=2||
            !database.groupReplicaReceipt(create.groupId,valid.eventId).empty()||
            !database.groupReplicaReceipt(create.groupId,group_devices::readApproval(chain[1].proof).event.eventId).empty()||
            database.groupReplicaReceipt(create.groupId,last.eventId)!=Blob{0})throw std::runtime_error("Late recovery persistence failure did not roll back the page");
    }
    {
        Storage appStore((root/L"application-replica").wstring());appStore.appendGroupEvent(create);
        GroupLedger appLedger;appLedger.append(create);
        group_keys::GroupKeyManager appKeys(appStore,1,owner.peerId,true);appKeys.apply(rooted);
        auto next=current;next.creationProof=rooted.creationProof;next.deviceApprovals={{2,validWire}};
        auto preflight=appKeys.checked(next);
        if(preflight.generation!=2||appKeys.roster(server)->generation!=1||appStore.groupEvents(create.groupId).size()!=1)
            throw std::runtime_error("Pure roster preflight mutated application state");
        group_devices::Replica::applyAuthenticated(appStore,appKeys,next,&appLedger,true);
        if(appKeys.roster(server)->generation!=2||!appLedger.state().device(1,target.peerId)||appStore.groupReplicaReceipt(create.groupId,valid.eventId).empty())
            throw std::runtime_error("Application roster did not commit an individually verified replica receipt");
        group_devices::Replica::applyAuthenticated(appStore,appKeys,next,&appLedger,true);
        if(appStore.groupEvents(create.groupId).size()!=2)throw std::runtime_error("Repeated application snapshot duplicated a ledger event");
        auto revoked=retired;revoked.creationProof=rooted.creationProof;revoked.deviceApprovals={{3,evidence.at("revoke.bin")}};
        group_devices::Replica::applyAuthenticated(appStore,appKeys,revoked,&appLedger,true);
        if(appLedger.state().device(1,target.peerId)||appKeys.roster(server)->generation!=3)throw std::runtime_error("Application device revoke did not remove authority");
        auto enrolled=restored;enrolled.creationProof=rooted.creationProof;enrolled.deviceApprovals={{4,evidence.at("reenroll.bin")}};
        group_devices::Replica::applyAuthenticated(appStore,appKeys,enrolled,&appLedger,true);
        for(int i=0;i<16;++i){Storage reopened((root/L"application-replica").wstring());
            group_keys::GroupKeyManager loaded(reopened,1,owner.peerId,true);GroupLedger live;
            group_devices::Replica::applyAuthenticated(reopened,loaded,enrolled,&live,true);
            if(!live.state().device(1,target.peerId)||live.events().size()!=4||loaded.roster(server)->generation!=4)
                throw std::runtime_error("Application reopen lost individually verified replica admission");}
        auto rejectApplication=[&](const std::wstring& name,group_keys::RosterSnapshot candidate,bool enabled,bool ledgerPresent){
            Storage isolated((root/name).wstring());isolated.appendGroupEvent(create);GroupLedger unchanged;unchanged.append(create);
            group_keys::GroupKeyManager isolatedKeys(isolated,1,owner.peerId,true);isolatedKeys.apply(rooted);bool failed=false;
            try{group_devices::Replica::applyAuthenticated(isolated,isolatedKeys,candidate,ledgerPresent?&unchanged:nullptr,enabled);}catch(...){failed=true;}
            if(!failed||isolatedKeys.roster(server)||unchanged.state().device(1,target.peerId)||isolated.groupEvents(create.groupId).size()!=1||!isolated.groupReplicaReceipt(create.groupId,valid.eventId).empty())
                throw std::runtime_error("Rejected application admission retained authority or mutated durable ledger");
        };
        rejectApplication(L"app-disabled",next,false,true);rejectApplication(L"app-no-ledger",next,true,false);
        auto missing=next;missing.deviceApprovals.clear();rejectApplication(L"app-missing-proof",missing,true,true);
        auto gap=next;gap.generation=3;rejectApplication(L"app-epoch-gap",gap,true,true);
        auto wrongRole=next;wrongRole.devices.back().role=GroupRole::Admin;rejectApplication(L"app-wrong-role",wrongRole,true,true);
        {
            Storage failedStore((root/L"app-conflicting-receipt").wstring());failedStore.appendGroupEvent(create);failedStore.commitGroupReplicaReceipt(valid,Blob{0});
            group_keys::GroupKeyManager failedKeys(failedStore,1,owner.peerId,true);failedKeys.apply(rooted);GroupLedger unchanged;unchanged.append(create);bool failed=false;
            try{group_devices::Replica::applyAuthenticated(failedStore,failedKeys,next,&unchanged,true);}catch(...){failed=true;}
            if(!failed||failedKeys.roster(server)||unchanged.state().device(1,target.peerId)||failedStore.groupReplicaReceipt(create.groupId,valid.eventId)!=Blob{0}||failedStore.groupEvents(create.groupId).size()!=2)
                throw std::runtime_error("Failed application persistence changed authority or discarded existing evidence");
        }
        {
            // Same signed identity/group namespace is insufficient: exact accepted
            // creation evidence must agree between server and local ledger.
            auto changedCreation=create;changedCreation.value=L"Different signed creation";signGroupEvent(changedCreation,owner);
            Storage conflictingRoot((root/L"app-conflicting-root").wstring());conflictingRoot.appendGroupEvent(changedCreation);
            GroupLedger conflictingLedger;conflictingLedger.append(changedCreation);
            group_keys::GroupKeyManager conflictingKeys(conflictingRoot,1,owner.peerId,true);conflictingKeys.apply(rooted);bool failed=false;
            try{group_devices::Replica::applyAuthenticated(conflictingRoot,conflictingKeys,next,&conflictingLedger,true);}catch(...){failed=true;}
            if(!failed||conflictingKeys.roster(server)||conflictingLedger.state().device(1,target.peerId)||conflictingRoot.groupEvents(create.groupId).size()!=1)
                throw std::runtime_error("Different signed creation root admitted an application transition");
        }
        // A server snapshot alone cannot bootstrap an unverified multi-device ledger.
        Storage naked((root/L"app-naked-bootstrap").wstring());naked.appendGroupEvent(create);
        group_keys::GroupKeyManager nakedKeys(naked,1,owner.peerId,true);GroupLedger unverified;bool rejected=false;
        try{group_devices::Replica::applyAuthenticated(naked,nakedKeys,next,&unverified,true);}catch(...){rejected=true;}
        if(!rejected||nakedKeys.roster(server)||naked.groupEvents(create.groupId).size()!=1)throw std::runtime_error("Current roster substituted for durable enrollment receipts");
    }
    group_keys::verifyDeviceApprovals(server,create.groupId,3,{{2,validWire},{3,evidence.at("revoke.bin")}});
    {
        protocol::Writer w(protocol::GroupEvent);w.string("PeerChat/group-roster-evidence/v1");w.bytes(encodeGroupEvent(create));w.number(2);
        w.number(2);w.bytes(validWire);w.number(3);w.bytes(evidence.at("revoke.bin"));
        auto parsed=group_keys::readRosterEvidence(w.data,server,create.groupId,3);
        if(parsed.approvals.size()!=2||parsed.root!=encodeGroupEvent(create))throw std::runtime_error("Copied roster proof projection changed evidence");
        auto reject=[&](const Blob& bytes){bool failed=false;try{group_keys::readRosterEvidence(bytes,server,create.groupId,3);}catch(...){failed=true;}if(!failed)throw std::runtime_error("Malformed roster evidence admitted");};
        auto altered=w.data;altered.back()^=1;reject(altered);altered=w.data;altered.push_back(0);reject(altered);reject(Blob(135169,0));
    }
    auto rejectEvidence=[&](const std::vector<group_keys::PublicDeviceApproval>& rows,std::uint64_t generation=3){
        bool failed=false;try{group_keys::verifyDeviceApprovals(server,create.groupId,generation,rows);}catch(...){failed=true;}
        if(!failed)throw std::runtime_error("Invalid public device receipt admitted");
    };
    rejectEvidence({{2,validWire}},1);rejectEvidence({{3,validWire}});
    rejectEvidence({{3,evidence.at("revoke.bin")},{2,validWire}});rejectEvidence({{2,validWire},{2,validWire}});
    auto damagedEvidence=validWire;damagedEvidence.back()^=1;rejectEvidence({{2,damagedEvidence}});
    Storage replicaStore((root/L"replica").wstring());replicaStore.appendGroupEvent(create);
    GroupLedger replicaLedger;replicaLedger.append(create);
    group_devices::Replica disabled(replicaStore,server,create.groupId);
    if(disabled.accept(previous,current,validWire,replicaLedger)!=group_devices::Reconciliation::LedgerRejected)
        throw std::runtime_error("Replica activated enrollment by default");
    group_devices::Replica replica(replicaStore,server,create.groupId,true);
    wrong=current;wrong.generation=3;
    if(replica.accept(previous,wrong,validWire,replicaLedger)!=group_devices::Reconciliation::StaleGeneration||!replicaStore.groupReplicaReceipt(create.groupId,valid.eventId).empty())
        throw std::runtime_error("Stale replica admission mutated durable state");
    if(replica.accept(previous,current,validWire,replicaLedger)!=group_devices::Reconciliation::Accepted||!replicaLedger.state().device(1,target.peerId))
        throw std::runtime_error("Proof-specific replica admission failed");
    if(replica.accept(previous,current,validWire,replicaLedger)==group_devices::Reconciliation::Accepted)
        throw std::runtime_error("Replica replay accepted stale transition");
    auto unauthorized=group_devices::readApproval(evidence.at("stale.bin")).event;
    if(replicaLedger.append(unauthorized)||replicaLedger.state().device(1,extra.peerId))
        throw std::runtime_error("One admitted proof globally enabled unapproved device events");
    // Use a fresh reconstructed ledger so the intentionally invalid test event
    // above is not part of the independently verified durable replica.
    replicaLedger=replica.restore();
    if(replica.accept(current,retired,evidence.at("revoke.bin"),replicaLedger)!=group_devices::Reconciliation::Accepted||replicaLedger.state().device(1,target.peerId))
        throw std::runtime_error("Durable replica revocation failed");
    if(replica.accept(retired,restored,evidence.at("reenroll.bin"),replicaLedger)!=group_devices::Reconciliation::Accepted)
        throw std::runtime_error("Explicit replica re-enrollment failed");
    for(int i=0;i<16;++i){Storage reopened((root/L"replica").wstring());group_devices::Replica loaded(reopened,server,create.groupId,true);
        auto ledger=loaded.restore();auto applicationLedger=group_devices::Replica::restoreStored(reopened,create.groupId);
        if(!applicationLedger.state().device(1,target.peerId)||applicationLedger.events().size()!=4)throw std::runtime_error("Application replica restore diverged");
        if(!ledger.state().device(1,target.peerId)||ledger.events().size()!=4)
            throw std::runtime_error("Protected replica reopen lost verified transitions");}
    Storage missingStore((root/L"missing-receipt").wstring());missingStore.appendGroupEvent(create);missingStore.appendGroupEvent(valid);
    bool missingRejected=false;try{group_devices::Replica(missingStore,server,create.groupId,true).restore();}catch(const std::exception&){missingRejected=true;}
    if(!missingRejected)throw std::runtime_error("Naked stored device event bypassed receipt verification");
    Storage conflictStore((root/L"conflict-receipt").wstring());conflictStore.appendGroupEvent(create);conflictStore.commitGroupReplicaReceipt(valid,Blob{0});
    GroupLedger conflictLedger;conflictLedger.append(create);bool conflictRejected=false;
    try{group_devices::Replica(conflictStore,server,create.groupId,true).accept(previous,current,validWire,conflictLedger);}catch(const std::exception&){conflictRejected=true;}
    if(!conflictRejected||conflictLedger.state().device(1,target.peerId))throw std::runtime_error("Failed receipt persistence changed live replica");
    if(group_devices::reconcileApproval(retired,restored,evidence.at("reenroll.bin"),signedLedger)!=group_devices::Reconciliation::Accepted||!signedLedger.state().device(1,target.peerId))
        throw std::runtime_error("Fresh explicit reenrollment did not reconcile");
    auto adversarial=[&](const char* name,const std::string& group,const Blob& identity,const Blob& signedEvent){
        protocol::Writer writer(protocol::GroupEvent);writer.string("PeerChat/group-device-approval/v1");
        writer.string(group);writer.string(create.groupId);writer.number(1);writer.number(1);
        writer.string(owner.peerId);writer.bytes(owner.publicKey);writer.string(target.peerId);writer.bytes(target.publicKey);
        writer.bytes(identity);writer.bytes(signedEvent);writer.bytes(signDevicePayload(writer.data,owner));
        if(group==server){bool rejected=false;try{group_devices::readApproval(writer.data);}catch(const std::exception&){rejected=true;}
            if(!rejected)throw std::runtime_error("Native approval parser trusted invalid nested evidence");}
        write(root/name,writer.data);
    };
    auto badIdentity=targetProof;badIdentity[0]^=1;
    adversarial("bad-target-signature.bin",server,badIdentity,encodeGroupEvent(valid));
    adversarial("target-wrong-account.bin",server,group_devices::identityProof(2,target),encodeGroupEvent(valid));
    auto badEvent=encodeGroupEvent(valid);badEvent.back()^=1;
    adversarial("bad-inner-signature.bin",server,targetProof,badEvent);
    auto substitutedGroup=server;substitutedGroup[0]=server[0]=='0'?'1':'0';
    adversarial("substituted-group.bin",substitutedGroup,targetProof,encodeGroupEvent(valid));
    bool rejected=false;try{group_devices::approvalProof(server,1,valid,target.publicKey,badIdentity,owner);}catch(const std::exception&){rejected=true;}
    if(!rejected)throw std::runtime_error("Native provider accepted invalid target possession proof");
    write(root/L"member-public.bin",member.publicKey);write(root/L"sibling-public.bin",sibling.publicKey);
    std::cout<<"PASS: native signing-only device binding fixtures; existing identities and canonical signed roster events\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
