#pragma once
#include "../group_device_replica.hpp"
#include "../group_message_envelope.hpp"
#include "../group_text.hpp"
#include "../delivery_manager.hpp"
#include "../archive_group_history.hpp"

namespace peerchat::portable {
// Android application adapter over the SAME native group owners used by Node.
// Thread confined by the existing profile gate. No sockets, timers, crypto,
// membership database or delivery engine is introduced here.
class GroupApplication {
    Storage& store_;const Profile& profile_;delivery::DeliveryManager& deliveries_;
    std::uint64_t account_,generation_=0,revision_=1;
    group_keys::GroupKeyManager keys_;
    std::map<std::string,GroupLedger> ledgers_;
    std::set<std::string> current_;
    std::map<std::string,std::uint64_t> historyDue_;
    bool historyEnabled_=false;
    static std::uint64_t historyClock(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
    GroupEvent event(GroupLedger& ledger,GroupEventKind kind,std::uint64_t now)const{
        GroupEvent e;e.groupId=ledger.state().groupId;e.kind=kind;e.logicalOrder=ledger.state().maxLogicalOrder+1;
        e.createdMilliseconds=now;e.actorAccountId=account_;e.actorPeerId=profile_.peerId;return e;
    }
    void persist(GroupLedger& ledger){for(const auto& e:ledger.events())if(ledger.accepted(e.eventId))store_.appendGroupEvent(e);++revision_;}
    void admit(GroupLedger& ledger,const GroupEvent& e){
        if(ledger.contains(e.eventId)){
            auto old=std::find_if(ledger.events().begin(),ledger.events().end(),[&](const auto& row){return row.eventId==e.eventId;});
            protocol::check(old!=ledger.events().end()&&ledger.accepted(e.eventId)&&encodeGroupEvent(*old)==encodeGroupEvent(e));return;
        }
        protocol::check(ledger.contains(e.eventId)||ledger.events().size()<4096);
        auto candidate=ledger;protocol::check(candidate.append(e));persist(candidate);ledger=std::move(candidate);
    }
    void append(GroupLedger& ledger,GroupEvent e){signGroupEvent(e,profile_);admit(ledger,e);}
public:
    GroupApplication(Storage& store,const Profile& profile,delivery::DeliveryManager& deliveries,std::uint64_t account,bool historyEnabled=false)
        :store_(store),profile_(profile),deliveries_(deliveries),account_(account),keys_(store,account,profile.peerId),historyEnabled_(historyEnabled){
        protocol::check(account>0);
        auto bound=store.groupAccount();protocol::check(!bound.accountId||bound.accountId==account);
        for(const auto& id:store.groupIds())ledgers_.emplace(id,group_devices::Replica::restoreStored(store,id));
    }
    std::uint64_t account()const{return account_;}
    std::uint64_t generation()const{return generation_;}
    std::optional<archive::GroupHistoryContext> historyContext(const std::string& server)const{
        auto row=keys_.roster(server);if(!generation_||!row)return {};
        auto found=ledgers_.find(row->legacyGroupId);if(found==ledgers_.end()||!group_keys::GroupKeyManager::matchesSignedLedger(*row,found->second.state()))return {};
        return archive::GroupHistoryContext{*row,found->second.state()};
    }
    void invalidate(){for(const auto& id:current_)keys_.revoke(id);current_.clear();historyDue_.clear();generation_=0;++revision_;}
    // Authenticated server inputs only. A rejected complete snapshot revokes all
    // live authority; protected history stays untouched for later verification.
    void authenticated(std::uint64_t generation,std::vector<group_keys::RosterSnapshot> rows){
        protocol::check(generation&&generation>=generation_&&rows.size()<=128);
        std::set<std::string> seen;
        try{
            for(const auto& row:rows){protocol::check(seen.insert(row.serverGroupId).second);keys_.checked(row);}
            for(auto& row:rows){
                const auto id=row.serverGroupId,legacy=row.legacyGroupId;
                auto found=ledgers_.find(legacy);
                group_devices::Replica::applyAuthenticated(store_,keys_,std::move(row),found==ledgers_.end()?nullptr:&found->second);
                const auto* installed=keys_.roster(id);
                if(installed&&found!=ledgers_.end()&&installed->distributorPeerId==profile_.peerId)
                    keys_.ensureCurrentKey(id,found->second.state());
            }
            for(const auto& old:current_)if(!seen.count(old))keys_.revoke(old);
            current_=std::move(seen);generation_=generation;++revision_;
        }catch(...){for(const auto& id:seen)keys_.revoke(id);invalidate();throw;}
    }
    const group_keys::RosterSnapshot* roster(const std::string& server)const{return generation_?keys_.roster(server):nullptr;}
    const GroupLedger* ledger(const std::string& legacy)const{auto i=ledgers_.find(legacy);return i==ledgers_.end()?nullptr:&i->second;}
    const std::set<std::string>& groups()const{return current_;}
    Blob create(const std::wstring& name,const std::wstring& username,std::uint64_t now){
        protocol::check(generation_&&now&&name.size()<=80&&!name.empty()&&!username.empty()&&username.size()<=64);
        auto bound=store_.groupAccount();protocol::check(!bound.accountId||bound.accountId==account_);
        if(!bound.accountId)store_.saveGroupAccount({account_,username});
        GroupEvent e;e.groupId=newGroupId();e.kind=GroupEventKind::Create;e.logicalOrder=1;e.createdMilliseconds=now;
        e.actorAccountId=e.subjectAccountId=account_;e.actorPeerId=e.subjectPeerId=profile_.peerId;
        e.subjectPublicKey=profile_.publicKey;e.subjectName=username;e.value=name;
        auto& ledger=ledgers_[e.groupId];append(ledger,e);return encodeGroupEvent(ledger.events().front());
    }
    bool current(const session::ReliableAuthorization& scope,const Blob* key=nullptr)const{
        if(scope.scope!=session::ReliableAuthorizationScope::GroupControl||!scope.valid())return false;
        auto r=roster(scope.groupId);if(!r||r->generation!=scope.rosterGeneration||scope.expectedRemoteDeviceId==profile_.peerId||
            !group_keys::GroupKeyManager::currentMember(*r,account_,profile_.peerId))return false;
        auto remote=std::find_if(r->devices.begin(),r->devices.end(),[&](const auto& d){return !d.invited&&d.peerId==scope.expectedRemoteDeviceId;});
        if(remote==r->devices.end())return false;
        if(key){
            if(Storage::publicKeyId(*key)!=remote->peerId)return false;
            for(const auto& contact:store_.contacts())if(contact.peerId==remote->peerId&&contact.verified&&contact.publicKey!=*key)return false;
        }
        return true;
    }
    bool needsControl(const session::ReliableAuthorization& scope){
        if(!current(scope))return false;
        auto r=roster(scope.groupId);auto ledger=ledgers_.find(r->legacyGroupId);
        if(ledger==ledgers_.end()||!group_keys::GroupKeyManager::matchesSignedLedger(*r,ledger->second.state()))return true;
        auto key=store_.groupKey(r->legacyGroupId,r->generation);bool present=key.size()==32;platform::group_crypto::wipe(key.data(),key.size());
        if(!present)return true;
        if(r->distributorPeerId==profile_.peerId&&!store_.hasGroupKeyReceipt(r->legacyGroupId,r->generation,scope.expectedRemoteDeviceId))return true;
        if(!historyEnabled_)return false;
        const auto now=historyClock();
        auto due=historyDue_.find(scope.continuityKey());return due==historyDue_.end()||now>=due->second;
    }
    class Control {
        GroupApplication& owner_;session::ReliableAuthorization scope_;session::TransportInstanceId transport_;
        Blob remoteKey_;std::set<std::string> sent_;
        std::uint64_t observed_=0;
        bool exchanged_=false;
        bool historyUnavailable_=false;
        std::unique_ptr<archive::GroupHistoryJob> history_;
    public:
        Control(GroupApplication& owner,session::ReliableAuthorization scope,session::TransportInstanceId transport,Blob key)
            :owner_(owner),scope_(std::move(scope)),transport_(transport),remoteKey_(std::move(key)){protocol::check(owner_.current(scope_,&remoteKey_));}
        bool complete()const noexcept{return history_?history_->settled():exchanged_||historyUnavailable_;}
        std::vector<Blob> historyOutgoing(std::uint64_t now,std::uint64_t capabilities){
            if(!owner_.historyEnabled_||!(capabilities&protocol::CapabilityArchiveGroups)||!archive::historyParticipationEnabled(owner_.store_,owner_.account_,owner_.profile_.peerId)){historyUnavailable_=true;owner_.historyDue_[scope_.continuityKey()]=historyClock()+60000;return {};}
            if(!owner_.historyContext(scope_.groupId))return {};
            if(!history_)history_=std::make_unique<archive::GroupHistoryJob>(owner_.store_,owner_.profile_,owner_.account_,scope_,
                [this](const std::string& server){return owner_.historyContext(server);},[this]{return owner_.current(scope_,&remoteKey_)&&archive::historyParticipationEnabled(owner_.store_,owner_.account_,owner_.profile_.peerId);});
            auto bytes=history_->request(now);if(history_->settled())owner_.historyDue_[scope_.continuityKey()]=historyClock()+60000;
            if(bytes.empty())return {};
            return {std::move(bytes)};
        }
        std::vector<Blob> outgoing(std::uint64_t now){
            protocol::check(owner_.current(scope_,&remoteKey_));std::vector<Blob> frames;
            if(observed_==owner_.revision_)return frames;
            auto r=owner_.roster(scope_.groupId);auto found=owner_.ledgers_.find(r->legacyGroupId);
            if(found==owner_.ledgers_.end()){observed_=owner_.revision_;return frames;}
            auto& ledger=found->second;const auto remote=scope_.expectedRemoteDeviceId;
            if(r->distributorPeerId==owner_.profile_.peerId)for(const auto& d:r->devices){
                if(d.invited||d.peerId!=remote||d.accountId==owner_.account_)continue;
                if(!ledger.state().members.count(d.accountId)&&!ledger.state().invitations.count(d.accountId)){
                    auto e=owner_.event(ledger,GroupEventKind::Invite,now);e.subjectAccountId=d.accountId;e.subjectPeerId=d.peerId;
                    e.subjectName=std::to_wstring(d.accountId);e.subjectPublicKey=remoteKey_;owner_.append(ledger,std::move(e));
                }
            }
            for(const auto& e:ledger.events()){
                if(frames.size()>=16)break;
                if(!ledger.accepted(e.eventId)||e.kind==GroupEventKind::Message||!e.message.empty()||sent_.count(e.eventId)||
                    (r->distributorPeerId!=owner_.profile_.peerId&&e.actorPeerId!=owner_.profile_.peerId))continue;
                protocol::Writer w(protocol::GroupRosterEvent);w.bytes(encodeGroupEvent(e));frames.push_back(std::move(w.data));sent_.insert(e.eventId);
            }
            if(r->distributorPeerId==owner_.profile_.peerId&&owner_.keys_.ensureCurrentKey(r->serverGroupId,ledger.state()))
                for(const auto& offer:owner_.deliveries_.pendingGroupKeyHandoffs(remote,r->legacyGroupId,r->generation,64)){
                    if(frames.size()>=16||owner_.deliveries_.inFlightCount(transport_)>=16)break;
                    if(offer.serverGroupId!=r->serverGroupId||!owner_.keys_.maySend(offer,remote)||owner_.deliveries_.wasSubmittedGroupKeyHandoff(remote,offer.id,transport_))continue;
                    frames.push_back(group_keys::offerPacket(offer));
                    protocol::check(owner_.deliveries_.submittedGroupKeyHandoff(remote,offer.id,transport_));
                }
            if(frames.size()<16&&owner_.deliveries_.inFlightCount(transport_)<16)observed_=owner_.revision_;
            return frames;
        }
        std::vector<Blob> receive(const Blob& bytes,std::uint64_t now){
            protocol::check(owner_.current(scope_,&remoteKey_));auto r=owner_.roster(scope_.groupId);
            protocol::Reader reader(bytes);const auto remote=scope_.expectedRemoteDeviceId;
            if(reader.type()==protocol::ArchiveControl){
                if(!history_){auto initial=historyOutgoing(now,protocol::CapabilityArchiveGroups);protocol::check(bool(history_));auto response=history_->control(bytes,now);if(!response.empty())initial.push_back(std::move(response));return initial;}
                protocol::check(bool(history_));auto response=history_->control(bytes,now);
                if(history_->settled())owner_.historyDue_[scope_.continuityKey()]=historyClock()+60000;
                if(response.empty())return {};
                return {std::move(response)};
            }else if(reader.type()==protocol::GroupRosterEvent){
                auto e=decodeGroupEvent(reader.bytes());reader.end();
                protocol::check(e.groupId==r->legacyGroupId&&e.kind!=GroupEventKind::Message&&e.message.empty()&&
                    (remote==r->distributorPeerId||e.actorPeerId==remote));
                auto found=owner_.ledgers_.find(e.groupId);
                if(found==owner_.ledgers_.end()){
                    protocol::check(e.kind==GroupEventKind::Create&&remote==r->distributorPeerId);
                    if(!r->creationProof.empty())protocol::check(encodeGroupEvent(e)==r->creationProof);
                    else protocol::check(std::any_of(r->devices.begin(),r->devices.end(),[&](const auto& d){return !d.invited&&d.role==GroupRole::Owner&&d.accountId==e.actorAccountId&&d.peerId==e.actorPeerId;}));
                    found=owner_.ledgers_.emplace(e.groupId,GroupLedger{}).first;
                }
                auto& ledger=found->second;owner_.admit(ledger,e);
                if(e.kind==GroupEventKind::Invite&&e.subjectAccountId==owner_.account_&&e.subjectPeerId==owner_.profile_.peerId&&
                    ledger.state().invitations.count(owner_.account_)){
                    protocol::check(e.subjectPublicKey==owner_.profile_.publicKey);
                    auto accept=owner_.event(ledger,GroupEventKind::Accept,now);accept.subjectAccountId=owner_.account_;accept.subjectPeerId=owner_.profile_.peerId;
                    owner_.append(ledger,std::move(accept));
                }
            }else if(reader.type()==protocol::GroupKeyOffer){
                auto offer=group_keys::readOffer(bytes);auto found=owner_.ledgers_.find(r->legacyGroupId);
                protocol::check(found!=owner_.ledgers_.end()&&owner_.keys_.accept(offer,remote,found->second.state()));
                ++owner_.revision_;
                exchanged_=true;
                return {group_keys::ackPacket({offer.id,offer.serverGroupId,offer.rosterGeneration,offer.keyGeneration})};
            }else if(reader.type()==protocol::GroupKeyAck){
                auto ack=group_keys::readAck(bytes);protocol::check(ack.serverGroupId==r->serverGroupId&&ack.rosterGeneration==r->generation&&ack.keyGeneration==r->generation);
                if(owner_.deliveries_.acknowledgeGroupKeyHandoff(remote,transport_,ack.handoffId))exchanged_=true;
                ++owner_.revision_;
            }else protocol::check(false);
            return {};
        }
    };
    group_messages::Creation send(const std::string& server,const std::wstring& text,std::uint64_t now){
        auto r=roster(server);protocol::check(r);auto found=ledgers_.find(r->legacyGroupId);protocol::check(found!=ledgers_.end());
        auto plain=group_text::encode({now,store_.nextLogicalOrder(),text});
        group_messages::MessageLayer layer(store_,keys_,profile_,account_);auto result=layer.create(r->legacyGroupId,found->second.state(),plain);
        platform::group_crypto::wipe(plain.data(),plain.size());
        if(result.result==group_messages::Result::Accepted)store_.setGroupMessageStatus(server,result.envelope.messageId,"Android group queued",0);
        return result;
    }
    Blob retry(const std::string& server,const std::string& id){
        auto r=roster(server);protocol::check(r&&protocol::id(id,32));
        auto record=store_.authenticatedGroupMessage(server,id);group_messages::Envelope e;
        platform::group_crypto::wipe(record.plaintext.data(),record.plaintext.size());
        protocol::check(group_messages::parse(record.envelope,e)==group_messages::Result::Accepted&&e.senderDevice==profile_.peerId&&
            e.senderAccount==account_&&e.groupId==server&&e.rosterGeneration==r->generation&&e.keyGeneration==r->generation&&
            group_keys::GroupKeyManager::currentMember(*r,account_,profile_.peerId));
        return record.envelope;
    }
    group_messages::Result receive(const Blob& wire){
        group_messages::Envelope e;auto parsed=group_messages::parse(wire,e);if(parsed!=group_messages::Result::Accepted)return parsed;
        auto r=roster(e.groupId);if(!r)return group_messages::Result::RosterUnavailable;
        auto found=ledgers_.find(r->legacyGroupId);if(found==ledgers_.end())return group_messages::Result::RosterUnavailable;
        group_messages::MessageLayer layer(store_,keys_,profile_,account_);auto result=layer.receive(r->legacyGroupId,found->second.state(),wire,group_text::valid);
        if(result.result==group_messages::Result::Accepted)store_.observeLogicalOrder(group_text::decode(result.plaintext).order);
        platform::group_crypto::wipe(result.plaintext.data(),result.plaintext.size());return result.result;
    }
};
}
