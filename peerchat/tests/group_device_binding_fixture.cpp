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
    group_keys::verifyDeviceApprovals(server,create.groupId,3,{{2,validWire},{3,evidence.at("revoke.bin")}});
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
