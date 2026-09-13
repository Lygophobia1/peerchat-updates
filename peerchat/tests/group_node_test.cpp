#include "node.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

void require(bool condition,const char* explanation){if(!condition)throw std::runtime_error(explanation);}

int main(){
    using namespace peerchat;using Clock=std::chrono::steady_clock;constexpr size_t Count=4;
    std::array<Node,Count> peers;std::array<std::string,Count> identities;std::array<bool,Count> ready{};
    std::array<std::set<std::string>,Count> connected,messages;std::array<std::map<std::string,GroupState>,Count> states;std::array<std::vector<std::vector<GroupEvent>>,Count> historyPages;std::array<unsigned int,Count> errors{};
    auto token=Storage::newMessageId();auto directory=std::filesystem::current_path()/L"build"/L"tests"/L"group-network"/std::wstring(token.begin(),token.end());
    unsigned short basePort=static_cast<unsigned short>(30000+std::stoul(token.substr(0,4),nullptr,16)%12000);
    auto collect=[&]{for(size_t index=0;index<Count;index++)for(auto& event:peers[index].poll())switch(event.kind){
        case NodeEvent::Kind::Ready:identities[index]=event.peerId;ready[index]=true;break;
        case NodeEvent::Kind::TrustRequired:peers[index].approve(event.peerId,true);break; // Ephemeral test identities only.
        case NodeEvent::Kind::Connected:connected[index].insert(event.peerId);break;
        case NodeEvent::Kind::Disconnected:connected[index].erase(event.peerId);break;
        case NodeEvent::Kind::Groups:states[index].clear();for(const auto& group:event.groups)states[index][group.groupId]=group;break;
        case NodeEvent::Kind::GroupMessage:for(const auto& message:event.groupEvents)require(messages[index].insert(message.eventId).second,"Duplicate group message event");break;
        case NodeEvent::Kind::GroupHistory:historyPages[index].push_back(event.groupEvents);break;
        case NodeEvent::Kind::Error:errors[index]++;std::wcerr<<L"Group peer "<<index<<L": "<<event.text<<L'\n';break;
        default:break;
    }};
    auto wait=[&](auto condition,const char* failure){auto deadline=Clock::now()+std::chrono::seconds(30);while(Clock::now()<deadline){collect();if(condition())return;std::this_thread::sleep_for(std::chrono::milliseconds(20));}throw std::runtime_error(failure);};
    try{
        for(size_t index=0;index<Count;index++){auto path=(directory/std::to_wstring(index)).wstring();{Storage store(path);auto profile=store.profile();profile.listenPort=static_cast<unsigned short>(basePort+index);profile.name=L"Group peer "+std::to_wstring(index);store.saveProfile(profile);}peers[index].start(path);}
        wait([&]{for(bool value:ready)if(!value)return false;return true;},"Four group peers did not start");
        for(size_t index=0;index<Count;index++)peers[index].setGroupAccount(1000+index,L"Account "+std::to_wstring(index));
        for(size_t index=1;index<Count;index++)peers[0].connect(L"127.0.0.1",static_cast<unsigned short>(basePort+index),identities[index]);
        wait([&]{if(connected[0].size()!=Count-1)return false;for(size_t index=1;index<Count;index++)if(!connected[index].count(identities[0]))return false;return true;},"Owner did not establish direct authenticated links");

        auto groupId=peers[0].createGroup(L"Scalable group");require(!groupId.empty(),"Group creation was not queued");
        wait([&]{return states[0].count(groupId)&&states[0][groupId].members.size()==1;},"Owner did not create group locally");
        for(size_t index=1;index<Count;index++)require(peers[0].inviteToGroup(groupId,1000+index,identities[index],L"Account "+std::to_wstring(index)),"Invite was not queued");
        wait([&]{for(size_t index=1;index<Count;index++)if(!states[index].count(groupId)||!states[index][groupId].invitations.count(1000+index))return false;return true;},"Signed invitations did not replicate directly");
        for(size_t index=1;index<Count;index++)require(peers[index].respondToGroupInvite(groupId,true),"Acceptance was not queued");
        wait([&]{for(size_t index=0;index<Count;index++)if(!states[index].count(groupId)||states[index][groupId].members.size()!=Count)return false;return true;},"Arbitrary four-member state did not converge");

        require(peers[0].setGroupAdmin(groupId,1001,true),"Promotion was not queued");
        wait([&]{for(size_t index=0;index<Count;index++)if(states[index][groupId].members.at(1001).role!=GroupRole::Admin)return false;return true;},"Role change did not converge");
        require(peers[1].renameGroup(groupId,L"Peer replicated name"),"Admin rename was not queued");
        wait([&]{for(size_t index=0;index<Count;index++)if(states[index][groupId].name!=L"Peer replicated name")return false;return true;},"Admin rename did not converge");
        require(peers[2].renameGroup(groupId,L"Unauthorized name"),"Unauthorized test command was not queued");
        wait([&]{return errors[2]>0;},"Unauthorized member rename was not rejected");
        for(size_t index=0;index<Count;index++)require(states[index][groupId].name==L"Peer replicated name","Rejected rename changed group state");

        require(peers[1].sendGroup(groupId,L"Direct fanout across four members"),"Group message was not queued");
        wait([&]{for(const auto& received:messages)if(received.size()!=1)return false;return true;},"Group message did not fan out through direct peers");
        peers[3].stop();ready[3]=false;connected[3].clear();wait([&]{return !connected[0].count(identities[3]);},"Offline member disconnect was not observed");
        require(peers[1].sendGroup(groupId,L"Queued for offline member"),"Offline group message was not queued");
        wait([&]{return messages[0].size()==2&&messages[1].size()==2&&messages[2].size()==2;},"Online replicas did not receive second message");
        peers[3].start((directory/L"3").wstring());
        wait([&]{return ready[3]&&messages[3].size()==2&&states[3].count(groupId)&&states[3][groupId].members.size()==Count;},"Restarted member did not recover persisted group and queued message");

        // History delivery is a presentation retrieval boundary. The replicated signed ledger
        // remains intact, but opening a group must not materialize its whole history into WPF.
        for(int index=0;index<100;index++)require(peers[1].sendGroup(groupId,L"Paged group history "+std::to_wstring(index)),"Paged group message was not queued");
        wait([&]{for(const auto& received:messages)if(received.size()!=102)return false;return true;},"Paged group messages did not converge");
        historyPages[0].clear();peers[0].loadGroups();std::this_thread::sleep_for(std::chrono::milliseconds(80));collect();require(historyPages[0].empty(),"Group metadata refresh eagerly materialized history");
        peers[0].loadGroupHistory(groupId);
        wait([&]{return !historyPages[0].empty();},"Newest group history page was not delivered");
        const auto& newest=historyPages[0].back();require(newest.size()==100,"Newest group history page was not bounded to 100 messages");
        std::set<std::string> pageIds;std::uint64_t oldest=newest.front().logicalOrder;for(const auto& event:newest){require(pageIds.insert(event.eventId).second,"Newest group history page contained a duplicate ID");oldest=std::min(oldest,event.logicalOrder);}
        peers[0].loadGroupHistory(groupId,static_cast<std::int64_t>(oldest));
        wait([&]{return historyPages[0].size()>=2;},"Older group history page was not delivered");
        const auto& older=historyPages[0].back();require(!older.empty()&&older.size()<100,"Older group history page did not stop at the history boundary");
        for(const auto& event:older){require(event.logicalOrder<oldest,"Older group history page did not use its before cursor");require(pageIds.insert(event.eventId).second,"Group history pages overlapped by message ID");}

        for(auto& peer:peers)peer.stop();
        for(size_t index=0;index<Count;index++){Storage store((directory/std::to_wstring(index)).wstring());require(store.groupAccount().accountId==1000+index,"Group account binding did not persist");require(!store.groupEvents(groupId).empty(),"Encrypted group ledger did not persist");}
        std::cout<<"PASS: four-member client-owned group, signed invites/accepts, roles, authorization, direct peer fanout, restart persistence, and bounded paged history retrieval.\n";return 0;
    }catch(const std::exception& error){for(auto& peer:peers)peer.stop();std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
