#include "../portable/reliable_endpoint.hpp"
#include <filesystem>
#include <iostream>

int main(){using namespace peerchat;using portable::ReliableEndpoint;try{
    auto require=[](bool value,const char* reason){if(!value)throw std::runtime_error(reason);};
    auto id=Storage::newMessageId();auto root=std::filesystem::current_path()/L"build"/L"tests"/L"runtime"/std::wstring(id.begin(),id.end());
    Storage a((root/L"a").wstring()),b((root/L"b").wstring());auto alice=a.profile(),bob=b.profile();
    TlsCredentials ac(alice),bc(bob);
    delivery::DeliveryManager ad(a,alice.peerId),bd(b,bob.peerId);
    session::SessionTransportManager as(alice.peerId,alice.peerId,{1}),bs(bob.peerId,bob.peerId,{1});
    reliable::AuthenticatedFriendSnapshot af,bf;af.replace(10,1,{{20,bob.peerId,{bob.peerId}}});bf.replace(20,1,{{10,alice.peerId,{alice.peerId}}});
    std::uint64_t now=1;unsigned received=0,delivered=0;
    auto route=[](std::uint64_t token,bool incoming){return session::RouteObservation{{token},"relay:fixture","authenticated server","opaque relay",incoming,1,session::TransportKind::Relay,session::RouteClass::ServerRelay};};
    auto drain=[&](ReliableEndpoint& endpoint){while(auto event=endpoint.poll()){
        if(event->kind==ReliableEndpoint::EventKind::TrustRequired)require(endpoint.initialPin(1,++now),"Authenticated initial pin rejected");
        if(event->kind==ReliableEndpoint::EventKind::Message){++received;require(event->message&&event->message->sender==alice.peerId&&event->message->text==L"Shared runtime \u65e5\u672c\u8a9e / \U0001f600","Message attribution/payload changed");}
        if(event->kind==ReliableEndpoint::EventKind::Delivered)++delivered;
    }};
    auto exchange=[&](ReliableEndpoint& left,ReliableEndpoint& right){
        for(unsigned round=0;round<100;++round){
            drain(left);drain(right);
            auto forward=left.takeOutput(),backward=right.takeOutput();
            if(forward.empty()&&backward.empty())break;
            if(!forward.empty())right.receive(forward,++now);
            if(!backward.empty())left.receive(backward,++now);
        }
        drain(left);drain(right);
    };
    for(unsigned cycle=0;cycle<100;++cycle){
        auto leftId=std::uint64_t(cycle*2+1),rightId=leftId+1;
        ReliableEndpoint left(alice,a,as,ad,af,{1},{leftId},std::make_unique<TlsSession>(ac,false),bob.peerId,route(leftId,false),++now);
        ReliableEndpoint right(bob,b,bs,bd,bf,{1},{rightId},std::make_unique<TlsSession>(bc,true),alice.peerId,route(rightId,true),++now);
        left.receive({},++now);right.receive({},++now);exchange(left,right);
        require(left.ready()&&right.ready(),"Shared runtime TLS/Hello/trust/Accepted did not activate");
        StoredMessage message{Storage::newMessageId(),alice.peerId,bob.peerId,L"Shared runtime \u65e5\u672c\u8a9e / \U0001f600",42,Delivery::Queued,0,a.nextLogicalOrder()};
        require(a.append(bob.peerId,message),"Durable send failed");left.pump();exchange(left,right);
        require(a.pending(bob.peerId).empty(),"ACK failed to clear outbox");
        require(received==cycle+1&&delivered==cycle+1,"Duplicate/missing message or receipt");
        left.close(false);right.close(false);bool retired=false;
        try{left.receive({},++now);}catch(const std::exception&){retired=true;}
        require(retired&&left.takeOutput().empty(),"Retired endpoint used TLS/output");
    }
    require(a.history(bob.peerId,100).size()==100&&b.history(alice.peerId,100).size()==100,"Persistent history changed");
    std::string pending;
    {
        ReliableEndpoint left(alice,a,as,ad,af,{1},{201},std::make_unique<TlsSession>(ac,false),bob.peerId,route(201,false),++now);
        ReliableEndpoint right(bob,b,bs,bd,bf,{1},{202},std::make_unique<TlsSession>(bc,true),alice.peerId,route(202,true),++now);
        left.receive({},++now);right.receive({},++now);exchange(left,right);require(left.ready()&&right.ready(),"Disconnect fixture did not activate");
        auto contacts=a.contacts();auto known=std::find_if(contacts.begin(),contacts.end(),[&](const auto& contact){return contact.peerId==bob.peerId;});
        pending=Storage::newMessageId();
        reliable::queueText(a,alice,&*known,bob.peerId,pending,L"Shared runtime \u65e5\u672c\u8a9e / \U0001f600",43);
        left.pump();right.receive(left.takeOutput(),++now);drain(right);
        auto lostAck=right.takeOutput();require(!lostAck.empty(),"No ACK was lost by fixture");
        require(a.pending(bob.peerId).size()==1&&received==101&&delivered==100,"Disconnect-before-ACK fixture changed durable state");
        left.close(true);right.close(true);
    }
    {
        ReliableEndpoint left(alice,a,as,ad,af,{1},{203},std::make_unique<TlsSession>(ac,false),bob.peerId,route(203,false),++now);
        ReliableEndpoint right(bob,b,bs,bd,bf,{1},{204},std::make_unique<TlsSession>(bc,true),alice.peerId,route(204,true),++now);
        left.receive({},++now);right.receive({},++now);exchange(left,right);
        require(left.ready()&&right.ready()&&a.pending(bob.peerId).empty()&&received==101&&delivered==101,"Reconnect did not retry same durable ID exactly once");
        auto history=b.history(alice.peerId,100);unsigned copies=0;for(const auto& message:history)if(message.id==pending)++copies;
        require(copies==1,"Retry duplicated durable history");
        af.replace(0,2,{});bool unauthorized=false;
        try{left.receive({},++now);}catch(const std::exception&){unauthorized=true;}
        require(unauthorized,"Logged-out friendship continued accepting transport bytes");left.close(false);right.close(false);
    }
    std::cout<<"PASS: 102 real Schannel/shared-runtime relay lifecycles, Hello/initial pin, existing owners, Unicode, durable messages/ACKs, disconnect-before-ACK retry/dedupe, logout and retired endpoint rejection. HOST ONLY\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
