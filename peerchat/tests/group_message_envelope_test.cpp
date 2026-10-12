#include "../group_message_envelope.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <bcrypt.h> // Independent Windows fixture even with portable production crypto.

using namespace peerchat;
using namespace peerchat::group_messages;
static void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
static Blob body(const std::string& value){return {value.begin(),value.end()};}
static void expect(const Reception& value,Result expected){
    if(value.result!=expected)throw std::runtime_error(std::string("Unexpected validation status: ")+resultName(value.result)+"; expected "+resultName(expected));
    if(expected!=Result::Accepted)require(value.plaintext.empty(),"Rejected envelope leaked plaintext");
}
static void resign(Envelope& envelope,const Profile& profile){envelope.signature=signDevicePayload(signingPayload(envelope),profile);}
// Independent CNG fixture: manufacture a fully authenticated conflicting ID,
// rather than weakening production creation to accept caller-selected IDs/nonces.
static void sealFixture(Envelope& e,const Blob& plaintext,const Blob& keyBytes,const Profile& device,bool preserveNonce=false){
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_KEY_HANDLE key{};
    auto ok=[](NTSTATUS status){require(status>=0,"CNG envelope fixture failed");};
    try{
        ok(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_AES_ALGORITHM,nullptr,0));
        ok(BCryptSetProperty(algorithm,BCRYPT_CHAINING_MODE,reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),sizeof(BCRYPT_CHAIN_MODE_GCM),0));
        ok(BCryptGenerateSymmetricKey(algorithm,&key,nullptr,0,const_cast<PUCHAR>(keyBytes.data()),32,0));
        e.nonce.resize(12);if(!preserveNonce)ok(BCryptGenRandom(nullptr,e.nonce.data(),12,BCRYPT_USE_SYSTEM_PREFERRED_RNG));
        e.ciphertext.resize(plaintext.size());e.tag.resize(16);auto aad=associatedData(e);
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce=e.nonce.data();info.cbNonce=12;info.pbAuthData=aad.data();info.cbAuthData=static_cast<ULONG>(aad.size());info.pbTag=e.tag.data();info.cbTag=16;
        ULONG written=0;ok(BCryptEncrypt(key,const_cast<PUCHAR>(plaintext.data()),static_cast<ULONG>(plaintext.size()),&info,nullptr,0,e.ciphertext.data(),static_cast<ULONG>(e.ciphertext.size()),&written,0));
        require(written==plaintext.size(),"CNG fixture output length mismatch");resign(e,device);
        BCryptDestroyKey(key);BCryptCloseAlgorithmProvider(algorithm,0);
    }catch(...){if(key)BCryptDestroyKey(key);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);throw;}
}
// Reuse the real signed roster/key owner, rather than synthetic trust flags or another transport.
static GroupState ledger(const std::string& local,const Profile& owner,const Profile& member,bool includeMember=true){
    GroupLedger result;GroupEvent event;event.groupId=local;event.logicalOrder=1;event.kind=GroupEventKind::Create;
    event.actorAccountId=event.subjectAccountId=1;event.actorPeerId=event.subjectPeerId=owner.peerId;
    event.subjectPublicKey=owner.publicKey;event.subjectName=L"Owner";event.value=L"Envelope proof";
    signGroupEvent(event,owner);require(result.append(event),"Signed create failed");
    if(includeMember){
        event={};event.groupId=local;event.logicalOrder=2;event.kind=GroupEventKind::Invite;
        event.actorAccountId=1;event.actorPeerId=owner.peerId;event.subjectAccountId=2;event.subjectPeerId=member.peerId;
        event.subjectPublicKey=member.publicKey;event.subjectName=L"Member";signGroupEvent(event,owner);require(result.append(event),"Signed invite failed");
        event={};event.groupId=local;event.logicalOrder=3;event.kind=GroupEventKind::Accept;
        event.actorAccountId=event.subjectAccountId=2;event.actorPeerId=event.subjectPeerId=member.peerId;
        signGroupEvent(event,member);require(result.append(event),"Signed accept failed");
    }
    return result.state();
}
static void deviceEnrollment(const std::filesystem::path& root){
    Storage firstStore((root/L"first").wstring()),secondStore((root/L"second").wstring()),thirdStore((root/L"third").wstring());
    auto first=firstStore.profile(),second=secondStore.profile(),third=thirdStore.profile();
    GroupLedger ledger(true);GroupEvent create;create.groupId=newGroupId();create.logicalOrder=1;create.kind=GroupEventKind::Create;
    create.actorAccountId=create.subjectAccountId=8;create.actorPeerId=create.subjectPeerId=first.peerId;
    create.subjectPublicKey=first.publicKey;create.subjectName=L"Account owner";create.value=L"Device enrollment";
    signGroupEvent(create,first);require(ledger.append(create),"Device fixture create");
    auto approve=[&](const Profile& actor,const Profile& target,std::uint64_t account){
        GroupEvent event;event.groupId=create.groupId;event.logicalOrder=ledger.state().maxLogicalOrder+1;
        event.kind=GroupEventKind::AuthorizeDevice;event.actorAccountId=8;event.actorPeerId=actor.peerId;
        event.subjectAccountId=account;event.subjectPeerId=target.peerId;event.subjectPublicKey=target.publicKey;
        event.value=L"PeerChat/group-device-authorization/v1";signGroupEvent(event,actor);return event;
    };
    require(!ledger.state().device(8,second.peerId),"Same-account device admitted without consent");
    auto rejected=[&](const GroupEvent& event){auto isolated=ledger;return !isolated.append(event);};
    require(rejected(approve(first,second,9)),"Cross-account approval accepted");
    require(rejected(approve(second,third,8)),"Unenrolled device issued approval");
    auto grant=approve(first,second,8);auto tampered=grant;tampered.subjectPeerId=third.peerId;
    require(rejected(tampered),"Substituted device approval accepted");
    auto wrongPurpose=grant;wrongPurpose.value=L"PeerChat/history";signGroupEvent(wrongPurpose,first);
    require(rejected(wrongPurpose),"Wrong-purpose device approval accepted");
    grant=decodeGroupEvent(encodeGroupEvent(grant));require(ledger.append(grant),"Explicit same-account approval rejected");
    GroupLedger productionDefault;require(productionDefault.append(create)&&!productionDefault.append(grant),
        "Production enabled enrollment before server proof/epoch integration");
    require(ledger.state().members.size()==1&&ledger.state().device(8,second.peerId),"Device enrollment changed logical membership");
    group_keys::RosterSnapshot oldRoster{create.groupId,newGroupId(),first.peerId,1,{{8,first.peerId,GroupRole::Owner,false}}};
    require(!group_keys::GroupKeyManager::matchesSignedLedger(oldRoster,ledger.state()),"Incomplete server roster authorized enrolled ledger");
    auto enrolledRoster=oldRoster;enrolledRoster.generation=2;
    enrolledRoster.devices.push_back({8,second.peerId,GroupRole::Owner,false});
    group_keys::GroupKeyManager closedKeys(firstStore,8,first.peerId);
    bool closed=false;try{closedKeys.apply(enrolledRoster);}catch(const std::exception&){closed=true;}
    require(closed,"Production key owner activated enrollment prematurely");
    group_keys::GroupKeyManager firstKeys(firstStore,8,first.peerId,true),secondKeys(secondStore,8,second.peerId,true);
    firstKeys.apply(enrolledRoster);secondKeys.apply(enrolledRoster);
    require(firstKeys.ensureCurrentKey(enrolledRoster.serverGroupId,ledger.state()),"Enrolled device roster did not provision current key");
    auto enrolledOffers=firstStore.pendingGroupKeyHandoffs(second.peerId);
    require(enrolledOffers.size()==1&&secondKeys.accept(enrolledOffers[0],first.peerId,ledger.state()),"Same-account exact-device key handoff rejected");
    require(secondStore.groupKey(create.groupId,1).empty(),"Enrollment implicitly shared historical key");
    MessageLayer firstMessages(firstStore,firstKeys,first,8),secondMessages(secondStore,secondKeys,second,8);
    auto siblingMessage=secondMessages.create(create.groupId,ledger.state(),body("same-account enrolled sender"));
    require(siblingMessage.result==Result::Accepted,"Enrolled sibling could not encrypt future group text");
    auto receivedSibling=firstMessages.receive(create.groupId,ledger.state(),siblingMessage.wire);expect(receivedSibling,Result::Accepted);
    require(receivedSibling.envelope.senderAccount==8&&receivedSibling.envelope.senderDevice==second.peerId,"Sibling attribution lost exact device identity");
    expect(firstMessages.receive(create.groupId,ledger.state(),siblingMessage.wire),Result::DuplicateMessage);
    auto spoofedSibling=siblingMessage.envelope;spoofedSibling.messageId=Storage::newMessageId();resign(spoofedSibling,first);
    expect(firstMessages.receive(create.groupId,ledger.state(),serialize(spoofedSibling)),Result::SignatureFailed);
    auto unapprovedRoster=enrolledRoster;unapprovedRoster.generation=3;
    unapprovedRoster.devices.push_back({8,third.peerId,GroupRole::Owner,false});
    group_keys::GroupKeyManager unapprovedKeys(firstStore,8,first.peerId,true);unapprovedKeys.apply(unapprovedRoster);
    require(!unapprovedKeys.ensureCurrentKey(unapprovedRoster.serverGroupId,ledger.state()),"Account-only server entry obtained a key without signed device grant");
    auto elevated=enrolledRoster;elevated.generation=3;elevated.devices.back().role=GroupRole::Admin;
    bool roleRejected=false;try{firstKeys.apply(elevated);}catch(const std::exception&){roleRejected=true;}
    require(roleRejected,"Same-account sibling role diverged from account authority");
    // A separately authorized device can approve its own account's next device.
    auto next=approve(second,third,8);require(ledger.append(next),"Authorized sibling could not approve own-account device");
    GroupEvent chat;chat.groupId=create.groupId;chat.logicalOrder=ledger.state().maxLogicalOrder+1;
    chat.kind=GroupEventKind::Message;chat.actorAccountId=8;chat.actorPeerId=second.peerId;chat.message=L"Signed sibling message";
    signGroupEvent(chat,second);require(ledger.append(chat),"Approved sibling could not sign group text");
    GroupEvent revoke;revoke.groupId=create.groupId;revoke.logicalOrder=ledger.state().maxLogicalOrder+1;
    revoke.kind=GroupEventKind::RevokeDevice;revoke.actorAccountId=revoke.subjectAccountId=8;
    revoke.actorPeerId=first.peerId;revoke.subjectPeerId=second.peerId;revoke.value=L"PeerChat/group-device-revocation/v1";
    signGroupEvent(revoke,first);require(ledger.append(revoke),"Device-specific revocation failed");
    require(!ledger.state().device(8,second.peerId)&&ledger.state().device(8,third.peerId),"Revocation changed sibling membership");
    auto revokedRoster=oldRoster;revokedRoster.generation=4;revokedRoster.devices.push_back({8,third.peerId,GroupRole::Owner,false});
    firstKeys.apply(revokedRoster);
    require(firstStore.pendingGroupKeyHandoffs(second.peerId).empty(),"Revoked recipient retained pending key handoff");
    require(firstKeys.ensureCurrentKey(revokedRoster.serverGroupId,ledger.state()),"Revocation did not rotate current key for surviving devices");
    require(!firstKeys.maySend(enrolledOffers[0],second.peerId)&&!secondKeys.accept(enrolledOffers[0],first.peerId,ledger.state()),"Revoked device retained current key authority");
    expect(firstMessages.receive(create.groupId,ledger.state(),siblingMessage.wire),Result::StaleRosterGeneration);
    auto stale=approve(second,second,8);require(!ledger.append(stale),"Revoked device issued fresh approval");
    chat.logicalOrder=ledger.state().maxLogicalOrder+1;chat.message=L"Revoked message";signGroupEvent(chat,second);
    require(rejected(chat),"Revoked device signed a new accepted group message");
    // Revoking the original device preserves the account role and an authorized sibling.
    auto primaryRevocation=ledger;revoke.logicalOrder=ledger.state().maxLogicalOrder+1;
    revoke.actorPeerId=third.peerId;revoke.subjectPeerId=first.peerId;signGroupEvent(revoke,third);
    require(primaryRevocation.append(revoke)&&!primaryRevocation.state().device(8,first.peerId)&&
        primaryRevocation.state().device(8,third.peerId)&&primaryRevocation.state().members.at(8).role==GroupRole::Owner,
        "Original-device revocation lost account ownership");
    revoke.logicalOrder=primaryRevocation.state().maxLogicalOrder+1;revoke.subjectPeerId=third.peerId;signGroupEvent(revoke,third);
    require(!primaryRevocation.append(revoke),"Last authorized device was silently revoked");
    // Ordinary members, not just Owners/Admins, may approve their own account.
    GroupLedger ordinary(true);require(ordinary.append(create),"Ordinary-member fixture create");
    GroupEvent invite;invite.groupId=create.groupId;invite.logicalOrder=2;invite.kind=GroupEventKind::Invite;
    invite.actorAccountId=8;invite.actorPeerId=first.peerId;invite.subjectAccountId=9;
    invite.subjectPeerId=third.peerId;invite.subjectPublicKey=third.publicKey;invite.subjectName=L"Member";
    signGroupEvent(invite,first);require(ordinary.append(invite),"Ordinary-member invite");
    GroupEvent accept;accept.groupId=create.groupId;accept.logicalOrder=3;accept.kind=GroupEventKind::Accept;
    accept.actorAccountId=accept.subjectAccountId=9;accept.actorPeerId=accept.subjectPeerId=third.peerId;
    signGroupEvent(accept,third);require(ordinary.append(accept),"Ordinary-member accept");
    auto memberGrant=grant;memberGrant.logicalOrder=4;memberGrant.actorAccountId=memberGrant.subjectAccountId=9;
    memberGrant.actorPeerId=third.peerId;signGroupEvent(memberGrant,third);
    require(ordinary.append(memberGrant)&&ordinary.state().device(9,second.peerId)->role==GroupRole::Member,
        "Ordinary member approval required admin or elevated sibling role");
    // Reverse delivery and encrypted storage reopen preserve the signed device state.
    GroupLedger reverse(true);for(auto it=ledger.events().rbegin();it!=ledger.events().rend();++it)reverse.append(*it);
    require(reverse.state().device(8,third.peerId)&&!reverse.state().device(8,second.peerId),"Device ledger did not converge");
    const auto path=(root/L"reopen").wstring();{Storage store(path);for(const auto& event:ledger.events())store.appendGroupEvent(event);}
    GroupLedger reopened(true);{Storage store(path);for(const auto& event:store.groupEvents(create.groupId))reopened.append(event);}
    require(reopened.state().device(8,third.peerId)&&!reopened.state().device(8,second.peerId),"Signed device approval did not persist");
    require(firstStore.contacts().empty()&&firstStore.groupKey(create.groupId,1).empty(),"Enrollment silently granted trust or history keys");
    std::cout<<"PASS: explicit same-account signed device approval; substitution/domain rejection; device revocation; primary replacement; last-device protection; convergence and encrypted reopen\n";
}
static void memberFanout(const std::filesystem::path& root,unsigned count){
    require(count==3||count==4,"Fanout fixture bound");
    std::vector<std::unique_ptr<Storage>> stores;std::vector<Profile> devices;
    for(unsigned i=0;i<count;++i){stores.push_back(std::make_unique<Storage>((root/std::to_wstring(i)).wstring()));devices.push_back(stores.back()->profile());}
    GroupLedger signedLedger;auto local=newGroupId(),server=newGroupId();GroupEvent event;
    event.groupId=local;event.logicalOrder=1;event.kind=GroupEventKind::Create;event.actorAccountId=event.subjectAccountId=1;event.actorPeerId=event.subjectPeerId=devices[0].peerId;event.subjectPublicKey=devices[0].publicKey;event.subjectName=L"Owner";event.value=L"Bounded fanout";signGroupEvent(event,devices[0]);require(signedLedger.append(event),"Fanout create");
    for(unsigned i=1;i<count;++i){event={};event.groupId=local;event.logicalOrder=2*i;event.kind=GroupEventKind::Invite;event.actorAccountId=1;event.actorPeerId=devices[0].peerId;event.subjectAccountId=i+1;event.subjectPeerId=devices[i].peerId;event.subjectPublicKey=devices[i].publicKey;event.subjectName=L"Member";signGroupEvent(event,devices[0]);require(signedLedger.append(event),"Fanout invite");event={};event.groupId=local;event.logicalOrder=2*i+1;event.kind=GroupEventKind::Accept;event.actorAccountId=event.subjectAccountId=i+1;event.actorPeerId=event.subjectPeerId=devices[i].peerId;signGroupEvent(event,devices[i]);require(signedLedger.append(event),"Fanout accept");}
    group_keys::RosterSnapshot roster;roster.legacyGroupId=local;roster.serverGroupId=server;roster.distributorPeerId=devices[0].peerId;roster.generation=2*count-1;
    for(unsigned i=0;i<count;++i)roster.devices.push_back({i+1,devices[i].peerId,i?GroupRole::Member:GroupRole::Owner,false});
    std::vector<std::unique_ptr<group_keys::GroupKeyManager>> owners;std::vector<std::unique_ptr<MessageLayer>> layers;
    for(unsigned i=0;i<count;++i){owners.push_back(std::make_unique<group_keys::GroupKeyManager>(*stores[i],i+1,devices[i].peerId));owners.back()->apply(roster);}
    require(owners[0]->ensureCurrentKey(server,signedLedger.state()),"Fanout key generation");
    for(unsigned i=1;i<count;++i){auto offers=stores[0]->pendingGroupKeyHandoffs(devices[i].peerId);require(offers.size()==1&&owners[i]->accept(offers[0],devices[0].peerId,signedLedger.state()),"Fanout exact-device key handoff");}
    for(unsigned i=0;i<count;++i)layers.push_back(std::make_unique<MessageLayer>(*stores[i],*owners[i],devices[i],i+1));
    for(unsigned sender=0;sender<count;++sender){auto plain=body("bounded-fanout-"+std::to_string(sender));auto sent=layers[sender]->create(local,signedLedger.state(),plain);require(sent.result==Result::Accepted,"Fanout sender");for(unsigned reader=0;reader<count;++reader)if(reader!=sender){auto received=layers[reader]->receive(local,signedLedger.state(),sent.wire);expect(received,Result::Accepted);require(received.plaintext==plain&&received.envelope.senderAccount==sender+1&&received.envelope.senderDevice==devices[sender].peerId,"Fanout provenance/content");expect(layers[reader]->receive(local,signedLedger.state(),sent.wire),Result::DuplicateMessage);}}
    for(unsigned i=0;i<count;++i)require(stores[i]->authenticatedGroupHistory(local,100).size()==count,"Fanout protected history count/dedupe");
    std::cout<<"PASS: "<<count<<" signed members, per-device key handoff, every sender/reader, original provenance and protected history dedupe\n";
}
int main(int argc,char** argv){
    try{
        require(argc==2,"Unique temporary profile directory required");auto root=std::filesystem::path(argv[1]);
        deviceEnrollment(root/L"device-enrollment");
        memberFanout(root/L"three-members",3);memberFanout(root/L"four-members",4);
        Storage ownerStore((root/L"owner").wstring()),memberStore((root/L"member").wstring()),outsiderStore((root/L"outsider").wstring());
        auto owner=ownerStore.profile(),member=memberStore.profile(),outsider=outsiderStore.profile();
        auto local=newGroupId(),server=newGroupId();auto state=ledger(local,owner,member);
        group_keys::RosterSnapshot roster{local,server,owner.peerId,2,{{1,owner.peerId,GroupRole::Owner,false},{2,member.peerId,GroupRole::Member,false}}};
        group_keys::GroupKeyManager ownerKeys(ownerStore,1,owner.peerId),memberKeys(memberStore,2,member.peerId);
        ownerKeys.apply(roster);memberKeys.apply(roster);require(ownerKeys.ensureCurrentKey(server,state),"Owner key creation failed");
        auto offers=ownerStore.pendingGroupKeyHandoffs(member.peerId);
        require(offers.size()==1&&memberKeys.accept(offers[0],owner.peerId,state),"Actual authorized key handoff failed");
        MessageLayer sender(ownerStore,ownerKeys,owner,1),receiver(memberStore,memberKeys,member,2);
        auto invalidBody=sender.create(local,state,body("invalid text framing"));
        require(invalidBody.result==Result::Accepted,"Invalid-body fixture encryption failed");
        expect(receiver.receive(local,state,invalidBody.wire,[](const Blob&){return false;}),Result::InvalidMessageBody);
        require(memberStore.authenticatedGroupMessage(server,invalidBody.envelope.messageId).envelope.empty(),"Invalid authenticated body reached history/dedupe");
        std::set<Blob> nonces;
        for(auto size:{std::size_t(0),std::size_t(1),std::size_t(128),MaxPlaintextBytes}){
            Blob plain(size);for(std::size_t i=0;i<size;++i)plain[i]=static_cast<unsigned char>(i%251);
            auto sent=sender.create(local,state,plain);require(sent.result==Result::Accepted,"Envelope creation failed");
            if(size){auto cng=sent.envelope;sealFixture(cng,plain,ownerStore.groupKey(local,2),owner,true);require(cng.ciphertext==sent.envelope.ciphertext&&cng.tag==sent.envelope.tag,"Independent CNG/production encryption mismatch");}
            require(nonces.insert(sent.envelope.nonce).second,"Repeated nonce");
            Envelope parsed;require(parse(sent.wire,parsed)==Result::Accepted&&serialize(parsed)==sent.wire,"Canonical parse/serialize roundtrip failed");
            auto received=receiver.receive(local,state,sent.wire);expect(received,Result::Accepted);require(received.plaintext==plain,"Roundtrip mismatch");
            expect(receiver.receive(local,state,sent.wire),Result::DuplicateMessage);
            require(memberStore.authenticatedGroupMessage(server,sent.envelope.messageId).plaintext==plain,"Authenticated local history missing");
        }
        auto original=sender.create(local,state,body("private-message-marker-DO-NOT-OUTPUT"));require(original.result==Result::Accepted,"Normal creation failed");
        require(ownerStore.setGroupMessageStatus(server,original.envelope.messageId,"Recipient accepted",2),"Recipient receipt persistence failed");
        require(!ownerStore.setGroupMessageStatus(server,original.envelope.messageId,"Server accepted",1)&&!ownerStore.setGroupMessageStatus(server,original.envelope.messageId,"Unknown after disconnect",0),"Late admission/disconnect overwrote a recipient receipt");
        require(ownerStore.authenticatedGroupMessage(server,original.envelope.messageId).status=="Recipient accepted","Protected receipt did not survive readback");
        auto marker=body("private-message-marker-DO-NOT-OUTPUT");
        require(std::search(original.wire.begin(),original.wire.end(),marker.begin(),marker.end())==original.wire.end(),"Server-facing representation contains plaintext");
        auto test=[&](Envelope e,Result status){expect(receiver.receive(local,state,serialize(e)),status);};
        auto e=original.envelope;e.ciphertext[0]^=1;test(e,Result::SignatureFailed);resign(e,owner);test(e,Result::AeadAuthFailed);
        e=original.envelope;e.tag[0]^=1;resign(e,owner);test(e,Result::AeadAuthFailed);
        e=original.envelope;e.signature[0]^=1;test(e,Result::SignatureFailed);
        e=original.envelope;e.groupId=newGroupId();test(e,Result::WrongGroup);
        e=original.envelope;e.rosterGeneration=1;test(e,Result::StaleRosterGeneration);
        e=original.envelope;e.rosterGeneration=3;test(e,Result::FutureRosterGeneration);
        e=original.envelope;e.keyGeneration=1;test(e,Result::StaleKeyGeneration);
        e=original.envelope;e.keyGeneration=3;test(e,Result::FutureKeyGeneration);
        e=original.envelope;e.senderAccount=999;test(e,Result::SenderNotMember);
        e=original.envelope;e.senderDevice=outsider.peerId;resign(e,outsider);test(e,Result::UnauthorizedSenderDevice);
        e=original.envelope;e.senderAccount=2;e.senderDevice=member.peerId;resign(e,owner);test(e,Result::SignatureFailed);
        // Shared key possession must not let a member impersonate the owner's signing identity.
        e=original.envelope;resign(e,member);test(e,Result::SignatureFailed);
        e=original.envelope;e.messageId=newGroupId();resign(e,owner);test(e,Result::AeadAuthFailed);
        e=original.envelope;e.nonce[0]^=1;resign(e,owner);test(e,Result::AeadAuthFailed);
        // Every AAD byte is authenticated. For fields rejected by context first,
        // metadata tests above prove fail-closed; message ID/nonce re-sign tests prove GCM AAD binding.
        for(std::size_t i=0;i<associatedData(original.envelope).size();++i){
            auto damaged=original.wire;damaged[i]^=1;auto rejected=receiver.receive(local,state,damaged);
            require(rejected.result!=Result::Accepted&&rejected.plaintext.empty(),"Metadata tamper accepted");
        }
        require(memberStore.authenticatedGroupMessage(server,original.envelope.messageId).envelope.empty(),"Failed auth inserted local history");
        expect(receiver.receive(local,state,original.wire),Result::Accepted);
        expect(receiver.receive(local,state,original.wire),Result::DuplicateMessage);
        // An authorized sender cannot replace an already committed ID with another ciphertext.
        auto conflict=sender.create(local,state,body("different"));require(conflict.result==Result::Accepted,"Conflict fixture creation failed");
        e=conflict.envelope;e.messageId=original.envelope.messageId;resign(e,owner);test(e,Result::AeadAuthFailed);
        auto fixtureKey=ownerStore.groupKey(local,2);
        sealFixture(e,body("authenticated conflicting body"),fixtureKey,owner);test(e,Result::ConflictingMessage);
        SecureZeroMemory(fixtureKey.data(),fixtureKey.size());
        auto history=memberStore.authenticatedGroupMessage(server,original.envelope.messageId);
        bool conflicted=false;try{memberStore.appendAuthenticatedGroupMessage(server,original.envelope.messageId,conflict.wire,body("different"));}catch(...){conflicted=true;}
        require(conflicted&&memberStore.authenticatedGroupMessage(server,original.envelope.messageId).envelope==history.envelope,"Conflict overwrote committed history");
        for(std::size_t size=0;size<original.wire.size();++size){auto cut=original.wire;cut.resize(size);expect(receiver.receive(local,state,cut),Result::MalformedEnvelope);}
        auto bad=original.wire;bad.push_back(0);expect(receiver.receive(local,state,bad),Result::MalformedEnvelope);
        bad=original.wire;bad[5]=2;expect(receiver.receive(local,state,bad),Result::UnsupportedVersion);
        bad=original.wire;for(std::size_t i=HeaderBytes-4;i<HeaderBytes;++i)bad[i]=255;expect(receiver.receive(local,state,bad),Result::MalformedEnvelope);
        bad.resize(MaxEnvelopeBytes+1);expect(receiver.receive(local,state,bad),Result::MalformedEnvelope);
        // Deterministic parser fuzzing (not the crypto random source): fail safely,
        // and any structurally valid parse must serialize to the same canonical bytes.
        std::uint32_t fuzz=0x5a31c07u;
        for(unsigned trial=0;trial<1000;++trial){
            Blob candidate(trial%400);for(auto& byte:candidate){fuzz=fuzz*1664525u+1013904223u;byte=static_cast<unsigned char>(fuzz>>24);}
            Envelope parsed;auto status=parse(candidate,parsed);
            if(status==Result::Accepted)require(serialize(parsed)==candidate,"Ambiguous parse accepted");
            auto rejected=receiver.receive(local,state,candidate);require(rejected.result!=Result::Accepted&&rejected.plaintext.empty(),"Random malformed input accepted");
        }
        for(auto length:{0u,11u,13u}){e=original.envelope;e.nonce.resize(length);bool failed=false;try{serialize(e);}catch(...){failed=true;}require(failed,"Invalid nonce length accepted");}
        e=original.envelope;e.tag.resize(15);bool invalidTag=false;try{serialize(e);}catch(...){invalidTag=true;}require(invalidTag,"Invalid tag length accepted");
        e=original.envelope;e.signature.resize(63);bool invalidSignature=false;try{serialize(e);}catch(...){invalidSignature=true;}require(invalidSignature,"Invalid signature length accepted");
        require(sender.create(local,state,Blob(MaxPlaintextBytes+1)).result==Result::MalformedEnvelope,"Oversized body accepted");
        for(int i=0;i<128;++i){auto value=sender.create(local,state,body("nonce proof"));require(value.result==Result::Accepted&&nonces.insert(value.envelope.nonce).second,"Nonce repeated");}
        auto key=ownerStore.groupKey(local,2);auto fingerprint=Storage::publicKeyId(key);
        require(!ownerStore.reserveGroupNonce(fingerprint,original.envelope.nonce),"Reserved nonce could be reused");
        {
            Storage reopened((root/L"owner").wstring());
            require(!reopened.reserveGroupNonce(fingerprint,original.envelope.nonce),"Nonce reservation lost on reopen");
        }
        {
            Storage reopened((root/L"member").wstring());auto sameDevice=reopened.profile();
            group_keys::GroupKeyManager sameKeys(reopened,2,sameDevice.peerId);sameKeys.apply(roster);
            MessageLayer sameReceiver(reopened,sameKeys,sameDevice,2);expect(sameReceiver.receive(local,state,original.wire),Result::DuplicateMessage);
        }
        // Archive authorization for an account must not imply group membership
        // for a different device belonging to that account.
        MessageLayer otherDeviceForMember(memberStore,memberKeys,owner,2);
        expect(otherDeviceForMember.receive(local,state,original.wire),Result::LocalNotMember);
        auto multiDeviceRoster=roster;auto extraDevice=multiDeviceRoster.devices.back();
        extraDevice.peerId=std::string(64,'f');multiDeviceRoster.devices.push_back(extraDevice);
        bool duplicateAccountRejected=false;try{ownerKeys.apply(multiDeviceRoster);}catch(...){duplicateAccountRejected=true;}
        require(duplicateAccountRejected,"Current one-device group roster accepted another device for the same account");
        // Current roster but missing/wrong key, with a matching persistent identity.
        Storage missing((root/L"missing").wstring());missing.saveProfile(member);
        group_keys::GroupKeyManager missingKeys(missing,2,member.peerId);missingKeys.apply(roster);MessageLayer missingReceiver(missing,missingKeys,member,2);
        expect(missingReceiver.receive(local,state,original.wire),Result::MissingGroupKey);
        require(missingReceiver.create(local,state,body("missing key")).result==Result::MissingGroupKey,"Sender used fallback key");
        Blob wrong(32,77);missing.saveGroupKey(local,2,wrong);expect(missingReceiver.receive(local,state,original.wire),Result::AeadAuthFailed);
        auto divergent=state;divergent.members.erase(1);expect(receiver.receive(local,divergent,original.wire),Result::RosterUnavailable);
        MessageLayer wrongLocalAccount(ownerStore,ownerKeys,owner,999);
        require(wrongLocalAccount.create(local,state,body("wrong local account")).result==Result::LocalNotMember,"Unbound local account created envelope");
        require(sender.create(newGroupId(),state,body("wrong local group")).result==Result::WrongGroup,"Wrong local group created envelope");
        // Real Stage 1A rotation/removal: old keys remain history-only, never accepted as current.
        auto oldMemberMessage=receiver.create(local,state,body("old member"));require(oldMemberMessage.result==Result::Accepted,"Member creation failed");
        roster.generation=3;roster.devices.pop_back();auto remaining=ledger(local,owner,member,false);
        ownerKeys.apply(roster);memberKeys.revoke(server);require(ownerKeys.ensureCurrentKey(server,remaining),"Rotated key missing");
        expect(sender.receive(local,remaining,oldMemberMessage.wire),Result::StaleRosterGeneration);
        require(receiver.create(local,state,body("removed member")).result==Result::RosterUnavailable,"Removed member could create current message");
        expect(receiver.receive(local,state,original.wire),Result::RosterUnavailable);
        auto rotated=sender.create(local,remaining,body("new generation"));require(rotated.result==Result::Accepted&&rotated.envelope.keyGeneration==3,"Sender used stale generation after rotation");
        Storage current((root/L"current").wstring());current.saveProfile(owner);current.saveGroupKey(local,3,ownerStore.groupKey(local,3));
        group_keys::GroupKeyManager currentKeys(current,1,owner.peerId);currentKeys.apply(roster);MessageLayer currentReceiver(current,currentKeys,owner,1);
        expect(currentReceiver.receive(local,remaining,rotated.wire),Result::Accepted);
        require(ownerStore.groupKey(local,2)==key&&ownerStore.groupKey(local,3)!=key,"Rotation/history key behavior changed");
        require(memberStore.authenticatedGroupMessage(server,original.envelope.messageId).plaintext==marker,"Old authorized history lost after rotation");
        auto forged=oldMemberMessage.envelope;forged.rosterGeneration=forged.keyGeneration=3;resign(forged,member);
        expect(sender.receive(local,remaining,serialize(forged)),Result::SenderNotMember);
        // No network, friend-trust mutation, diagnostics plaintext, or plaintext SQLite payloads.
        require(ownerStore.contacts().empty()&&memberStore.contacts().empty(),"Envelope promoted friend trust");
        for(auto& entry:std::filesystem::recursive_directory_iterator(root))if(entry.is_regular_file()){
            std::ifstream input(entry.path(),std::ios::binary);std::string data((std::istreambuf_iterator<char>(input)),{});
            require(data.find("private-message-marker-DO-NOT-OUTPUT")==std::string::npos,"Plaintext leaked into local storage files");
        }
        SecureZeroMemory(key.data(),key.size());for(auto& offer:offers)SecureZeroMemory(offer.contentKey.data(),offer.contentKey.size());
        std::cout<<"PASS: Stage 1B AES-256-GCM envelope; device signatures; bounded parsing/tampering; current roster/device/key validation; durable replay/nonce protection; rotation and protected history.\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
