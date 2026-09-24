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
    auto token=Storage::newMessageId();
    auto directory=std::filesystem::current_path()/L"build"/L"tests"/L"runtime"/std::wstring(token.begin(),token.end());
    unsigned short basePort=static_cast<unsigned short>(20000+std::stoul(token.substr(0,4),nullptr,16)%20000);
    auto collect=[&]{
        for(size_t index=0;index<peers.size();index++)for(auto& event:peers[index].poll()){
            switch(event.kind){
            case NodeEvent::Kind::Ready:identities[index]=event.peerId;ready[index]=true;break;
            case NodeEvent::Kind::TrustRequired:peers[index].approve(event.peerId,true);break; // Test identities only.
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
        for(size_t from=0;from<peers.size();from++)for(size_t to=from+1;to<peers.size();to++)peers[from].connect(L"localhost",static_cast<unsigned short>(basePort+to),identities[to]);
        wait([&]{for(const auto& active:connected)if(active.size()!=5)return false;return true;},"Six peers did not authenticate all direct links");
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
        std::cout<<"PASS: six concurrent peer nodes, 15 direct mutual-TLS links, hostnames, trust, 30 messages and ACKs, disconnect, restart/reconnect, offline outbox, contacts and encrypted histories.\n";
        return 0;
    }catch(const std::exception& error){for(auto& peer:peers)peer.stop();std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
