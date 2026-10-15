#pragma once
#include "group_application.hpp"

namespace peerchat::portable {
// Read-only projection of existing protected authenticated history. The caller
// owns authorization/reconnect scheduling and the existing event transport.
// No outbox, timer, delivery transition, nonce reservation or encryption here.
class GroupPendingProjection {
public:
    struct Item {std::string groupId,messageId;std::uint64_t rosterGeneration=0;Blob wire;};
    struct Batch {std::vector<Item> items;std::size_t scanned=0;bool more=false;};
    static constexpr std::size_t MaxScanned=8,MaxReturned=4,MaxReturnedBytes=239000;
private:
    Storage& store_;const Profile& profile_;
    struct Cursor {std::string legacy;std::uint64_t epoch=0;std::int64_t before=INT64_MAX;bool done=false;};
    std::map<std::string,Cursor> cursors_;
    std::uint64_t account_=0,authentication_=0;
    std::string previous_;
    bool changed_=false;
    struct Secret {Blob bytes;~Secret(){platform::group_crypto::wipe(bytes.data(),bytes.size());}};
    static bool pending(const std::string& status){
        return status=="Android group queued"||status=="Awaiting server acceptance (online recipients only)"||
            status=="Not submitted; server event channel unavailable"||status=="Delivery unknown after disconnect";
    }
public:
    GroupPendingProjection(Storage& store,const Profile& profile):store_(store),profile_(profile){}
    void reset(){cursors_.clear();previous_.clear();account_=authentication_=0;changed_=false;}
    // A new commit/same-epoch snapshot must not restart an unfinished backward
    // scan at the newest row. Finish the bounded pass, then inspect new rows.
    void changed(){
        if(std::any_of(cursors_.begin(),cursors_.end(),[](const auto& entry){return !entry.second.done;}))changed_=true;
        else reset();
    }
    Batch scan(const GroupApplication& groups){
        Batch result;
        if(!groups.generation()){reset();return result;}
        protocol::check(groups.groups().size()<=128);
        // Commit ephemeral progress only with a successful batch. A malformed
        // stored record must not silently consume earlier returned candidates.
        auto cursors=(account_==groups.account()&&authentication_==groups.generation())?cursors_:std::map<std::string,Cursor>{};
        auto previous=previous_;
        for(auto it=cursors.begin();it!=cursors.end();)if(!groups.groups().count(it->first))it=cursors.erase(it);else ++it;
        for(const auto& id:groups.groups()){
            const auto* row=groups.roster(id);protocol::check(row);
            auto found=cursors.find(id);
            if(found==cursors.end()||found->second.legacy!=row->legacyGroupId||found->second.epoch!=row->generation)
                cursors[id]={row->legacyGroupId,row->generation,INT64_MAX,false};
        }
        std::size_t returnedBytes=0;
        while(result.scanned<MaxScanned&&result.items.size()<MaxReturned&&!cursors.empty()){
            auto selected=cursors.upper_bound(previous);if(selected==cursors.end())selected=cursors.begin();
            std::size_t visited=0;
            while(selected->second.done&&visited<cursors.size()){
                ++visited;if(++selected==cursors.end())selected=cursors.begin();
            }
            if(visited==cursors.size())break;
            auto& cursor=selected->second;const auto& id=selected->first;previous=id;
            const auto* roster=groups.roster(id);const auto* ledger=groups.ledger(cursor.legacy);
            if(!roster||!ledger||!group_keys::GroupKeyManager::matchesSignedLedger(*roster,ledger->state())||
                !group_keys::GroupKeyManager::currentMember(*roster,groups.account(),profile_.peerId)){
                cursor.done=true;continue;
            }
            const auto* member=ledger->state().device(groups.account(),profile_.peerId);
            if(!member||member->publicKey!=profile_.publicKey||Storage::publicKeyId(profile_.publicKey)!=profile_.peerId){cursor.done=true;continue;}
            // One row per group turn bounds decrypted data and avoids starving
            // another group behind a large or mostly terminal history.
            auto page=store_.authenticatedGroupHistory(cursor.legacy,1,cursor.before);
            if(page.empty()){cursor.done=true;continue;}
            protocol::check(page.size()==1);auto& record=page.front();Secret stored{std::move(record.plaintext)};
            ++result.scanned;protocol::check(record.sequence>0&&record.sequence<cursor.before);
            group_messages::Envelope envelope;
            protocol::check(group_messages::parse(record.envelope,envelope)==group_messages::Result::Accepted&&envelope.groupId==id);
            const bool eligible=pending(record.status)&&envelope.senderDevice==profile_.peerId&&envelope.senderAccount==groups.account()&&
                envelope.rosterGeneration==roster->generation&&envelope.keyGeneration==roster->generation;
            if(eligible){
                // Reuse native verification primitives without receive(), which
                // would perform durable import/duplicate-receipt transitions.
                protocol::check(verifyDevicePayload(group_messages::signingPayload(envelope),envelope.signature,member->publicKey));
                Secret key{store_.groupKey(cursor.legacy,roster->generation)},plaintext;
                protocol::check(key.bytes.size()==32);auto tag=envelope.tag;
                protocol::check(platform::group_crypto::gcm(false,key.bytes,group_messages::associatedData(envelope),envelope.nonce,
                    envelope.ciphertext,plaintext.bytes,tag)&&plaintext.bytes==stored.bytes&&group_text::valid(plaintext.bytes));
                // Includes room for each local projection's identifiers/epoch/
                // byte-length fields; 1000 bytes remain for response framing.
                const auto cost=record.envelope.size()+104;
                protocol::check(cost<=MaxReturnedBytes);
                if(returnedBytes+cost>MaxReturnedBytes)break; // Preserve this unread cursor for the next call.
                returnedBytes+=cost;
                result.items.push_back({id,envelope.messageId,envelope.rosterGeneration,std::move(record.envelope)});
            }
            cursor.before=record.sequence;
        }
        result.more=std::any_of(cursors.begin(),cursors.end(),[](const auto& entry){return !entry.second.done;});
        if(!result.more&&changed_){reset();result.more=true;return result;}
        cursors_=std::move(cursors);previous_=std::move(previous);account_=groups.account();authentication_=groups.generation();
        return result;
    }
};
}
