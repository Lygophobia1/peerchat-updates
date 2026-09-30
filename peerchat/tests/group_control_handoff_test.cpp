#include "../node.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace peerchat;
using Clock=std::chrono::steady_clock;
static void require(bool condition,const char* reason){if(!condition)throw std::runtime_error(reason);}

int main(int argc,char** argv){
    if(argc!=2){std::cerr<<"FAIL: temporary profile directory required\n";return 1;}
    Node owner,member;std::array<Node*,2> nodes{&owner,&member};
    std::array<std::string,2> identities;
    std::array<std::uint64_t,2> attempts{};
    std::array<bool,2> tlsReady{},keyAcknowledged{};
    std::array<std::vector<std::wstring>,2> stages;
    std::array<std::wstring,2> paths;
    const auto root=std::filesystem::path(argv[1]);
    paths[0]=(root/L"owner").wstring();paths[1]=(root/L"member").wstring();
    try{
        owner.start(paths[0],0,L"Group owner",false);
        member.start(paths[1],0,L"Group member",false);
        auto collect=[&]{
            for(size_t i=0;i<2;i++)for(auto& event:nodes[i]->poll()){
                if(event.kind==NodeEvent::Kind::Ready){
                    require(event.port==0,"relay-only group test opened a direct listener");
                    identities[i]=event.peerId;
                    nodes[i]->setNetworkContext(1,L"test relay");
                    nodes[i]->setGroupAccount(i+1,i?L"Member":L"Owner");
                }else if(event.kind==NodeEvent::Kind::RelayRequested){
                    require(!event.authorizationGroupId.empty()&&event.authorizationRosterGeneration==2,
                            "group relay request lost its scoped roster authorization");
                    attempts[i]=event.attemptId;
                }else if(event.kind==NodeEvent::Kind::SessionSnapshot&&event.sessionConnected&&
                         !event.authorizationGroupId.empty())tlsReady[i]=true;
                else if(event.kind==NodeEvent::Kind::ConnectionLifecycle&&
                        event.text==L"Group key offer acknowledged by member")keyAcknowledged[i]=true;
                else if(event.kind==NodeEvent::Kind::ConnectionLifecycle){
                    if(stages[i].size()>=24)stages[i].erase(stages[i].begin());
                    stages[i].push_back(event.text);
                }
                else if(event.kind==NodeEvent::Kind::Error){
                    std::wcerr<<L"peer "<<i<<L": "<<event.text<<L'\n';
                    for(const auto& stage:stages[i])std::wcerr<<L"  stage: "<<stage<<L'\n';
                    throw std::runtime_error("native group-control peer rejected the handoff");
                }
            }
        };
        auto pump=[&]{
            collect();
            if(attempts[0]&&attempts[1]){
                unsigned char bytes[65536];
                for(size_t from=0;from<2;from++)for(;;){
                    int count=nodes[from]->takeRelayOutput(attempts[from],bytes,sizeof(bytes),0);
                    if(count<=0)break;
                    require(nodes[1-from]->relayReceive(attempts[1-from],bytes,static_cast<size_t>(count)),
                            "opaque relay rejected authenticated peer-TLS bytes");
                }
            }
        };
        auto wait=[&](auto ready,const char* reason){
            auto until=Clock::now()+std::chrono::seconds(15);
            while(Clock::now()<until){pump();if(ready())return;std::this_thread::sleep_for(std::chrono::milliseconds(3));}
            throw std::runtime_error(reason);
        };
        wait([&]{return !identities[0].empty()&&!identities[1].empty();},"group devices did not initialize");
        const auto legacyId=owner.createGroup(L"Current server-roster group");
        require(!legacyId.empty(),"owner could not create signed group identity");
        wait([&]{return !owner.groupCreationProof(legacyId).empty();},"signed creation event was not persisted");
        group_keys::RosterSnapshot roster;
        roster.legacyGroupId=legacyId;roster.serverGroupId=Storage::newMessageId();
        roster.distributorPeerId=identities[0];roster.generation=2;
        roster.devices={{1,identities[0],GroupRole::Owner,false},{2,identities[1],GroupRole::Member,false}};
        require(owner.setAuthenticatedGroupRoster(1,1,roster)&&member.setAuthenticatedGroupRoster(2,1,roster),
                "authenticated roster could not be forwarded to the native owner");
        require(owner.beginGroupControl(roster.serverGroupId,2,identities[1],1)&&
                member.beginGroupControl(roster.serverGroupId,1,identities[0],1),
                "scoped group-control relay request was rejected");
        wait([&]{return attempts[0]&&attempts[1];},"both scoped relay requests did not appear");
        require(owner.relayReady(attempts[0],false)&&member.relayReady(attempts[1],true),
                "existing relay adapter did not accept scoped attempts");
        wait([&]{return tlsReady[0]&&tlsReady[1]&&keyAcknowledged[0];},
             "signed group-control ledger and key ACK did not converge");
        owner.stop();member.stop();
        Storage ownerStore(paths[0]),memberStore(paths[1]);
        Blob ownerKey=ownerStore.groupKey(legacyId,2),memberKey=memberStore.groupKey(legacyId,2);
        require(ownerKey.size()==32&&memberKey==ownerKey,"recipient did not persist the authenticated current group key");
        require(memberStore.groupKey(legacyId,1).empty(),"new member received a historical key generation");
        require(ownerStore.hasGroupKeyReceipt(legacyId,2,identities[1]),"recipient ACK did not persist");
        require(ownerStore.contacts().empty()&&memberStore.contacts().empty(),
                "group-control peer TLS silently established friend trust");
        std::cout<<"PASS: non-friend group-control peer TLS synchronized signed membership and ACKed a protected key through the existing relay.\n";
        return 0;
    }catch(const std::exception& error){owner.stop();member.stop();std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
