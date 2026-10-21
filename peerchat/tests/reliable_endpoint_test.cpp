#include "../portable/reliable_endpoint.hpp"
#include "../windows_transfer_io.hpp"
#include "../platform/windows_sql_database.hpp"
#include "../platform/windows_record_protection.hpp"
#include <filesystem>
#include <iostream>
#include <chrono>

namespace {
struct CountingProtector final:peerchat::platform::RecordProtector {
    peerchat::platform::WindowsRecordProtector delegate;
    std::size_t reads=0;
    peerchat::Blob protect(const peerchat::Blob& value,const std::string& label) override{return delegate.protect(value,label);}
    peerchat::Blob unprotect(const peerchat::Blob& value,const std::string& label) override{++reads;return delegate.unprotect(value,label);}
};
struct MemoryFile final:peerchat::transfer::Source {
    peerchat::Blob bytes;std::string label;
    MemoryFile(peerchat::Blob value,std::string name):bytes(std::move(value)),label(std::move(name)){}
    std::uint64_t size()const override{return bytes.size();}std::string name()const override{return label;}
    peerchat::Blob read(std::uint64_t offset,size_t count)override{return {bytes.begin()+offset,bytes.begin()+offset+count};}
};
struct ReceivedFile {peerchat::Blob bytes;bool committed=false;};
struct MemoryDestination final:peerchat::transfer::Sink {
    std::shared_ptr<ReceivedFile> result;explicit MemoryDestination(std::shared_ptr<ReceivedFile> value):result(std::move(value)){}
    void append(const peerchat::Blob& bytes)override{result->bytes.insert(result->bytes.end(),bytes.begin(),bytes.end());}
    void commit()override{result->committed=true;}
};
}

int main(){using namespace peerchat;using portable::ReliableEndpoint;try{
    auto require=[](bool value,const char* reason){if(!value)throw std::runtime_error(reason);};
    for(auto version:{1u,2u})for(unsigned kind=1;kind<=6;++kind){
        protocol::Writer frame(protocol::FileTransfer);frame.number(version);frame.number(kind);
        require(scheduling::admitsTransfer(frame.data,scheduling::BulkOutputHighWater)==(kind!=static_cast<unsigned>(transfer::Kind::Chunk)),"Only bulk payload should consume the local output reserve");
    }
    auto id=Storage::newMessageId();auto root=std::filesystem::current_path()/L"build"/L"tests"/L"runtime"/std::wstring(id.begin(),id.end());
    auto protectedReads=std::make_shared<CountingProtector>();
    Storage a(platform::openWindowsSqlDatabase((root/L"a").wstring()),protectedReads),b((root/L"b").wstring());auto alice=a.profile(),bob=b.profile();
    TlsCredentials ac(alice),bc(bob);
    delivery::DeliveryManager ad(a,alice.peerId),bd(b,bob.peerId);
    session::SessionTransportManager as(alice.peerId,alice.peerId,{1}),bs(bob.peerId,bob.peerId,{1});
    reliable::AuthenticatedFriendSnapshot af,bf;af.replace(10,1,{{20,bob.peerId,{bob.peerId}}});bf.replace(20,1,{{10,alice.peerId,{alice.peerId}}});
    std::uint64_t now=1;unsigned received=0,delivered=0;std::vector<std::uint64_t> ackMicroseconds;
    auto route=[](std::uint64_t token,bool incoming){return session::RouteObservation{{token},"relay:fixture","authenticated server","opaque relay",incoming,1,session::TransportKind::Relay,session::RouteClass::ServerRelay};};
    auto drain=[&](ReliableEndpoint& endpoint){while(auto event=endpoint.poll()){
        if(event->kind==ReliableEndpoint::EventKind::TrustRequired){
            reliable::InitialPinDecision decision=reliable::InitialPinDecision::StoredKeyMismatch;
            require(endpoint.initialPin(1,++now,&decision)&&decision==reliable::InitialPinDecision::Allowed,"Authenticated initial pin/diagnostic rejected");
        }
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
        const auto queuedAt=std::chrono::steady_clock::now();
        require(a.append(bob.peerId,message),"Durable send failed");left.pump();exchange(left,right);
        require(a.pending(bob.peerId).empty(),"ACK failed to clear outbox");
        require(received==cycle+1&&delivered==cycle+1,"Duplicate/missing message or receipt");
        ackMicroseconds.push_back(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-queuedAt).count()));
        left.close(false);right.close(false);bool retired=false;
        try{left.receive({},++now);}catch(const std::exception&){retired=true;}
        require(retired&&left.takeOutput().empty(),"Retired endpoint used TLS/output");
    }
    std::sort(ackMicroseconds.begin(),ackMicroseconds.end());std::uint64_t totalMicros=0;for(auto value:ackMicroseconds)totalMicros+=value;
    std::cout<<"HOST LATENCY: n=100 boundary=before-local-append-to-validated-durable-recipient-ACK median_us="<<ackMicroseconds[49]<<" p95_us="<<ackMicroseconds[94]<<" mean_us="<<totalMicros/ackMicroseconds.size()<<"; in-memory opaque carriage, real Schannel/SQLite, handshake excluded, no physical RTT/FCM claim\n";
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
    af.replace(10,3,{{20,bob.peerId,{bob.peerId}}});
    {
        ReliableEndpoint sender(alice,a,as,ad,af,{1},{205},std::make_unique<TlsSession>(ac,false),bob.peerId,route(205,false),++now);
        ReliableEndpoint receiver(bob,b,bs,bd,bf,{1},{206},std::make_unique<TlsSession>(bc,true),alice.peerId,route(206,true),++now);
        sender.receive({},++now);receiver.receive({},++now);exchange(sender,receiver);
        require(sender.ready()&&receiver.ready(),"Full message window fixture did not authenticate");
        auto beforeReceived=received,beforeDelivered=delivered;
        for(unsigned n=0;n<17;++n){StoredMessage message{Storage::newMessageId(),alice.peerId,bob.peerId,L"Shared runtime \u65e5\u672c\u8a9e / \U0001f600",43,Delivery::Queued,0,a.nextLogicalOrder()};require(a.append(bob.peerId,message),"Full message window queue failed");}
        for(unsigned batch=0;batch<4;++batch)sender.pump();
        require(ad.inFlightCount({205})==16,"Full message window did not fill");
        auto beforeReads=protectedReads->reads;
        for(unsigned n=0;n<8;++n)sender.pump();
        auto afterReads=protectedReads->reads;
        std::cout<<"HOST FULL WINDOW: redundant_protected_reads="<<(afterReads-beforeReads)<<"\n";
        require(afterReads==beforeReads,"Full message window reread protected outbox records");
        auto forward=sender.takeOutput();require(!forward.empty(),"Full window produced no TLS output");receiver.receive(forward,++now);exchange(sender,receiver);
        require(a.pending(bob.peerId).empty()&&received==beforeReceived+17&&delivered==beforeDelivered+17,"Full window ACK did not resume final message");
        sender.close(false);receiver.close(false);
    }
    transfer::TransferManager uploads([]{return std::make_unique<transfer::windows::Sha256>();});
    transfer::TransferManager downloads([]{return std::make_unique<transfer::windows::Sha256>();});
    unsigned fileFrames=0;Blob capturedBulk;
    ReliableEndpoint left(alice,a,as,ad,af,{1},{301},std::make_unique<TlsSession>(ac,false),bob.peerId,route(301,false),++now,{},
        [&](const std::string& peer,const Blob& frame,std::uint64_t time){++fileFrames;uploads.receive(peer,frame,time);});
    ReliableEndpoint right(bob,b,bs,bd,bf,{1},{302},std::make_unique<TlsSession>(bc,true),alice.peerId,route(302,true),++now,{},
        [&](const std::string& peer,const Blob& frame,std::uint64_t time){++fileFrames;protocol::Reader r(frame);r.number();if(r.number()==static_cast<unsigned>(transfer::Kind::Chunk)&&frame.size()>65536)capturedBulk=frame;downloads.receive(peer,frame,time);});
    protocol::Writer premature(protocol::FileTransfer);require(!left.sendTransfer(premature.data),"File sent before authenticated winner");
    left.receive({},++now);right.receive({},++now);exchange(left,right);require(left.ready()&&right.ready(),"File route failed authentication");
    require(left.permitsTransferPipeline()&&right.permitsTransferPipeline(),"Portable endpoint omitted negotiated pipeline capability");
    uploads.capability(bob.peerId,301,left.permitsTransferPipeline());downloads.capability(alice.peerId,302,right.permitsTransferPipeline());
    uploads.configure("",{65536,16,0});downloads.configure("",{65536,16,0});
    Blob picture(1048576+100000),document(786432+70000);for(size_t i=0;i<picture.size();++i)picture[i]=static_cast<unsigned char>(i);
    for(size_t i=0;i<document.size();++i)document[i]=static_cast<unsigned char>(255-i);
    auto pictureId=Storage::newMessageId(),documentId=Storage::newMessageId();
    uploads.offer(bob.peerId,pictureId,std::make_unique<MemoryFile>(picture,"picture.png"),++now);
    downloads.offer(alice.peerId,documentId,std::make_unique<MemoryFile>(document,"file.bin"),++now);
    auto receivedPicture=std::make_shared<ReceivedFile>(),receivedDocument=std::make_shared<ReceivedFile>();
    for(unsigned n=0;n<100;++n){
        uploads.tick(++now,[&](const std::string&){return left.ready()?301:0;},[&](const std::string&,const Blob& bytes){return left.sendTransfer(bytes);});
        downloads.tick(++now,[&](const std::string&){return right.ready()?302:0;},[&](const std::string&,const Blob& bytes){return right.sendTransfer(bytes);});exchange(left,right);
        for(const auto& item:downloads.snapshots())if(item.state==transfer::State::AwaitingAcceptance)downloads.accept(item.metadata.id,std::make_unique<MemoryDestination>(receivedPicture),++now);
        for(const auto& item:uploads.snapshots())if(item.state==transfer::State::AwaitingAcceptance)uploads.accept(item.metadata.id,std::make_unique<MemoryDestination>(receivedDocument),++now);
        if(uploads.completedRecords().size()==2&&downloads.completedRecords().size()==2)break;
    }
    require(receivedPicture->committed&&receivedPicture->bytes==picture&&receivedDocument->committed&&receivedDocument->bytes==document&&fileFrames>8,"Bidirectional authenticated binary transfer corrupted or incomplete");
    require(uploads.completedRecords().size()==2&&downloads.completedRecords().size()==2,"File completion acknowledgement missing");
    require(uploads.diagnostics(now)[0].find("effective_chunk_bytes=65536 effective_window=16")!=std::string::npos,"Portable 64 KiB frame/window did not activate");
    // Hold the transport consumer: repeated valid payload must not fill a MiB
    // ahead of newly queued text. Duplicate completed chunks preserve integrity.
    require(!capturedBulk.empty(),"No authenticated payload captured for blocked-writer fixture");
    unsigned queuedBulk=0;while(queuedBulk<40&&left.sendTransfer(capturedBulk))++queuedBulk;
    require(queuedBulk<=3,"Bulk output prefilled ahead of text beyond bounded control reserve");
    StoredMessage priorityText{Storage::newMessageId(),alice.peerId,bob.peerId,L"Shared runtime \u65e5\u672c\u8a9e / \U0001f600",44,Delivery::Queued,0,a.nextLogicalOrder()};
    require(a.append(bob.peerId,priorityText),"Priority text durable send failed");left.pump();
    unsigned forwardDrains=0;while(forwardDrains<32&&!b.message(priorityText.id)){
        auto forward=left.takeOutput();if(!forward.empty()){++forwardDrains;right.receive(forward,++now);}
        auto backward=right.takeOutput();if(!backward.empty())left.receive(backward,++now);drain(left);drain(right);
    }
    exchange(left,right);require(b.message(priorityText.id).has_value()&&a.pending(bob.peerId).empty()&&forwardDrains<=4,"Text/ACK delayed behind bulk backlog");
    std::cout<<"HOST PRIORITY: held_consumer_bulk_frames="<<queuedBulk<<" text_forward_64KiB_drains="<<forwardDrains<<"; actual Schannel ciphertext order preserved; not physical network latency\n";
    // Capacity rejection is an application refusal, not TLS/session failure.
    // Four unaccepted offers are retained; the fifth must leave the winner live.
    for(unsigned n=0;n<5;++n){protocol::Writer offer(protocol::FileTransfer);offer.number(1);offer.number(static_cast<unsigned>(transfer::Kind::Offer));offer.string(Storage::newMessageId());offer.string("capacity.bin");offer.number(1);offer.bytes(Blob(32,0));require(left.sendTransfer(offer.data),"Capacity fixture send failed");exchange(left,right);}
    require(left.ready()&&right.ready()&&downloads.snapshots().size()==6,"Capacity rejection closed the authenticated messaging winner");
    StoredMessage afterCapacity{Storage::newMessageId(),alice.peerId,bob.peerId,L"Shared runtime \u65e5\u672c\u8a9e / \U0001f600",44,Delivery::Queued,0,a.nextLogicalOrder()};
    require(a.append(bob.peerId,afterCapacity),"Post-capacity send persistence failed");left.pump();exchange(left,right);require(a.pending(bob.peerId).empty()&&b.message(afterCapacity.id).has_value(),"Messaging failed after file capacity rejection");
    // A retained backlog must not require decrypting 100 rows before writing
    // a newly queued chat message. Keep old rows fair and exact ACKs durable.
    std::vector<std::string> backlog;
    for(unsigned i=0;i<80;++i){StoredMessage m{Storage::newMessageId(),alice.peerId,bob.peerId,L"Shared runtime \u65e5\u672c\u8a9e / \U0001f600",44,Delivery::Queued,0,a.nextLogicalOrder()};require(a.append(bob.peerId,m),"Backlog persistence failed");backlog.push_back(m.id);}
    const auto readsBefore=protectedReads->reads;left.pump();
    require(protectedReads->reads-readsBefore<=4,"One live pump decrypted more than four legacy messages");
    StoredMessage newest{Storage::newMessageId(),alice.peerId,bob.peerId,L"Shared runtime \u65e5\u672c\u8a9e / \U0001f600",44,Delivery::Queued,0,a.nextLogicalOrder()};require(a.append(bob.peerId,newest),"Newest persistence failed");
    unsigned turns=0;
    while(turns++<8&&!b.message(newest.id)){
        auto forward=left.takeOutput();if(!forward.empty())right.receive(forward,++now);
        auto backward=right.takeOutput();if(!backward.empty())left.receive(backward,++now);
        drain(left);drain(right);
    }
    require(b.message(newest.id).has_value(),"New chat starved behind retained backlog");
    exchange(left,right);require(a.pending(bob.peerId).empty(),"Old backlog starved while prioritizing new chat");
    for(const auto& id:backlog)require(b.message(id).has_value(),"Backlog message lost");
    std::cout<<"HOST BACKLOG: newest message reached within eight drains; all 80 older rows durably ACKed; four protected message reads per pump\n";
    af.replace(0,4,{});require(!left.sendTransfer(premature.data),"Logged-out endpoint sent file bytes");
    std::cout<<"PASS: 102 real Schannel/shared-runtime relay lifecycles, durable text/ACK/dedupe, bidirectional binary picture/file transfers through existing TransferManager, authenticated winner/capability/logout gates. HOST ONLY\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
