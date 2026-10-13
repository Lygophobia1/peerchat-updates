#define NOMINMAX
#include "../storage.cpp"
#include "../windows_transfer_io.hpp"
#include "../portable/transfer_statistics_storage.hpp"
#include <iostream>
using namespace peerchat;using namespace peerchat::transfer;
static void checked(bool value,int line){if(!value)throw std::runtime_error("Transfer statistics regression at line "+std::to_string(line));}
#define check(value) checked((value),__LINE__)
struct File:Source {Blob data=Blob(10*1024*1024,42);std::uint64_t size()const override{return data.size();}std::string name()const override{return "statistics.bin";}Blob read(std::uint64_t at,size_t count)override{return {data.begin()+at,data.begin()+at+count};}};
struct Destination:Sink {Blob bytes;void append(const Blob& b)override{bytes.insert(bytes.end(),b.begin(),b.end());}void commit()override{};};
struct BrokenStatistics:StatisticsStateStore{void load(std::vector<DurableState>&)override{throw std::runtime_error("optional statistics unavailable");}void save(const std::vector<DurableState>&)override{throw std::runtime_error("optional statistics unavailable");}};
int main(){try{
    auto root=std::filesystem::current_path()/"build"/"tests"/Storage::newMessageId();Storage a((root/"a").wstring()),b((root/"b").wstring());auto pa=a.profile(),pb=b.profile();
    // Test-only auditing on disposable databases; never profile initialization.
    auto audit=platform::openWindowsSqlDatabase((root/"a").wstring());
    audit->execute("CREATE TABLE statistics_writes(n INTEGER NOT NULL); INSERT INTO statistics_writes VALUES(0); CREATE TRIGGER statistics_insert AFTER INSERT ON objects BEGIN UPDATE statistics_writes SET n=n+1; END; CREATE TRIGGER statistics_update AFTER UPDATE ON objects BEGIN UPDATE statistics_writes SET n=n+1; END; CREATE TRIGGER message_guard BEFORE INSERT ON messages BEGIN SELECT RAISE(ABORT,'statistics changed messages'); END;");
    auto writes=[&]{auto q=audit->prepare("SELECT n FROM statistics_writes");check(q->row());return q->number(0);};
    ProtectedStatisticsStore sa(a,pa.peerId),sb(b,pb.peerId);BrokenStatistics broken;
    TransferManager sender([]{return std::make_unique<windows::Sha256>();},nullptr,&sa),receiver([]{return std::make_unique<windows::Sha256>();},nullptr,&sb);
    const auto id=Storage::newMessageId();std::uint64_t now=1;sender.offer(pb.peerId,id,std::make_unique<File>(),now);
    check(sender.snapshots()[0].statisticsBytes==0); // Offered/prepared bytes are not throughput.
    bool accepted=false,stalled=false,rewound=false,steady=false;
    for(unsigned n=0;n<4000&&sender.snapshots()[0].state!=State::Complete;++n){
        now+=20;sender.tick(now,[](const auto&){return 1;},[&](const auto&,const Blob& frame){receiver.receive(pa.peerId,frame,now);return true;});
        if(!accepted&&!receiver.snapshots().empty()){receiver.accept(id,std::make_unique<Destination>(),now);accepted=true;}
        receiver.tick(now,[](const auto&){return 1;},[&](const auto&,const Blob& frame){sender.receive(pb.peerId,frame,now);return true;});
        auto s=sender.snapshots(now)[0];check(s.statisticsBytes<=s.metadata.size&&s.statisticsBytes>=s.done);
        steady|=s.etaReliable;
        if(s.state!=State::Complete)check(writes()==0); // No journal in this fixture: all live statistics are memory-only.
        if(!stalled&&s.currentBitsPerSecond>0){now+=1100;sender.tick(now,[](const auto&){return 0;},[](const auto&,const auto&){return false;});auto pause=sender.snapshots(now)[0];check(pause.currentBitsPerSecond==0&&!pause.etaReliable);stalled=true;}
        if(!rewound&&s.done>ChunkBytes){auto before=s.statisticsBytes;protocol::Writer resume(protocol::FileTransfer);resume.number(1);resume.number(static_cast<unsigned>(Kind::Resume));resume.string(id);resume.number(0);sender.receive(pb.peerId,resume.data,now);check(sender.snapshots(now)[0].statisticsBytes==before);rewound=true;}
    }
    auto sent=sender.snapshots(now)[0],received=receiver.snapshots(now)[0];check(sent.state==State::Complete&&received.state==State::Complete&&sent.done==10*1024*1024&&received.done==sent.done);
    check(stalled&&rewound&&steady&&sent.statisticsBytes==sent.done&&received.statisticsBytes==received.done&&sent.durationMilliseconds>0&&sent.currentBitsPerSecond==0);
    auto duration=sent.durationMilliseconds;sender.tick(now+100000,[](const auto&){return 0;},[](const auto&,const auto&){return false;});check(sender.snapshots()[0].durationMilliseconds==duration);
    check(writes()==1);sa.save(sender.retainedRecords());sa.save(sender.retainedRecords());check(writes()==1);
    auto retained=sender.retainedRecords();for(auto& d:retained){d.statisticsKnown=false;d.statisticsBytes=0;d.durationMilliseconds=0;}
    {Storage reopened((root/"a").wstring());ProtectedStatisticsStore restored(reopened,pa.peerId);restored.load(retained);check(retained[0].statisticsKnown&&retained[0].statisticsBytes==sent.metadata.size&&retained[0].durationMilliseconds==duration);}
    check(writes()==1);
    // Missing/foreign/corrupted supplemental summaries never alter core rows.
    {ProtectedStatisticsStore foreign(a,pb.peerId);auto rows=retained;rows[0].statisticsKnown=false;foreign.load(rows);check(!rows[0].statisticsKnown&&rows[0].done==sent.done);}
    audit->execute("UPDATE objects SET b=X'00' WHERE k LIKE 'archive-sync/%'");
    {ProtectedStatisticsStore damaged(a,pa.peerId);auto rows=retained;damaged.load(rows);check(!rows[0].statisticsKnown&&rows[0].state==State::Complete&&rows[0].done==sent.done);}
    TransferManager unaffected([]{return std::make_unique<windows::Sha256>();},nullptr,&broken);unaffected.offer(pb.peerId,Storage::newMessageId(),std::make_unique<File>(),1);unaffected.tick(2,[](const auto&){return 0;},[](const auto&,const auto&){return false;});check(unaffected.snapshots()[0].state==State::Preparing);
    std::cout<<"PASS: confirmed bytes, stalled zero, conservative ETA, retry high-water dedupe, integrity/completion, frozen duration, protected reopen and optional telemetry failure isolation\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
