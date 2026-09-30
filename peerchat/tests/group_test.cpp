#include "../group.hpp"
#include "../delivery_manager.hpp"
#include "../protocol.hpp"
#include "../group_key_handoff.hpp"
#include "../group_key_manager.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace peerchat;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static GroupEvent base(const GroupLedger& ledger,const Profile& actor,std::uint64_t account,GroupEventKind kind){GroupEvent event;event.groupId=ledger.state().groupId;event.logicalOrder=ledger.state().maxLogicalOrder+1;event.createdMilliseconds=event.logicalOrder*1000;event.kind=kind;event.actorAccountId=account;event.actorPeerId=actor.peerId;return event;}
static GroupEvent create(const Profile& owner,std::uint64_t account,const std::wstring& user,const std::wstring& name){GroupEvent event;event.groupId=newGroupId();event.logicalOrder=1;event.createdMilliseconds=1;event.kind=GroupEventKind::Create;event.actorAccountId=event.subjectAccountId=account;event.actorPeerId=event.subjectPeerId=owner.peerId;event.subjectName=user;event.subjectPublicKey=owner.publicKey;event.value=name;signGroupEvent(event,owner);return event;}
static GroupEvent invite(const GroupLedger& ledger,const Profile& actor,std::uint64_t actorAccount,const Profile& target,std::uint64_t targetAccount,const std::wstring& name){auto event=base(ledger,actor,actorAccount,GroupEventKind::Invite);event.subjectAccountId=targetAccount;event.subjectPeerId=target.peerId;event.subjectName=name;event.subjectPublicKey=target.publicKey;signGroupEvent(event,actor);return event;}
static GroupEvent self(const GroupLedger& ledger,const Profile& actor,std::uint64_t account,GroupEventKind kind){auto event=base(ledger,actor,account,kind);event.subjectAccountId=account;event.subjectPeerId=actor.peerId;signGroupEvent(event,actor);return event;}

int main(int argc,char** argv){
    try{
        require(argc==2,"group test needs a temporary directory");std::filesystem::path root(argv[1]);std::filesystem::create_directories(root);
        std::vector<Profile> devices;for(int i=0;i<8;i++){Storage store((root/std::to_wstring(i)).wstring());devices.push_back(store.profile());}
        GroupLedger owner;auto genesis=create(devices[0],1,L"Owner",L"Weekend plans");require(owner.append(genesis),"create rejected");require(owner.state().groupId.size()==32,"Group ID is not independent random ID");
        std::vector<GroupEvent> history{genesis};
        // Eight members demonstrates collection-based behavior beyond the three-client minimum.
        for(std::uint64_t account=2;account<=8;account++){auto add=invite(owner,devices[0],1,devices[account-1],account,L"Member "+std::to_wstring(account));require(owner.append(add),"owner invite rejected");history.push_back(add);auto accept=self(owner,devices[account-1],account,GroupEventKind::Accept);require(owner.append(accept),"invitation acceptance rejected");history.push_back(accept);}
        require(owner.state().members.size()==8&&owner.state().invitations.empty(),"membership collection was capped or incomplete");
        auto promote=base(owner,devices[0],1,GroupEventKind::SetRole);promote.subjectAccountId=2;promote.value=L"admin";signGroupEvent(promote,devices[0]);require(owner.append(promote),"owner promotion rejected");history.push_back(promote);require(owner.state().members.at(2).role==GroupRole::Admin,"admin role not applied");
        auto unauthorized=base(owner,devices[2],3,GroupEventKind::Rename);unauthorized.value=L"Hijacked";signGroupEvent(unauthorized,devices[2]);require(!owner.append(unauthorized)&&owner.state().name==L"Weekend plans","member renamed group without authorization");
        auto ownerRename=base(owner,devices[0],1,GroupEventKind::Rename);ownerRename.value=L"Autumn plans";signGroupEvent(ownerRename,devices[0]);auto adminRename=base(owner,devices[1],2,GroupEventKind::Rename);adminRename.value=L"Road trip";signGroupEvent(adminRename,devices[1]);require(owner.append(ownerRename),"owner rename rejected");require(owner.append(adminRename),"admin concurrent rename rejected");history.push_back(ownerRename);history.push_back(adminRename);
        auto firstMessage=base(owner,devices[1],2,GroupEventKind::Message);firstMessage.message=L"Message from admin";signGroupEvent(firstMessage,devices[1]);auto secondMessage=base(owner,devices[2],3,GroupEventKind::Message);secondMessage.message=L"Message from member";signGroupEvent(secondMessage,devices[2]);require(owner.append(secondMessage)&&owner.append(firstMessage),"simultaneous group messages rejected");history.push_back(secondMessage);history.push_back(firstMessage);require(owner.state().messages.size()==2,"group messages missing");require(owner.state().messages[0].eventId<owner.state().messages[1].eventId,"simultaneous messages lack deterministic tie break");
        Blob encoded=encodeGroupEvent(firstMessage);auto decoded=decodeGroupEvent(encoded);require(decoded.eventId==firstMessage.eventId&&verifyGroupEventSignature(decoded,devices[1].publicKey),"group wire event did not retain signature");decoded.message=L"tampered";require(!verifyGroupEventSignature(decoded,devices[1].publicKey),"tampered group event signature accepted");
        auto largeMessage=firstMessage;largeMessage.message=std::wstring(protocol::MaxAcceptedChatCharacters,L'X');
        signGroupEvent(largeMessage,devices[1]);bool oversized=false;
        auto limited=decodeGroupEvent(encodeGroupEvent(largeMessage),true,&oversized);
        require(!oversized&&limited.message==largeMessage.message&&verifyGroupEventSignature(limited,devices[1].publicKey),
            "Historical signed group message was not accepted for ledger convergence");
        // A replica receiving events in reverse arrival order converges once predecessors arrive.
        GroupLedger replica;for(auto it=history.rbegin();it!=history.rend();++it)replica.append(*it);require(replica.state().members.size()==owner.state().members.size()&&replica.state().name==owner.state().name,"reverse delivery did not converge");
        require(replica.append(firstMessage),"identical replay changed acceptance");require(replica.state().messages.size()==2,"duplicate group delivery was not suppressed");
        auto remove=base(owner,devices[1],2,GroupEventKind::Remove);remove.subjectAccountId=3;signGroupEvent(remove,devices[1]);require(owner.append(remove)&&owner.state().members.count(3)==0,"admin could not remove member");
        auto leave=self(owner,devices[3],4,GroupEventKind::Leave);require(owner.append(leave)&&owner.state().members.count(4)==0,"member leave failed");auto ownerLeave=self(owner,devices[0],1,GroupEventKind::Leave);require(!owner.append(ownerLeave)&&owner.state().members.count(1)==1,"owner left without ownership transfer");
        auto persistentPath=(root/L"persistent").wstring();{Storage storage(persistentPath);for(const auto& event:owner.events())storage.appendGroupEvent(event);require(storage.groupIds()==std::vector<std::string>{genesis.groupId},"stored Group ID index mismatch");delivery::DeliveryManager deliveries(storage,devices[0].peerId);storage.queueGroupDelivery(firstMessage.eventId,devices[1].peerId);storage.queueGroupDelivery(firstMessage.eventId,devices[2].peerId);require(deliveries.authorizeTransport({401},devices[1].peerId)&&deliveries.authorizeTransport({402},devices[2].peerId),"Group delivery transports were not destination-bound");require(deliveries.submittedGroupEvent(devices[1].peerId,firstMessage.eventId,{401}),"Group event submission was not recorded");require(!deliveries.acknowledgeGroupEvent(devices[2].peerId,{402},firstMessage.eventId),"Another group destination acknowledged an unsent delivery");require(deliveries.beginDraining({401})&&deliveries.acknowledgeGroupEvent(devices[1].peerId,{401},firstMessage.eventId),"Authorized draining group ACK was rejected");require(storage.pendingGroupEvents(devices[1].peerId).empty()&&!storage.pendingGroupEvents(devices[2].peerId).empty(),"Group ACK changed another destination's durable outbox");}
        GroupLedger restarted;{Storage storage(persistentPath);for(const auto& event:storage.groupEvents(genesis.groupId))restarted.append(event);}require(restarted.state().name==owner.state().name&&restarted.state().members.size()==owner.state().members.size()&&restarted.state().messages.size()==2,"encrypted group ledger did not survive restart");
        // The Stage 1A handoff outbox must survive a disconnect, remain bound to
        // the exact authenticated recipient, and clear only after that peer ACKs.
        GroupKeyHandoff handoff;handoff.id=Storage::newMessageId();handoff.groupId=genesis.groupId;handoff.serverGroupId=Storage::newMessageId();handoff.distributorPeerId=devices[0].peerId;handoff.recipientPeerId=devices[1].peerId;handoff.rosterGeneration=2;handoff.keyGeneration=2;handoff.contentKey=Blob(32,0x5a);
        auto keyPacket=group_keys::offerPacket(handoff);auto decodedKey=group_keys::readOffer(keyPacket);
        require(decodedKey.contentKey==handoff.contentKey&&decodedKey.recipientPeerId==handoff.recipientPeerId,"group key offer wire roundtrip failed");
        keyPacket.pop_back();bool truncatedRejected=false;try{group_keys::readOffer(keyPacket);}catch(...){truncatedRejected=true;}require(truncatedRejected,"truncated group key offer was accepted");
        group_keys::Ack handoffAck{handoff.id,handoff.serverGroupId,2,2};auto ack=group_keys::readAck(group_keys::ackPacket(handoffAck));require(ack.handoffId==handoff.id&&ack.rosterGeneration==2,"group key ACK wire roundtrip failed");
        {Storage storage(persistentPath);storage.saveGroupKey(handoff.groupId,2,handoff.contentKey);storage.queueGroupKeyHandoff(handoff);require(storage.pendingGroupKeyHandoffs(handoff.recipientPeerId).size()==1,"group key handoff was not queued");}
        {Storage storage(persistentPath);require(storage.groupKey(handoff.groupId,2)==handoff.contentKey,"protected group key did not survive restart");delivery::DeliveryManager delivery(storage,devices[0].peerId);auto pending=delivery.pendingGroupKeyHandoffs(handoff.recipientPeerId);require(pending.size()==1&&pending[0].contentKey==handoff.contentKey,"protected handoff did not survive restart");require(delivery.authorizeTransport({801},handoff.recipientPeerId)&&delivery.submittedGroupKeyHandoff(handoff.recipientPeerId,handoff.id,{801}),"group key handoff was not bound to authenticated transport");require(!delivery.acknowledgeGroupKeyHandoff(devices[2].peerId,{801},handoff.id),"wrong device acknowledged group key handoff");delivery.retireTransport({801});require(delivery.authorizeTransport({802},handoff.recipientPeerId)&&delivery.submittedGroupKeyHandoff(handoff.recipientPeerId,handoff.id,{802}),"disconnected group key handoff was not retried");require(delivery.acknowledgeGroupKeyHandoff(handoff.recipientPeerId,{802},handoff.id),"recipient group key ACK failed");require(storage.pendingGroupKeyHandoffs(handoff.recipientPeerId).empty()&&storage.hasGroupKeyReceipt(handoff.groupId,2,handoff.recipientPeerId),"ACK did not persist recipient receipt");}
        {Storage sender((root/L"key-manager-sender").wstring()),receiver((root/L"key-manager-receiver").wstring());
            group_keys::RosterSnapshot snapshot;snapshot.legacyGroupId=genesis.groupId;snapshot.serverGroupId=Storage::newMessageId();snapshot.distributorPeerId=devices[0].peerId;snapshot.generation=3;
            for(const auto& member:owner.state().members)snapshot.devices.push_back({member.first,member.second.peerId,member.second.role,false});
            group_keys::GroupKeyManager senderKeys(sender,1,devices[0].peerId),receiverKeys(receiver,2,devices[1].peerId);
            auto partial=snapshot;partial.devices.pop_back();
            require(!group_keys::GroupKeyManager::matchesSignedLedger(partial,owner.state()),
                    "partial server roster authorized a larger signed group");
            senderKeys.apply(snapshot);receiverKeys.apply(snapshot);require(senderKeys.ensureCurrentKey(snapshot.serverGroupId,owner.state()),"authorized distributor did not create current generation");
            auto offers=sender.pendingGroupKeyHandoffs(devices[1].peerId);require(offers.size()==1&&senderKeys.maySend(offers[0],devices[1].peerId),"current authorized recipient did not get a pending key handoff");
            require(!receiverKeys.accept(offers[0],devices[2].peerId,owner.state()),"wrong TLS peer delivered a group key");
            require(receiverKeys.accept(offers[0],devices[0].peerId,owner.state())&&receiver.groupKey(genesis.groupId,3)==offers[0].contentKey,"authenticated key handoff was not accepted");
            auto stale=offers[0];stale.keyGeneration=2;require(!receiverKeys.accept(stale,devices[0].peerId,owner.state()),"wrong key generation was accepted");
            auto removed=snapshot;removed.generation=4;
            removed.devices.erase(std::remove_if(removed.devices.begin(),removed.devices.end(),[&](const auto& member){return member.peerId==devices[1].peerId;}),removed.devices.end());
            senderKeys.apply(removed);
            require(!senderKeys.maySend(offers[0],devices[1].peerId)&&
                    sender.pendingGroupKeyHandoffs(devices[1].peerId).empty(),
                    "removed member retained a pending group-key delivery");
            GroupLedger removalLedger=owner;
            auto signedRemoval=base(removalLedger,devices[0],1,GroupEventKind::Remove);
            signedRemoval.subjectAccountId=2;signGroupEvent(signedRemoval,devices[0]);
            require(removalLedger.append(signedRemoval),"owner could not sign the explicit membership removal");
            require(senderKeys.ensureCurrentKey(removed.serverGroupId,removalLedger.state()),
                    "current signed removal did not authorize a rotated group key");
            Blob rotatedKey=sender.groupKey(genesis.groupId,4);
            require(rotatedKey.size()==32&&rotatedKey!=offers[0].contentKey&&
                    sender.pendingGroupKeyHandoffs(devices[1].peerId).empty(),
                    "removed member received the rotated key generation");
        }
        std::cout<<"PASS: signed client-owned encrypted group ledger, arbitrary membership, destination-scoped group delivery, draining ACK, convergence, replay rejection, and restart.\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<"\n";return 1;}
}
