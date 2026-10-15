#pragma once
#include "storage.hpp"
#include <map>
#include <set>

namespace peerchat {
namespace group_devices { class Replica; }
enum class GroupRole : unsigned char { Member=1, Admin=2, Owner=3 };
enum class GroupEventKind : unsigned char { Create=1, Rename=2, Invite=3, Accept=4, Decline=5, Leave=6, Remove=7, SetRole=8, Message=9, AuthorizeDevice=10, RevokeDevice=11 };

struct GroupEvent {
    std::string groupId,eventId,actorPeerId,subjectPeerId;
    std::uint64_t logicalOrder=0,createdMilliseconds=0,actorAccountId=0,subjectAccountId=0;
    GroupEventKind kind=GroupEventKind::Create;
    std::wstring subjectName,value,message;
    Blob subjectPublicKey,signature;
    // Local/UI message projection only; never encoded into signed roster events.
    std::int64_t sequence=0;
    std::string deliverySummary;
};

struct GroupMember {
    std::uint64_t accountId=0;
    std::wstring name;
    std::string peerId;
    Blob publicKey;
    GroupRole role=GroupRole::Member;
    // Derived from the admitted signed join/device event, never a new wire field.
    // Rejoining with the same identity must not revive previous history authority.
    std::string membershipId;
    GroupMember()=default;
    GroupMember(std::uint64_t account,std::wstring label,std::string peer,Blob key,GroupRole permission,std::string membership={})
        :accountId(account),name(std::move(label)),peerId(std::move(peer)),publicKey(std::move(key)),role(permission),membershipId(std::move(membership)){}
};

struct GroupState {
    bool created=false;
    std::string groupId;
    std::wstring name;
    std::uint64_t ownerAccountId=0,maxLogicalOrder=0;
    std::map<std::uint64_t,GroupMember> members,invitations;
    // Additional devices require an explicit signed same-account approval.
    // Account roles remain authoritative in members; this is not a history grant.
    std::map<std::string,GroupMember> authorizedDevices;
    std::vector<GroupEvent> messages;
    const GroupMember* device(std::uint64_t account,const std::string& peer)const{
        auto member=members.find(account);
        if(member==members.end())return nullptr;
        if(member->second.peerId==peer)return &member->second;
        auto extra=authorizedDevices.find(peer);
        return extra!=authorizedDevices.end()&&extra->second.accountId==account?&extra->second:nullptr;
    }
};

// The ledger is the authority for a local group replica. It accepts only correctly signed
// events authorized by the deterministically reconstructed prior state. Replaying the same set
// in any arrival order produces the same state because ordering is (logicalOrder,eventId).
class GroupLedger {
public:
    // Production remains fail-closed until authoritative server proof/epoch
    // reconciliation explicitly enables additional-device events.
    explicit GroupLedger(bool deviceEnrollmentEnabled=false):deviceEnrollmentEnabled_(deviceEnrollmentEnabled){}
    bool append(const GroupEvent& event);
    const GroupState& state()const{return state_;}
    const std::vector<GroupEvent>& events()const{return events_;}
    bool contains(const std::string& eventId)const;
    bool accepted(const std::string& eventId)const{return accepted_.count(eventId)!=0;}
private:
    friend class group_devices::Replica;
    std::set<std::string> admittedDeviceEvents_;
    bool deviceEnrollmentEnabled_=false;
    bool rebuild(const std::string& requiredEventId);
    std::vector<GroupEvent> events_;
    GroupState state_;
    std::set<std::string> accepted_;
};

std::string newGroupId();
void signGroupEvent(GroupEvent& event,const Profile& device);
bool verifyGroupEventSignature(const GroupEvent& event,const Blob& publicKey);
// Same persistent P-256 device identity as the signed roster; no new signing key.
Blob signDevicePayload(const Blob& canonicalPayload,const Profile& device);
Blob digestDevicePayload(const Blob& canonicalPayload); // Existing SHA-256 provider.
bool verifyDevicePayload(const Blob& canonicalPayload,const Blob& signature,const Blob& publicKey);
Blob encodeGroupEvent(const GroupEvent& event);
GroupEvent decodeGroupEvent(const Blob& packet,bool enforceChatLimit=false,bool* oversizedMessage=nullptr);
}
