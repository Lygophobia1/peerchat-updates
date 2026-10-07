#include "node.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
using namespace peerchat;
void require(bool b,const char* text){if(!b)throw std::runtime_error(text);}
int main(){
    Node left,right;std::string a,b;std::uint64_t la=0,ra=0;bool readyLeft=false,readyRight=false;
    auto root=std::filesystem::current_path()/L"build"/L"tests"/L"runtime"/("file-session-"+Storage::newMessageId());
    try{
        left.start((root/L"left").wstring(),0,L"File left",false);right.start((root/L"right").wstring(),0,L"File right",false);
        auto poll=[&]{
            for(auto& e:left.poll()){
                if(e.kind==NodeEvent::Kind::Ready)a=e.peerId;
                if(e.kind==NodeEvent::Kind::RelayRequested)la=e.attemptId;
                if(e.kind==NodeEvent::Kind::TrustRequired)require(left.pinInitialAuthenticatedFriendIdentity(e.peerId,e.attemptId,1),"left constrained identity pin rejected");
                if(e.kind==NodeEvent::Kind::Connected)readyLeft=true;
                if(e.kind==NodeEvent::Kind::Disconnected)readyLeft=false;
                if(e.kind==NodeEvent::Kind::Error)std::wcerr<<L"left: "<<e.text<<L"\n";
            }
            for(auto& e:right.poll()){
                if(e.kind==NodeEvent::Kind::Ready)b=e.peerId;
                if(e.kind==NodeEvent::Kind::RelayRequested)ra=e.attemptId;
                if(e.kind==NodeEvent::Kind::TrustRequired)require(right.pinInitialAuthenticatedFriendIdentity(e.peerId,e.attemptId,1),"right constrained identity pin rejected");
                if(e.kind==NodeEvent::Kind::Connected)readyRight=true;
                if(e.kind==NodeEvent::Kind::Disconnected)readyRight=false;
                if(e.kind==NodeEvent::Kind::Error)std::wcerr<<L"right: "<<e.text<<L"\n";
            }
        };
        auto pump=[&]{unsigned char bytes[65536];if(la&&ra){
            for(int n=0;n<8;++n){int count=left.takeRelayOutput(la,bytes,sizeof(bytes),0);if(count<=0)break;require(right.relayReceive(ra,bytes,count),"right relay input rejected");}
            for(int n=0;n<8;++n){int count=right.takeRelayOutput(ra,bytes,sizeof(bytes),0);if(count<=0)break;require(left.relayReceive(la,bytes,count),"left relay input rejected");}
        }poll();};
        auto wait=[&](auto condition,const char* failure,int seconds=12){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);while(std::chrono::steady_clock::now()<end){pump();if(condition())return;std::this_thread::sleep_for(std::chrono::milliseconds(5));}throw std::runtime_error(failure);};
        wait([&]{return !a.empty()&&!b.empty();},"nodes did not open");
        bool denied=false;try{left.fileTransfer(1,b,"",L"not-a-file");}catch(...){denied=true;}require(denied,"unverified device accepted file send");
        left.replaceAuthenticatedFriends(71,1,{{72,b,{b}}});right.replaceAuthenticatedFriends(72,1,{{71,a,{a}}});
        left.setNetworkContext(1,L"file-relay-test");right.setNetworkContext(1,L"file-relay-test");
        left.updateCandidates(b,L"",0,L"",0,true,1,true);right.updateCandidates(a,L"",0,L"",0,true,1,true);
        wait([&]{return la&&ra;},"no relay demands");
        require(left.relayReady(la,a>b)&&right.relayReady(ra,b>a),"relay binding failed");
        wait([&]{return readyLeft&&readyRight;},"mutual TLS and device trust failed");
        auto name=std::wstring(L"binary-") + L"\u65e5\u672c.bin";auto source=root/name;std::vector<char> bytes(2*1024*1024);
        for(size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<char>(i*19);
        {std::ofstream file(source,std::ios::binary);file.write(bytes.data(),bytes.size());require(file.good(),"test source write failed");}
        auto destination=root/L"received";std::filesystem::create_directory(destination);
        auto id=left.fileTransfer(1,b,"",source.wstring());std::string narrow(id.begin(),id.end());
        wait([&]{return right.fileTransfer(0).find(id)!=std::wstring::npos;},"offer not delivered over peer TLS");
        right.fileTransfer(2,"",narrow,destination.wstring());
        wait([&]{return right.fileTransfer(0).find(L"\"done\":32768")!=std::wstring::npos||right.fileTransfer(0).find(L"\"done\":65536")!=std::wstring::npos;},"first bounded chunks did not arrive");
        auto oldLeft=la,oldRight=ra;left.relayClosed(la,L"file test reconnect");right.relayClosed(ra,L"file test reconnect");
        la=ra=0;readyLeft=readyRight=false;
        wait([&]{return la&&ra&&la!=oldLeft&&ra!=oldRight;},"existing manager failed to replace relay",15);
        require(left.relayReady(la,a>b)&&right.relayReady(ra,b>a),"replacement relay rejected");
        wait([&]{return readyLeft&&readyRight;},"replacement TLS did not establish");
        wait([&]{return left.fileTransfer(0).find(L"\"state\":4")!=std::wstring::npos&&right.fileTransfer(0).find(L"\"state\":4")!=std::wstring::npos;},"file did not resume and complete",30);
        auto final=destination/(id+L"-"+name);std::ifstream file(final,std::ios::binary);std::vector<char> actual((std::istreambuf_iterator<char>(file)),{});
        require(actual==bytes,"completed file bytes mismatch");
        require(!std::filesystem::exists(destination/(id+L".peerchat-part")),"partial leaked after completion");
        auto message=left.send(b,L"Messaging survives file completion");require(!message.empty(),"text enqueue failed");
        bool delivered=false;
        auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(std::chrono::steady_clock::now()<end&&!delivered){
            unsigned char data[65536];int count=left.takeRelayOutput(la,data,sizeof(data),0);if(count>0)right.relayReceive(ra,data,count);
            count=right.takeRelayOutput(ra,data,sizeof(data),0);if(count>0)left.relayReceive(la,data,count);
            right.poll();for(auto& e:left.poll())if(e.kind==NodeEvent::Kind::Delivered)for(auto& m:e.messages)if(m.id==message)delivered=true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        require(delivered,"file transfer disturbed DeliveryManager message/ACK");
        left.stop();right.stop();
        std::cout<<"Native file session: unverified rejection / mutual peer TLS / opaque relay / Unicode binary / reconnect resume / final hash / message ACK coexistence PASS\n";
    }catch(const std::exception& error){left.stop();right.stop();std::cerr<<error.what()<<"\n";return 1;}
}
