#include "node.hpp"
#include "protocol.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>

void require(bool condition,const char* explanation){if(!condition)throw std::runtime_error(explanation);}
int main(){
    using namespace peerchat;
    using Clock=std::chrono::steady_clock;
    std::array<Node,6> peers;
    std::array<std::string,6> identities;
    std::array<bool,6> ready{};
    std::array<std::set<std::string>,6> connected,received,delivered;
    std::array<unsigned int,6> initialPinRequests{};
    auto token=Storage::newMessageId();
    auto directory=std::filesystem::current_path()/L"build"/L"tests"/L"runtime"/std::wstring(token.begin(),token.end());
    unsigned short basePort=static_cast<unsigned short>(20000+std::stoul(token.substr(0,4),nullptr,16)%20000);
    auto collect=[&]{
        for(size_t index=0;index<peers.size();index++)for(auto& event:peers[index].poll()){
            switch(event.kind){
            case NodeEvent::Kind::Ready:identities[index]=event.peerId;ready[index]=true;break;
            case NodeEvent::Kind::TrustRequired:
                // The normal first device for an authenticated friendship is pinned only by the
                // constrained core operation; this harness does not grant generic approval.
                require(peers[index].pinInitialAuthenticatedFriendIdentity(event.peerId,event.attemptId,1),"Initial identity pin request rejected");
                ++initialPinRequests[index];
                break;
            case NodeEvent::Kind::Connected:connected[index].insert(event.peerId);break;
            case NodeEvent::Kind::Disconnected:connected[index].erase(event.peerId);break;
            case NodeEvent::Kind::Message:
                for(const auto& message:event.messages)if(message.recipient==identities[index]){
                    require(message.text==L"Synthetic encrypted test message \u2603 https://example.test/"||
                        message.text==std::wstring(protocol::MaxChatCharacters,L'X'),"Received payload mismatch");
                    require(received[index].insert(message.id).second,"Duplicate incoming message event");
                }break;
            case NodeEvent::Kind::Delivered:for(const auto& message:event.messages)delivered[index].insert(message.id);break;
            case NodeEvent::Kind::Error:std::wcerr<<L"Peer "<<index<<L": "<<event.text<<L'\n';break;
            default:break;
            }
        }
    };
    auto wait=[&](auto condition,const char* failure){auto deadline=Clock::now()+std::chrono::seconds(25);while(Clock::now()<deadline){collect();if(condition())return;std::this_thread::sleep_for(std::chrono::milliseconds(20));}throw std::runtime_error(failure);};
    try{
        for(size_t index=0;index<peers.size();index++){
            auto path=(directory/std::to_wstring(index)).wstring();
            {Storage store(path);auto profile=store.profile();profile.listenPort=static_cast<unsigned short>(basePort+index);profile.name=L"Test peer "+std::to_wstring(index);store.saveProfile(profile);}
            peers[index].start(path);
        }
        wait([&]{for(bool active:ready)if(!active)return false;return true;},"Six listeners did not start");
        for(size_t index=0;index<peers.size();++index){
            std::vector<AuthenticatedFriend> friends;
            for(size_t other=0;other<peers.size();++other)if(other!=index)friends.push_back({static_cast<std::uint64_t>(other+1),identities[other],{identities[other]}});
            peers[index].replaceAuthenticatedFriends(static_cast<std::uint64_t>(index+1),1,std::move(friends));
        }
        for(size_t from=0;from<peers.size();from++)for(size_t to=from+1;to<peers.size();to++)peers[from].connect(L"localhost",static_cast<unsigned short>(basePort+to),identities[to]);
        wait([&]{for(const auto& active:connected)if(active.size()!=5)return false;return true;},"Six peers did not authenticate all direct links");
        for(const auto count:initialPinRequests)require(count==5,"Authenticated first friend identity did not use restricted automatic pinning");
        for(size_t from=0;from<peers.size();from++)for(size_t to=0;to<peers.size();to++)if(from!=to)require(!peers[from].send(identities[to],L"Synthetic encrypted test message \u2603 https://example.test/").empty(),"Send rejected");
        wait([&]{for(size_t index=0;index<peers.size();index++)if(received[index].size()!=5||delivered[index].size()!=5)return false;return true;},"Bidirectional full mesh messages/acknowledgements failed");
        require(peers[0].send(identities[1],std::wstring(protocol::MaxChatCharacters+1,L'X')).empty(),
            "Oversized outgoing chat message was queued");
        auto nearLimit=peers[0].send(identities[1],std::wstring(protocol::MaxChatCharacters,L'X'));
        require(!nearLimit.empty(),"Near-limit outgoing chat message was rejected");
        wait([&]{return received[1].count(nearLimit)&&delivered[0].count(nearLimit);},
            "Near-limit chat message did not deliver and acknowledge");
        peers[5].stop();connected[5].clear();ready[5]=false;
        wait([&]{for(size_t index=0;index<5;index++)if(connected[index].count(identities[5]))return false;return true;},"Disconnect was not detected");
        std::string queued=peers[0].send(identities[5],L"Synthetic encrypted test message \u2603 https://example.test/");require(!queued.empty(),"Offline message rejected");
        peers[5].start((directory/L"5").wstring());
        wait([&]{return received[5].count(queued)&&delivered[0].count(queued);},"Restart/reconnect did not deliver durable outbox message");
        for(auto& peer:peers)peer.stop();
        for(size_t index=0;index<peers.size();index++){
            Storage store((directory/std::to_wstring(index)).wstring());require(store.profile().peerId==identities[index],"Restart changed identity");require(store.contacts().size()==5,"Saved contacts missing");
            for(size_t other=0;other<peers.size();other++)if(index!=other)require(store.history(identities[other]).size()>=2,"Independent durable history missing");
        }
        // A pending/non-authenticated relationship must not convert a first TLS identity into a
        // trust pin. The remote may explicitly approve in this harness; the local core must still
        // reject its restricted automatic operation and leave its encrypted contacts untouched.
        auto exerciseRejectedInitialPin=[&](const std::wstring& name,std::uint64_t publishedGeneration,bool clearAccount,const char* failure){
            Node local,remote;std::string localId,remoteId;bool localReady=false,remoteReady=false,requested=false,rejected=false;
            auto localPath=(directory/name/L"local").wstring(),remotePath=(directory/name/L"remote").wstring();
            local.start(localPath,static_cast<unsigned short>(basePort+(clearAccount?31:21)),L"Local");remote.start(remotePath,static_cast<unsigned short>(basePort+(clearAccount?32:22)),L"Remote");
            auto deadline=Clock::now()+std::chrono::seconds(8);
            while(Clock::now()<deadline&&(!localReady||!remoteReady)){for(auto& event:local.poll())if(event.kind==NodeEvent::Kind::Ready){localId=event.peerId;localReady=true;}for(auto& event:remote.poll())if(event.kind==NodeEvent::Kind::Ready){remoteId=event.peerId;remoteReady=true;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}
            require(localReady&&remoteReady,"Guard-test listeners did not start");
            if(clearAccount){local.replaceAuthenticatedFriends(900,publishedGeneration,{{901,remoteId,{remoteId}}});local.replaceAuthenticatedFriends(0,publishedGeneration+1,{ });}
            remote.connect(L"localhost",static_cast<unsigned short>(basePort+(clearAccount?31:21)),localId);
            deadline=Clock::now()+std::chrono::seconds(8);
            while(Clock::now()<deadline&&!rejected){
                for(auto& event:local.poll())if(event.kind==NodeEvent::Kind::TrustRequired&&event.peerId==remoteId){
                    if(!requested){require(local.pinInitialAuthenticatedFriendIdentity(event.peerId,event.attemptId,clearAccount?publishedGeneration+1:publishedGeneration),"Restricted pin command was not queued");requested=true;}
                    else rejected=event.text.find(L"Initial automatic device pinning was rejected")!=std::wstring::npos||event.text.find(L"requires a current authenticated friendship")!=std::wstring::npos;
                }
                for(auto& event:remote.poll())if(event.kind==NodeEvent::Kind::TrustRequired)remote.approve(event.peerId,true);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            local.stop();remote.stop();require(requested&&rejected,failure);Storage store(localPath);require(store.contacts().empty(),"Rejected automatic pin altered local trusted contacts");
        };
        exerciseRejectedInitialPin(L"pending-initial-pin",1,false,"Pending/unaccepted friend received an automatic identity pin");
        exerciseRejectedInitialPin(L"stale-profile-initial-pin",1,true,"Stale profile/account snapshot received an automatic identity pin");
        // A route may authenticate TLS yet present a different Peer ID than the account snapshot.
        // Even an explicit bridge request must not turn that mismatch into an initial trust pin.
        {
            Node local,remote;std::string localId,remoteId;bool localReady=false,remoteReady=false,requested=false,rejected=false;
            auto localPath=(directory/L"expected-peer-mismatch"/L"local").wstring(),remotePath=(directory/L"expected-peer-mismatch"/L"remote").wstring();
            local.start(localPath,static_cast<unsigned short>(basePort+35),L"Local");remote.start(remotePath,static_cast<unsigned short>(basePort+36),L"Remote");auto deadline=Clock::now()+std::chrono::seconds(5);
            while(Clock::now()<deadline&&(!localReady||!remoteReady)){for(auto& event:local.poll())if(event.kind==NodeEvent::Kind::Ready){localId=event.peerId;localReady=true;}for(auto& event:remote.poll())if(event.kind==NodeEvent::Kind::Ready){remoteId=event.peerId;remoteReady=true;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}
            require(localReady&&remoteReady,"Expected-peer guard listeners did not start");local.replaceAuthenticatedFriends(1101,1,{{1102,remoteId,{remoteId}}});std::string wrongExpected=remoteId;wrongExpected[0]=wrongExpected[0]=='a'?'b':'a';local.connect(L"localhost",static_cast<unsigned short>(basePort+36),wrongExpected);
            deadline=Clock::now()+std::chrono::seconds(8);while(Clock::now()<deadline&&!rejected){for(auto& event:local.poll())if(event.kind==NodeEvent::Kind::TrustRequired){if(!requested){require(local.pinInitialAuthenticatedFriendIdentity(event.peerId,event.attemptId,1),"Expected-peer pin command was not queued");requested=true;}else rejected=event.text.find(L"Initial automatic device pinning was rejected")!=std::wstring::npos;}for(auto& event:remote.poll())if(event.kind==NodeEvent::Kind::TrustRequired)remote.approve(event.peerId,true);std::this_thread::sleep_for(std::chrono::milliseconds(10));}
            local.stop();remote.stop();require(requested&&rejected,"Expected Peer ID mismatch was silently pinned");Storage store(localPath);require(store.contacts().empty(),"Expected Peer ID mismatch created a trusted contact");
        }
        // A pre-existing key record, even if not marked verified, is historical security state.
        // The restricted path must never replace it merely because the account friendship is valid.
        {
            Node local,remote;std::string localId,remoteId;bool localReady=false,remoteReady=false,requested=false,rejected=false;
            auto localPath=(directory/L"stored-key-mismatch"/L"local").wstring(),remotePath=(directory/L"stored-key-mismatch"/L"remote").wstring();
            remote.start(remotePath,static_cast<unsigned short>(basePort+41),L"Remote");auto deadline=Clock::now()+std::chrono::seconds(5);
            while(Clock::now()<deadline&&!remoteReady){for(auto& event:remote.poll())if(event.kind==NodeEvent::Kind::Ready){remoteId=event.peerId;remoteReady=true;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}
            require(remoteReady,"Stored-key guard remote did not start");Blob historicalKey(65,0xA5);{Storage store(localPath);store.saveContact({remoteId,L"Historical device",historicalKey,{{L"localhost",static_cast<unsigned short>(basePort+41)}},false});}
            local.start(localPath,static_cast<unsigned short>(basePort+42),L"Local");deadline=Clock::now()+std::chrono::seconds(5);
            while(Clock::now()<deadline&&!localReady){for(auto& event:local.poll())if(event.kind==NodeEvent::Kind::Ready){localId=event.peerId;localReady=true;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}
            require(localReady,"Stored-key guard local did not start");local.replaceAuthenticatedFriends(1001,1,{{1002,remoteId,{remoteId}}});local.connect(L"localhost",static_cast<unsigned short>(basePort+41),remoteId);
            deadline=Clock::now()+std::chrono::seconds(8);while(Clock::now()<deadline&&!rejected){for(auto& event:local.poll())if(event.kind==NodeEvent::Kind::TrustRequired&&event.peerId==remoteId){if(!requested){require(local.pinInitialAuthenticatedFriendIdentity(event.peerId,event.attemptId,1),"Stored-key pin command was not queued");requested=true;}else rejected=event.text.find(L"stored device key")!=std::wstring::npos;}for(auto& event:remote.poll())if(event.kind==NodeEvent::Kind::TrustRequired)remote.approve(event.peerId,true);std::this_thread::sleep_for(std::chrono::milliseconds(10));}
            local.stop();remote.stop();require(requested&&rejected,"Stored key mismatch was silently pinned");Storage store(localPath);auto contacts=store.contacts();require(contacts.size()==1&&!contacts[0].verified&&contacts[0].publicKey==historicalKey,"Stored key was overwritten by automatic pinning");
        }
        std::cout<<"PASS: six concurrent peer nodes, 15 direct mutual-TLS links, hostnames, trust, 30 messages and ACKs, disconnect, restart/reconnect, offline outbox, contacts and encrypted histories.\n";
        return 0;
    }catch(const std::exception& error){for(auto& peer:peers)peer.stop();std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
