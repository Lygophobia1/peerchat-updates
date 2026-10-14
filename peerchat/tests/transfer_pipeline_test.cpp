#define main legacy_transfer_manager_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "transfer_manager_test.cpp"
#pragma GCC diagnostic pop
#undef main
#include "archive_attachment_sync.hpp"
struct PipelineHarness:Harness {
    std::deque<Packet> held;
    unsigned maxPending=0;bool delay=false;
    void setup(size_t chunk,size_t window,bool supported=true){sender.capability(b,route,supported);receiver.capability(a,route,supported);offer(data(1024*1024+123));sender.configure(id,{chunk,window,0});}
    void step(){
        auto available=[&](const std::string&){return route;};
        sender.capability(b,route,true);receiver.capability(a,route,true);
        sender.tick(now,available,[&](const std::string&,const Blob& f){packets.push_back({true,f});return true;});
        receiver.tick(now,available,[&](const std::string&,const Blob& f){if(delay)held.push_back({false,f});else packets.push_back({false,f});return true;});
        while(!packets.empty()){auto p=std::move(packets.front());packets.pop_front();if(p.toReceiver)receiver.receive(a,p.bytes,now);else sender.receive(b,p.bytes,now);}
        if(!accepted&&!receiver.snapshots().empty()){receiver.accept(id,std::make_unique<MemorySink>(result),now);accepted=true;}
        ++now;
    }
    void end(){delay=false;for(auto& p:held)packets.push_back(p);held.clear();for(unsigned i=0;i<5000&&sender.snapshots()[0].state!=State::Complete;++i)step();check(sender.snapshots()[0].state==State::Complete&&result.committed&&result.bytes==data(1024*1024+123));}
};
int main(){try{
 {protocol::Writer w(protocol::FileTransfer);w.number(2);w.number(3);w.string(std::string(32,'1'));w.number(1);w.number(0);w.bytes(data(65536));check(packetId(w.data)==std::string(32,'1'));rejected([&]{archive::transferPacketId(w.data);});}
 for(auto chunk:{ChunkBytes,ExtendedChunkBytes})for(size_t window:{1,4,8,16}){
    PipelineHarness h;h.setup(chunk,window);while(h.sender.snapshots()[0].done==0)h.step();h.delay=true;
    for(unsigned i=0;i<50;++i){h.step();}
    auto done=h.sender.snapshots()[0].done;auto received=h.result.bytes.size();
    check(received>=done&&received-done<=chunk*window);check(received>done);check(!h.sender.hasTransmissionWork());
    auto ack=h.held.back();h.delay=false;h.sender.receive(h.b,ack.bytes,h.now);auto advanced=h.sender.snapshots()[0].done;
    h.sender.receive(h.b,ack.bytes,h.now);check(h.sender.snapshots()[0].done==advanced);
    // A delayed lower cumulative ACK must not rewind an active sender.
    h.sender.receive(h.b,h.held.front().bytes,h.now);check(h.sender.snapshots()[0].done==advanced);
    h.end();std::cout<<"PASS pipeline chunk="<<chunk<<" window="<<window<<" durable-credit bound/duplicate ACK/integrity\n";
 }
 {PipelineHarness h;h.setup(ExtendedChunkBytes,16,false); // Explicit absence forces the old format.
  h.sender.capability(h.b,h.route,false);h.receiver.capability(h.a,h.route,false);
  while(h.sender.hasPreparationWork(h.now)){h.sender.tick(h.now++,[](const std::string&){return 1;},[](const std::string&,const Blob&){return true;});}
  h.sender.tick(h.now++,[](const std::string&){return 1;},[&](const std::string&,const Blob& f){protocol::Reader r(f);check(r.number()==1);return true;});
  check(h.sender.diagnostics(h.now)[0].find("effective_chunk_bytes=32768 effective_window=1")!=std::string::npos);
 }
 {PipelineHarness h;h.setup(ExtendedChunkBytes,8);while(h.sender.snapshots()[0].done<65536)h.step();h.delay=true;for(unsigned i=0;i<12;++i)h.step();auto stale=h.held.back();h.held.clear();++h.route;h.delay=false;h.step();h.step();auto before=h.sender.snapshots()[0].done;h.sender.receive(h.b,stale.bytes,h.now);check(h.sender.snapshots()[0].done==before);h.end();}
 {PipelineHarness h;h.setup(ExtendedChunkBytes,4);while(!h.accepted)h.step();protocol::Writer w(protocol::FileTransfer);w.number(2);w.number(static_cast<unsigned>(Kind::Chunk));w.string(h.id);w.number(1);w.number(0);w.bytes(data(65536));h.receiver.capability(h.a,h.route,false);rejected([&]{h.receiver.receive(h.a,w.data,h.now);});}
 std::cout<<"PASS negotiated v2, stale negotiation, no-capability rejection and legacy fallback\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
