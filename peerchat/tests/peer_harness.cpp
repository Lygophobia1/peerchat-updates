// Test-only command-line adapter. It uses the same Node implementation as the GUI.
// Trust approval is explicit through stdin; the harness never auto-trusts peers.
#define _WIN32_WINNT 0x0602
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "node.hpp"
#include <atomic>
#include <iostream>
#include <sstream>
#include <thread>

std::wstring fromUtf8(const std::string& text){int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);std::wstring result(count,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),count);return result;}
int main(int argumentCount,char** arguments){
    if(argumentCount!=3)return 2;
    peerchat::Node node;node.start(fromUtf8(arguments[1]),static_cast<unsigned short>(std::stoul(arguments[2])),L"Synthetic test peer");
    std::atomic<bool> running{true};
    std::thread input([&]{std::string line;while(std::getline(std::cin,line)){
        std::istringstream command(line);std::string action,peer,host;unsigned int port=0;command>>action;
        if(action=="QUIT")break;
        if(action=="CONNECT"){command>>host>>port>>peer;node.connect(fromUtf8(host),static_cast<unsigned short>(port),peer);}
        else if(action=="APPROVE"){command>>peer;node.approve(peer,true);}
        else if(action=="SEND"){command>>peer;node.send(peer,L"Six-process test https://example.test/ \u2603");}
        else if(action=="HISTORY"){command>>peer;node.loadHistory(peer);}
    }running=false;});
    while(running){for(auto& event:node.poll()){
        using Kind=peerchat::NodeEvent::Kind;
        switch(event.kind){
        case Kind::Ready:std::cout<<"READY "<<event.peerId<<'\n';break;
        case Kind::TrustRequired:std::cout<<"TRUST "<<event.peerId<<'\n';break;
        case Kind::Connected:std::cout<<"CONNECTED "<<event.peerId<<'\n';break;
        case Kind::Disconnected:std::cout<<"DISCONNECTED "<<event.peerId<<'\n';break;
        case Kind::Error:std::cout<<"ERROR "<<event.peerId<<'\n';break;
        case Kind::Message:for(const auto& message:event.messages){if(message.text!=L"Six-process test https://example.test/ \u2603")std::cout<<"PAYLOAD_ERROR\n";std::cout<<"MESSAGE "<<event.peerId<<' '<<message.id<<' '<<message.sender<<'\n';}break;
        case Kind::Delivered:for(const auto& message:event.messages)std::cout<<"DELIVERED "<<message.id<<'\n';break;
        case Kind::History:std::cout<<"HISTORY "<<event.peerId<<' '<<event.messages.size()<<'\n';break;
        case Kind::Contacts:std::cout<<"CONTACTS "<<event.contacts.size()<<'\n';break;
        case Kind::Groups:case Kind::GroupMessage:case Kind::GroupHistory:case Kind::GroupDelivered:case Kind::VoiceCall:case Kind::ConnectionAttempt:case Kind::ReconnectScheduled:case Kind::ConnectionLifecycle:case Kind::SessionSnapshot:case Kind::RelayRequested:break;
        }
        std::cout.flush();
    }std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    input.join();node.stop();return 0;
}
