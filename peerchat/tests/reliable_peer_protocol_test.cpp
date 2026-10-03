#include "../storage.cpp"
#include "../reliable_peer_protocol.hpp"
#include "../shared_peer_runtime.hpp"
#include <iostream>

// Test-only byte-feed transport. Real peer-TLS proof is tested separately; this
// fixture checks callback ordering/retirement, not TLS authentication itself.
struct TestTls {
    peerchat::Blob key,plain;
    unsigned receives=0;
    bool ended=false;
    void receive(const peerchat::Blob& bytes){++receives;plain.insert(plain.end(),bytes.begin(),bytes.end());}
    bool ready()const{return true;}
    bool closed()const{return ended;}
    peerchat::Blob remotePublicKey()const{return key;}
    peerchat::Blob takePlaintext(){auto bytes=std::move(plain);plain.clear();return bytes;}
};

int main(){using namespace peerchat;try{
    auto require=[](bool value,const char* reason){if(!value)throw std::runtime_error(reason);};
    auto hello=reliable::helloPacket(protocol::CapabilityMutualTls|protocol::CapabilityLogicalOrdering,0,L"Device");
    protocol::Reader helloInput(hello.data);require(reliable::receiveHello(helloInput,true).name==L"Device","Relay Hello changed");
    bool noListenerRejected=false;
    try{protocol::Reader bad(hello.data);reliable::receiveHello(bad,false);}catch(...){noListenerRejected=true;}
    require(noListenerRejected,"Direct zero-port Hello accepted");
    reliable::FrameQueue queue;
    auto wire=hello.frame();
    for(size_t index=0;index<wire.size();++index){queue.receive(Blob{wire[index]});if(index+1<wire.size())require(!queue.take(),"Partial frame emitted");}
    require(queue.take()==hello.data&&!queue.take(),"Fragmented frame mismatch");
    queue.receive(wire);queue.receive(wire);require(queue.take()==hello.data&&queue.take()==hello.data&&!queue.take(),"Coalesced frame mismatch");
    bool boundRejected=false;try{queue.receive(Blob(256*1024+1));}catch(...){boundRejected=true;}
    require(boundRejected,"Unbounded peer stream accepted");
    auto root=std::filesystem::current_path()/L"build"/L"tests"/L"reliable-wire"/storage_detail::wide(Storage::newMessageId());
    Storage sender((root/L"sender").wstring()),receiver((root/L"receiver").wstring());
    auto a=sender.profile(),b=receiver.profile();
    auto rejects=[&](const Blob& bytes,bool relay){
        bool failed=false;
        try{protocol::Reader reader(bytes);reliable::receiveHello(reader,relay);}catch(const std::exception&){failed=true;}
        require(failed,"Malformed Hello accepted");
    };
    auto trailing=hello.data;trailing.push_back(0);rejects(trailing,true);
    auto wrongVersion=hello.data;wrongVersion[2]=2;rejects(wrongVersion,true);
    auto truncated=hello.data;truncated.pop_back();rejects(truncated,true);
    rejects(reliable::helloPacket(0,0,L"Device").data,true);
    rejects(reliable::helloPacket(protocol::CapabilityMutualTls,0,std::wstring(65,L'x')).data,true);
    protocol::Writer invalidPort(protocol::Hello);invalidPort.number(protocol::CapabilityMutualTls);invalidPort.number(65536);invalidPort.text(L"Device");rejects(invalidPort.data,true);
    for(unsigned cycle=0;cycle<1000;++cycle){
        reliable::SharedPeerRuntime state;state.expected=a.peerId;
        protocol::Reader reader(hello.data);
        state.observeHello(reader,true,a.publicKey,b.peerId);
        require(state.hello&&!state.identityMismatch&&state.logicalOrdering&&!state.approved,"Hello state changed trust");
        reliable::AuthenticatedFriendSnapshot friends;
        std::map<std::string,Contact> pins;
        using Pin=reliable::InitialPinDecision;
        require(friends.initialPin(&state,true,a.peerId,0,&a.publicKey,pins)==Pin::NoCurrentFriendship,"Absent friendship authorized pin");
        friends.replace(10,7,{{11,a.peerId,{a.peerId}}});
        require(friends.initialPin(&state,true,a.peerId,7,&a.publicKey,pins)==Pin::Allowed,"Current initial friend pin rejected");
        require(friends.initialPin(&state,true,a.peerId,6,&a.publicKey,pins)==Pin::StaleIdentity,"Stale account snapshot authorized pin");
        require(friends.initialPin(&state,false,a.peerId,7,&a.publicKey,pins)==Pin::StaleIdentity,"Dead attempt authorized pin");
        require(friends.initialPin(&state,true,a.peerId,7,&b.publicKey,pins)==Pin::StaleIdentity,"Wrong TLS key authorized pin");
        pins.emplace(a.peerId,Contact{a.peerId,L"Device",a.publicKey,{},true});
        require(friends.initialPin(&state,true,a.peerId,7,&a.publicKey,pins)==Pin::HistoricalPin,"Historical verified identity overwritten");
        pins[a.peerId].verified=false;pins[a.peerId].publicKey=b.publicKey;
        require(friends.initialPin(&state,true,a.peerId,7,&a.publicKey,pins)==Pin::StoredKeyMismatch,"Stored mismatched key overwritten");
        friends.replace(0,8,{});pins.clear();
        require(friends.initialPin(&state,true,a.peerId,8,&a.publicKey,pins)==Pin::NoCurrentFriendship,"Logout preserved friendship authorization");
        Contact known{a.peerId,L"Device",a.publicKey,{},true};
        require(state.verifiedContactMatches(&known,a.publicKey),"Verified identity rejected");
        auto wrong=known;wrong.publicKey=b.publicKey;bool badPin=false;
        try{state.verifiedContactMatches(&wrong,a.publicKey);}catch(const std::exception&){badPin=true;}
        require(badPin,"Wrong verified key accepted");
        protocol::Writer accepted(protocol::Accepted);protocol::Reader acceptedInput(accepted.data);
        state.observeAccepted(acceptedInput);require(state.remoteApproved&&!state.approved,"Remote approval created local trust");
        bool duplicate=false;try{protocol::Reader repeat(accepted.data);state.observeAccepted(repeat);}catch(const std::exception&){duplicate=true;}
        require(duplicate,"Duplicate Accepted accepted");
        reliable::PeerProtocolState mismatch;mismatch.expected=b.peerId;
        protocol::Reader mismatchInput(hello.data);mismatch.observeHello(mismatchInput,true,a.publicKey,b.peerId);
        require(mismatch.identityMismatch&&!mismatch.approved,"Fingerprint mismatch became trusted");
        bool self=false;try{reliable::PeerProtocolState local;protocol::Reader selfInput(hello.data);local.observeHello(selfInput,true,a.publicKey,a.peerId);}catch(const std::exception&){self=true;}
        require(self,"Self identity accepted");
        TestTls transport{a.publicKey,{},0,false};reliable::SharedPeerRuntime runtime;
        runtime.expected=a.peerId;unsigned sends=0,callbacks=0;
        auto twoFrames=hello.frame();auto next=accepted.frame();twoFrames.insert(twoFrames.end(),next.begin(),next.end());
        runtime.receive(transport,twoFrames,b,protocol::CapabilityMutualTls,0,
            []{return true;},[]{},[&](protocol::Writer& value){require(value.data[3]==protocol::Hello,"Unexpected initial packet");++sends;},
            [&](const Blob& bytes){++callbacks;protocol::Reader input(bytes);runtime.enter(input,true,a.publicKey,b.peerId);runtime.stop();});
        require(sends==1&&callbacks==1&&runtime.stopped()&&!runtime.remoteApproved,"Retired runtime dispatched queued frame");
        bool retired=false;
        try{runtime.receive(transport,{},b,protocol::CapabilityMutualTls,0,[]{return true;},[]{},[](protocol::Writer&){},[](const Blob&){});}catch(const std::exception&){retired=true;}
        require(retired&&transport.receives==1&&callbacks==1,"Callback/transport invocation after stop");
    }
    delivery::DeliveryManager sending(sender,a.peerId),receiving(receiver,b.peerId);
    StoredMessage message;message.id=Storage::newMessageId();message.sender=a.peerId;message.recipient=b.peerId;
    message.text=L"Unicode \u65e5\u672c\u8a9e / \U0001f600\nsecond line";message.utcMilliseconds=1234;message.logicalOrder=42;
    require(sender.append(b.peerId,message),"Outbox insert failed");
    require(sending.authorizeTransport({9},b.peerId),"Transport binding failed");
    auto encoded=reliable::textPacket(message,true);
    protocol::Writer control(protocol::Text);control.string(message.id);control.number(message.logicalOrder);control.number(message.utcMilliseconds);control.text(message.text);
    require(encoded.data==control.data,"Existing wire changed");
    protocol::Reader input(encoded.data);
    auto received=reliable::receiveText(input,true,a.peerId,b,receiver,receiving);
    require(received.inserted&&received.message&&received.message->text==message.text,"Receive/commit failed");
    require(received.message->sender==a.peerId&&received.message->recipient==b.peerId,"Authenticated attribution changed");
    protocol::Reader repeated(encoded.data);
    require(!reliable::receiveText(repeated,true,a.peerId,b,receiver,receiving).inserted,"Replay duplicated history");
    require(receiver.history(a.peerId).size()==1,"History duplicated");
    protocol::Writer ack(protocol::Ack);ack.string(message.id);protocol::Reader ackInput(ack.data);
    auto id=reliable::receiveAck(ackInput);
    require(!sending.acknowledgeMessage(b.peerId,{9},id),"Unsubmitted ACK accepted");
    require(sending.submittedMessage(b.peerId,id,{9}),"Submission record failed");
    require(!sending.acknowledgeMessage(a.peerId,{9},id),"Wrong source ACK accepted");
    require(sending.acknowledgeMessage(b.peerId,{9},id)&&sender.pending(b.peerId).empty(),"ACK durability failed");
    auto legacy=reliable::textPacket(message,false);protocol::Reader oldInput(legacy.data);
    require(!reliable::receiveText(oldInput,false,a.peerId,b,receiver,receiving).inserted,"Legacy retry changed dedupe");
    auto damaged=encoded.data;damaged.pop_back();bool rejected=false;
    try{protocol::Reader bad(damaged);reliable::receiveText(bad,true,a.peerId,b,receiver,receiving);}catch(...){rejected=true;}
    require(rejected&&receiver.history(a.peerId).size()==1,"Truncated packet accepted");
    require(receiver.nextLogicalOrder()>42,"Ordering observation lost");
    std::cout<<"PASS: 1000 shared Hello/identity/Accepted lifecycles, malformed rejection, wire compatibility, Unicode, history, retry dedupe and scoped ACK\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
