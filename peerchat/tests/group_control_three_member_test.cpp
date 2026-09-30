#include "../node.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>

using namespace peerchat;
using Clock=std::chrono::steady_clock;
static void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}

int main(int argc,char** argv){
    if(argc!=2){std::cerr<<"FAIL: temporary profile directory required\n";return 1;}
    constexpr size_t Count=3;
    std::array<Node,Count> peers;
    std::array<std::string,Count> ids;
    std::array<std::wstring,Count> paths;
    std::array<std::uint64_t,2> ownerAttempts{},memberAttempts{};
    std::array<bool,2> attached{};
    std::set<std::string> acknowledged;
    std::uint64_t expectedGeneration=3;
    const auto root=std::filesystem::path(argv[1]);
    try{
        for(size_t i=0;i<Count;i++){
            paths[i]=(root/std::to_wstring(i)).wstring();
            peers[i].start(paths[i],0,L"Three-member group",false);
        }
        auto collect=[&]{
            for(size_t i=0;i<Count;i++)for(auto& event:peers[i].poll()){
                if(event.kind==NodeEvent::Kind::Ready){
                    require(event.port==0,"group member opened an ordinary direct listener");
                    ids[i]=event.peerId;
                    peers[i].setNetworkContext(1,L"test server relay");
                    peers[i].setGroupAccount(i+1,L"Member "+std::to_wstring(i+1));
                }else if(event.kind==NodeEvent::Kind::RelayRequested){
                    require(event.authorizationRosterGeneration==expectedGeneration,"group relay used a stale roster generation");
                    if(i==0){
                        size_t target=event.peerId==ids[1]?1:event.peerId==ids[2]?2:0;
                        require(target>0,"owner requested an unknown group-control device");
                        ownerAttempts[target-1]=event.attemptId;
                    }else{
                        require(event.peerId==ids[0],"member attempted a peer mesh instead of owner key control");
                        memberAttempts[i-1]=event.attemptId;
                    }
                }else if(event.kind==NodeEvent::Kind::ConnectionLifecycle&&i==0&&
                         event.text==L"Group key offer acknowledged by member")acknowledged.insert(event.peerId);
                else if(event.kind==NodeEvent::Kind::Error){
                    std::wcerr<<L"peer "<<i<<L": "<<event.text<<L'\n';
                    throw std::runtime_error("three-member authenticated group control failed");
                }
            }
        };
        auto pump=[&]{
            collect();
            unsigned char bytes[65536];
            for(size_t offset=0;offset<2;offset++){
                if(!attached[offset])continue;
                size_t member=offset+1;
                for(;;){int n=peers[0].takeRelayOutput(ownerAttempts[offset],bytes,sizeof(bytes),0);if(n<=0)break;require(peers[member].relayReceive(memberAttempts[offset],bytes,static_cast<size_t>(n)),"member rejected opaque TLS relay data");}
                for(;;){int n=peers[member].takeRelayOutput(memberAttempts[offset],bytes,sizeof(bytes),0);if(n<=0)break;require(peers[0].relayReceive(ownerAttempts[offset],bytes,static_cast<size_t>(n)),"owner rejected opaque TLS relay data");}
            }
        };
        auto wait=[&](auto done,const char* reason){auto until=Clock::now()+std::chrono::seconds(20);while(Clock::now()<until){pump();if(done())return;std::this_thread::sleep_for(std::chrono::milliseconds(3));}throw std::runtime_error(reason);};
        wait([&]{for(const auto& id:ids)if(id.empty())return false;return true;},"three group devices did not initialize");
        std::string legacy=peers[0].createGroup(L"Three-member server-roster group");
        wait([&]{return !peers[0].groupCreationProof(legacy).empty();},"owner did not persist the signed Create event");
        group_keys::RosterSnapshot roster;
        roster.legacyGroupId=legacy;roster.serverGroupId=Storage::newMessageId();
        roster.distributorPeerId=ids[0];roster.generation=3;
        for(size_t i=0;i<Count;i++)roster.devices.push_back({i+1,ids[i],i==0?GroupRole::Owner:GroupRole::Member,false});
        for(size_t i=0;i<Count;i++)require(peers[i].setAuthenticatedGroupRoster(i+1,1,roster),"authenticated group roster was rejected");
        for(size_t offset=0;offset<2;offset++){
            size_t member=offset+1;
            require(peers[0].beginGroupControl(roster.serverGroupId,member+1,ids[member],1),"owner could not request group control");
            require(peers[member].beginGroupControl(roster.serverGroupId,1,ids[0],1),"member could not request group control");
        }
        wait([&]{return ownerAttempts[0]&&ownerAttempts[1]&&memberAttempts[0]&&memberAttempts[1];},"three-member group did not request both owner/member relay pairs");
        for(size_t offset=0;offset<2;offset++){
            require(peers[0].relayReady(ownerAttempts[offset],false)&&
                    peers[offset+1].relayReady(memberAttempts[offset],true),"existing scoped relay adapter rejected a group member");
            attached[offset]=true;
        }
        wait([&]{return acknowledged.size()==2;},"one group member did not receive and ACK the current protected key");
        // An explicit authorized server removal changes the authenticated
        // roster first; the same initiating device then signs the matching
        // local Remove. No absent legacy member is removed automatically.
        attached={};ownerAttempts={};memberAttempts={};acknowledged.clear();
        expectedGeneration=4;roster.generation=4;roster.devices.pop_back();
        require(peers[0].setAuthenticatedGroupRoster(1,1,roster)&&
                peers[1].setAuthenticatedGroupRoster(2,1,roster),"remaining devices rejected the next authoritative roster");
        peers[2].clearAuthenticatedGroupRosters();
        require(peers[0].removeGroupMember(legacy,3),"owner could not queue the explicit signed removal");
        require(peers[0].beginGroupControl(roster.serverGroupId,2,ids[1],1)&&
                peers[1].beginGroupControl(roster.serverGroupId,1,ids[0],1),"remaining devices could not reopen scoped control");
        wait([&]{return ownerAttempts[0]&&memberAttempts[0];},"rotated group did not request a fresh scoped relay");
        require(peers[0].relayReady(ownerAttempts[0],false)&&peers[1].relayReady(memberAttempts[0],true),
                "rotated group relay could not attach");
        attached[0]=true;
        wait([&]{return acknowledged.count(ids[1])>0;},"remaining member did not ACK the rotated group key");
        for(auto& peer:peers)peer.stop();
        Storage ownerStore(paths[0]);Blob expected=ownerStore.groupKey(legacy,3);
        require(expected.size()==32,"owner did not retain the current group key");
        for(size_t i=1;i<Count;i++){
            Storage memberStore(paths[i]);
            require(memberStore.groupKey(legacy,3)==expected,"one member received a different group key");
            require(ownerStore.hasGroupKeyReceipt(legacy,3,ids[i]),"one member ACK was not durable");
            require(memberStore.contacts().empty(),"group-control path created ordinary friend trust");
        }
        require(ownerStore.contacts().empty(),"group-control owner created ordinary friend trust");
        Blob rotated=ownerStore.groupKey(legacy,4);
        require(rotated.size()==32&&rotated!=expected,"membership removal did not rotate the content key");
        require(Storage(paths[1]).groupKey(legacy,4)==rotated,"remaining member did not receive the rotated key");
        require(Storage(paths[2]).groupKey(legacy,4).empty(),"removed member received the future key generation");
        require(!ownerStore.hasGroupKeyReceipt(legacy,4,ids[2]),"removed member received a future ACK receipt");
        std::cout<<"PASS: three non-friend devices ACKed the group key without a mesh; explicit removal rotated the key for remaining members only.\n";
        return 0;
    }catch(const std::exception& error){for(auto& peer:peers)peer.stop();std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
