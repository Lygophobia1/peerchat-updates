#define main legacy_transfer_restart_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "transfer_restart_test.cpp"
#pragma GCC diagnostic pop
#undef main

static Blob resume(const std::string& id,std::uint64_t negotiation,std::uint64_t offset){
    protocol::Writer w(protocol::FileTransfer);w.number(2);w.number(static_cast<unsigned>(Kind::Resume));w.string(id);w.number(negotiation);w.number(offset);return w.data;
}
static void step(Harness& h){h.sender->capability(h.b,h.route,true);h.receiver->capability(h.a,h.route,true);h.pump();}
static void finish(Harness& h){for(unsigned i=0;i<2000&&h.sender->snapshots()[0].state!=State::Complete;++i)step(h);check(h.sender->snapshots()[0].state==State::Complete&&h.receiver->snapshots()[0].state==State::Complete);}
template<class F>static void stage(const char* expected,F call){bool failed=false;try{call();}catch(const ReceiveFailure& e){failed=true;check(std::string(e.stage)==expected&&std::string(e.what())=="Transfer receive rejected");}check(failed);}

int main(){try{
    auto root=std::filesystem::current_path()/L"build"/L"tests"/L"transfer-control-replay"/std::to_wstring(GetCurrentProcessId());std::filesystem::create_directories(root);
    for(bool originalV2:{false,true}){
        Harness h(root/(originalV2?L"awaiting-accept-v2-restart":L"awaiting-accept-v1-v2-upgrade"));h.sender->configure("",{ExtendedChunkBytes,16,0});h.offer(bytes(17785));
        Blob offer;h.sender->capability(h.b,h.route,originalV2);h.receiver->capability(h.a,h.route,originalV2);
        for(unsigned i=0;i<20&&offer.empty();++i)h.sender->tick(h.now++, [&](const std::string&){return h.route;},[&](const std::string&,const Blob& f){offer=f;return true;});
        check(!offer.empty());h.receiver->receive(h.a,offer,h.now);check(h.receiver->snapshots()[0].state==State::AwaitingAcceptance);
        h.restart();h.sender->capability(h.b,h.route,true);h.receiver->capability(h.a,h.route,true);
        h.receiver->accept(h.id,std::make_unique<windows::FileSink>(h.destination.wstring(),h.receiver->snapshots()[0].metadata,nullptr,true),h.now);h.accepted=true;
        Blob freshOffer;for(unsigned i=0;i<20&&freshOffer.empty();++i)h.sender->tick(h.now++, [&](const std::string&){return h.route;},[&](const std::string&,const Blob& f){freshOffer=f;return true;});
        check(!freshOffer.empty());Blob premature;
        h.receiver->tick(h.now++, [&](const std::string&){return h.route;},[&](const std::string&,const Blob& f){premature=f;return true;});
        if(!premature.empty()){
            try{h.sender->receive(h.b,premature,h.now);}catch(const ReceiveFailure& e){std::cerr<<"REPRO: restored receiver acceptance before fresh Offer emitted stale control; sender rejects stage="<<e.stage<<'\n';}
            throw std::runtime_error("Restored receiver sent control before current Offer");
        }
        h.receiver->receive(h.a,freshOffer,h.now);finish(h);check(h.result()==bytes(17785));
        std::cout<<"PASS: restored unaccepted "<<(originalV2?"v2":"v1-to-v2")<<" offer accepted before fresh Offer; no stale control leaks, current negotiation completes\n";
    }
    {
        Harness h(root/L"small-full-prefix-before-ack");const auto data=bytes(20001);h.sender->configure("",{ExtendedChunkBytes,16,0});h.receiver->configure("",{ExtendedChunkBytes,16,0});h.offer(data);
        for(unsigned i=0;i<1000&&(h.receiver->snapshots().empty()||h.receiver->snapshots()[0].done!=data.size());++i)step(h);
        check(h.receiver->snapshots()[0].done==data.size()&&h.sender->snapshots()[0].done==0);
        check(h.ja->load()[0].highWaterSent==data.size());h.restart();h.duplicate=true;finish(h);check(h.result()==data);
        check(h.sender->diagnostics(h.now)[0].find("protocol_version=2 effective_chunk_bytes=65536 effective_window=16")!=std::string::npos);
        std::cout<<"PASS: small v2 file, receiver full durable prefix before first sender ACK, both journal restores, rehash/reoffer/full-offset Resume, duplicate frames and completion\n";
    }
    {
        Harness h(root/L"resume-guard-stages");h.sender->configure("",{ExtendedChunkBytes,16,0});h.offer(bytes(17785));
        for(unsigned i=0;i<1000&&!h.accepted;++i)step(h);
        check(h.sender->snapshots()[0].done==0&&h.ja->load()[0].highWaterSent==0);
        stage("transfer_resume_high_water",[&]{h.sender->receive(h.b,resume(h.id,1,1),h.now);});
        stage("transfer_resume_size",[&]{h.sender->receive(h.b,resume(h.id,1,17786),h.now);});
        stage("transfer_control_identity",[&]{h.sender->receive(h.b,resume(std::string(32,'e'),1,0),h.now);});
        stage("transfer_resume_direction",[&]{h.receiver->receive(h.a,resume(h.id,1,0),h.now);});
        h.sender->capability(h.b,h.route,false);stage("transfer_v2_capability",[&]{h.sender->receive(h.b,resume(h.id,1,0),h.now);});h.sender->capability(h.b,h.route,true);
        finish(h);check(h.result()==bytes(17785));
        std::cout<<"PASS: exact fixed Resume high-water/size/direction/identity/capability stages; invalid controls cannot advance durable progress\n";
    }
    {
        Harness h(root/L"complete-receiver-lost-completion-ack");h.sender->configure("",{ExtendedChunkBytes,16,0});h.offer(bytes(16269));
        for(unsigned i=0;i<1000&&(h.receiver->snapshots().empty()||h.receiver->snapshots()[0].state!=State::Complete);++i)step(h);
        check(h.receiver->snapshots()[0].state==State::Complete&&h.sender->snapshots()[0].state!=State::Complete);
        h.restart();check(h.receiver->snapshots()[0].state==State::Complete);finish(h);check(h.result()==bytes(16269));
        std::cout<<"PASS: persisted completed receiver and lost completion ACK; restored sender reoffers, receives current full-prefix Resume, re-Finish and completion\n";
    }
    {
        Harness h(root/L"fresh-route-stale-resume");h.sender->configure("",{ExtendedChunkBytes,16,0});h.offer(bytes(16269));
        for(unsigned i=0;i<1000&&!h.accepted;++i)step(h);
        ++h.route;h.sender->capability(h.b,h.route,true);h.receiver->capability(h.a,h.route,true);
        // A new Offer must establish a new negotiation before a delayed old
        // Resume can cross this authenticated replacement route.
        h.sender->tick(h.now++, [&](const std::string&){return h.route;},[&](const std::string&,const Blob& f){h.receiver->receive(h.a,f,h.now);return true;});
        h.sender->receive(h.b,resume(h.id,1,0),h.now);check(h.sender->snapshots()[0].done==0);
        finish(h);check(h.result()==bytes(16269));
        std::cout<<"PASS: stale Resume across replacement route ignored; current negotiation resumes and completes exact small file\n";
    }
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
