#pragma once
#include "storage.hpp"
#include <map>
#include <set>

namespace peerchat {
enum class GroupRole : unsigned char { Member=1, Admin=2, Owner=3 };
enum class GroupEventKind : unsigned char { Create=1, Rename=2, Invite=3, Accept=4, Decline=5, Leave=6, Remove=7, SetRole=8, Message=9 };

struct GroupEvent {
    std::string groupId,eventId,actorPeerId,subjectPeerId;
    std::uint64_t logicalOrder=0,createdMilliseconds=0,actorAccountId=0,subjectAccountId=0;
    GroupEventKind kind=GroupEventKind::Create;
    std::wstring subjectName,value,message;
    Blob subjectPublicKey,signature;
};

struct GroupMember {
    std::uint64_t accountId=0;
    std::wstring name;
    std::string peerId;
    Blob publicKey;
    GroupRole role=GroupRole::Member;
};

struct GroupState {
    bool created=false;
    std::string groupId;
    std::wstring name;
    std::uint64_t ownerAccountId=0,maxLogicalOrder=0;
    std::map<std::uint64_t,GroupMember> members,invitations;
    std::vector<GroupEvent> messages;
};

// The ledger is the authority for a local group replica. It accepts only correctly signed
// events authorized by the deterministically reconstructed prior state. Replaying the same set
// in any arrival order produces the same state because ordering is (logicalOrder,eventId).
class GroupLedger {
public:
    bool append(const GroupEvent& event);
    const GroupState& state()const{return state_;}
    const std::vector<GroupEvent>& events()const{return events_;}
    bool contains(const std::string& eventId)const;
    bool accepted(const std::string& eventId)const{return accepted_.count(eventId)!=0;}
private:
    bool rebuild(const std::string& requiredEventId);
    std::vector<GroupEvent> events_;
    GroupState state_;
    std::set<std::string> accepted_;
};

std::string newGroupId();
void signGroupEvent(GroupEvent& event,const Profile& device);
bool verifyGroupEventSignature(const GroupEvent& event,const Blob& publicKey);
Blob encodeGroupEvent(const GroupEvent& event);
GroupEvent decodeGroupEvent(const Blob& packet,bool enforceChatLimit=false,bool* oversizedMessage=nullptr);
}
