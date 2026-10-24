#define NOMINMAX
#include "../windows_transfer_io.hpp"
#include <fstream>
#include <iostream>
#include <deque>
using namespace peerchat;
using namespace peerchat::transfer;
void checked(bool b,int line){if(!b)throw std::runtime_error("Restart assertion failed at line "+std::to_string(line));}
#define check(b) checked((b),__LINE__)
template<class F> void rejected(F f){bool bad=false;try{f();}catch(...){bad=true;}check(bad);}
Blob bytes(size_t n){Blob b(n);for(size_t i=0;i<n;++i)b[i]=static_cast<unsigned char>(i*31);return b;}
struct FaultStore:StateStore{
    windows::DurableJournal& inner;bool broken=false;std::uint64_t offset=0;
    explicit FaultStore(windows::DurableJournal& value):inner(value){}
    std::uint64_t wallTime()const override{return inner.wallTime()+offset;}
    std::vector<DurableState> load()override{return inner.load();}
    void save(const std::vector<DurableState>& s)override{if(broken)throw std::runtime_error("disk full");inner.save(s);}
    std::unique_ptr<Source> source(const DurableState& d)override{return inner.source(d);}
    std::unique_ptr<Sink> sink(const DurableState& d)override{return inner.sink(d);}
};
void write(const std::filesystem::path& p,const Blob& b){std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(reinterpret_cast<const char*>(b.data()),b.size());check(bool(f));}
struct Harness {
    std::filesystem::path root,source,destination,pa,pb;
    std::string a=std::string(64,'a'),b=std::string(64,'b'),id=std::string(32,'1');
    std::unique_ptr<windows::DurableJournal> ja,jb;
    std::unique_ptr<TransferManager> sender,receiver;
    std::uint64_t now=1,route=1;bool accepted=false,duplicate=false;
    std::deque<std::pair<bool,Blob>> wire;
    explicit Harness(std::filesystem::path path):root(std::move(path)),source(root/L"résumé-日本.bin"),destination(root/L"received"),pa(root/L"a"),pb(root/L"b"){
        std::filesystem::create_directory(root);std::filesystem::create_directory(destination);std::filesystem::create_directory(pa);std::filesystem::create_directory(pb);open();
    }
    ~Harness(){sender.reset();receiver.reset();ja.reset();jb.reset();}
    void open(){
        ja=std::make_unique<windows::DurableJournal>(pa.wstring(),a);jb=std::make_unique<windows::DurableJournal>(pb.wstring(),b);
        sender=std::make_unique<TransferManager>([]{return std::make_unique<windows::Sha256>();},ja.get());
        receiver=std::make_unique<TransferManager>([]{return std::make_unique<windows::Sha256>();},jb.get());
        sender->restore(now);receiver->restore(now);
    }
    void restart(){sender.reset();receiver.reset();ja.reset();jb.reset();wire.clear();++route;open();}
    void offer(const Blob& data){write(source,data);sender->offer(b,id,std::make_unique<windows::FileSource>(source.wstring()),now);}
    void pump(){
        auto available=[&](const std::string&){return route;};
        sender->tick(now,available,[&](const std::string& p,const Blob& f){check(p==b);wire.emplace_back(true,f);return true;});
        receiver->tick(now,available,[&](const std::string& p,const Blob& f){check(p==a);wire.emplace_back(false,f);return true;});
        while(!wire.empty()){
            auto p=std::move(wire.front());wire.pop_front();
            if(p.first){receiver->receive(a,p.second,now);if(duplicate)receiver->receive(a,p.second,now);}
            else sender->receive(b,p.second,now);
        }
        if(!accepted&&!receiver->snapshots().empty()&&receiver->snapshots()[0].state==State::AwaitingAcceptance){
            receiver->accept(id,std::make_unique<windows::FileSink>(destination.wstring(),receiver->snapshots()[0].metadata,nullptr,true),now);accepted=true;
        }
        now+=10;
    }
    void partial(){for(int n=0;n<1000&&(receiver->snapshots().empty()||receiver->snapshots()[0].done<ChunkBytes);++n)pump();check(receiver->snapshots()[0].done==ChunkBytes);}
    void finish(){for(int n=0;n<2000&&sender->snapshots()[0].state!=State::Complete;++n)pump();check(sender->snapshots()[0].state==State::Complete);check(receiver->snapshots()[0].state==State::Complete);}
    Blob result(){auto path=destination/(unicode::decode(id)+L"-"+source.filename().wstring());std::ifstream f(path,std::ios::binary);return Blob(std::istreambuf_iterator<char>(f),{});}
};
int main(int argc,char** argv){
    if(argc==3&&std::string(argv[1])=="--crash-fixture"){
        Harness h(std::filesystem::path(unicode::decode(argv[2])));h.offer(bytes(100001));h.partial();
        while(h.sender->snapshots()[0].done<ChunkBytes)h.pump();
        check(h.ja->load()[0].done==0&&h.ja->load()[0].highWaterSent==ChunkBytes);
        ExitProcess(0); // Deliberately bypass ALL destructors, like abrupt process death.
    }
    auto root=std::filesystem::temp_directory_path()/("peerchat-restart-"+std::to_string(GetCurrentProcessId()));
    try{
        check(std::filesystem::create_directory(root));
        {
            auto profile=root/L"journal-coalescing-gates";std::filesystem::create_directory(profile);
            windows::DurableJournal journal(profile.wstring(),std::string(64,'a'));
            DurableState row;row.metadata={std::string(32,'d'),"file.bin",65536,{}};row.peer=std::string(64,'b');row.sending=true;row.digestReady=true;row.state=State::Transferring;row.highWaterSent=ChunkBytes;
            journal.save({row});row.done=ChunkBytes;journal.save({row});journal.save({row});
            check(journal.load()[0].done==0); // No new transmission authorization was lost.
            row.highWaterSent=65536;journal.save({row});check(journal.load()[0].done==ChunkBytes);
            row.done=0;journal.save({row});check(journal.load()[0].done==0); // Rewinds are never deferred.
            row.state=State::Cancelled;journal.save({row});check(journal.load()[0].state==State::Cancelled);
            row.sending=false;row.state=State::Transferring;journal.save({row});row.done=ChunkBytes;journal.save({row});check(journal.load()[0].done==ChunkBytes);
            row.done=65536;journal.save({row});row.state=State::Complete;journal.save({row});check(journal.load()[0].state==State::Complete);
            check(journal.diagnostics(row.metadata.id).find("journal_selected_commits=8 ")!=std::string::npos);
        }
        {
            Harness h(root/L"ack-only-crash-window");auto original=bytes(100001);h.offer(original);h.partial();
            while(h.sender->snapshots()[0].done<ChunkBytes)h.pump();
            auto durable=h.ja->load();check(durable.size()==1&&durable[0].done==0&&durable[0].highWaterSent==ChunkBytes);
            check(h.jb->load()[0].done==ChunkBytes); // Receiver ACK is ALWAYS durable.
            h.restart();h.finish();check(h.result()==original);
        }
        {
            auto id=std::string(32,'e');auto path=root/(id+".snippet");std::string text="protected generated message\r\nUnicode \xe6\x97\xa5\xf0\x9f\x99\x82";Blob plain(text.begin(),text.end());
            auto source=windows::ProtectedSnippetSource::create(path.wstring(),plain);check(source->name()==windows::ProtectedSnippetSource::MessageName&&source->size()==plain.size()&&source->read(0,plain.size())==plain);
            DurableState d;d.location=source->resumeToken();d.metadata={id,source->name(),plain.size(),{}};source.reset();
            check(windows::protectedSnippetToken(d.location));source=windows::reopenSource(d);check(source->read(0,plain.size())==plain);source.reset();
            // Previously stored dev.73 tokens retain their name and exact bytes.
            source=std::make_unique<windows::ProtectedSnippetSource>(path.wstring());DurableState legacy;legacy.location=source->resumeToken();source.reset();source=windows::reopenSource(legacy);check(source->name()=="message.txt"&&source->read(0,plain.size())==plain);source.reset();
            std::ifstream input(path,std::ios::binary);Blob sealed(std::istreambuf_iterator<char>(input),{});input.close();check(std::search(sealed.begin(),sealed.end(),plain.begin(),plain.end())==sealed.end());
            auto other=root/(std::string(32,'f')+".snippet");write(other,sealed);rejected([&]{windows::ProtectedSnippetSource wrong(other.wstring());});
            sealed[sealed.size()/2]^=1;write(path,sealed);rejected([&]{windows::reopenSource(d);});
            rejected([&]{windows::ProtectedSnippetSource::create((root/(std::string(32,'f')+".snippet")).wstring(),Blob(windows::ProtectedSnippetSource::MaxBytes+1,42));});
        }
        {
            wchar_t executable[32768]{};check(GetModuleFileNameW(nullptr,executable,32768)!=0);
            auto childRoot=root/L"actual-process-death";
            std::wstring command=L"\""+std::wstring(executable)+L"\" --crash-fixture \""+childRoot.wstring()+L"\"";
            STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION process{};
            check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&process)!=0);
            check(WaitForSingleObject(process.hProcess,15000)==WAIT_OBJECT_0);DWORD code=1;check(GetExitCodeProcess(process.hProcess,&code)&&code==0);
            CloseHandle(process.hThread);CloseHandle(process.hProcess);
            Harness h(childRoot);h.accepted=true;h.finish();check(h.result()==bytes(100001));
        }
        {
            Harness h(root/L"resume");auto data=bytes(100001);h.offer(data);h.partial();h.restart();
            check(h.receiver->snapshots()[0].done==ChunkBytes&&h.receiver->snapshots()[0].state==State::Recovering);
            h.duplicate=true;h.finish();check(h.result()==data);h.restart();check(h.sender->snapshots()[0].state==State::Complete&&h.receiver->snapshots()[0].state==State::Complete);
        }
        {
            Harness h(root/L"source-changed");h.offer(bytes(100001));h.partial();h.sender.reset();h.receiver.reset();
            auto data=bytes(100001);data[40000]^=1;write(h.source,data);h.ja.reset();h.jb.reset();h.open();
            check(h.sender->snapshots()[0].state==State::Failed);check(!std::filesystem::exists(h.destination/(h.id+"-"+unicode::encode(h.source.filename().wstring()))));
        }
        {
            Harness h(root/L"source-hash-changed");h.offer(bytes(100001));h.partial();h.sender.reset();h.receiver.reset();h.ja.reset();h.jb.reset();
            HANDLE file=CreateFileW(h.source.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(file!=INVALID_HANDLE_VALUE);
            FILETIME time{};check(GetFileTime(file,nullptr,nullptr,&time)!=0);DWORD wrote=0;unsigned char bad=99;
            check(WriteFile(file,&bad,1,&wrote,nullptr)&&wrote==1&&SetFileTime(file,nullptr,nullptr,&time));CloseHandle(file);
            h.open();check(h.sender->snapshots()[0].state==State::Recovering);
            protocol::Writer resume(protocol::FileTransfer);resume.number(1);resume.number(static_cast<unsigned>(Kind::Resume));resume.string(h.id);resume.number(ChunkBytes);
            h.sender->receive(h.b,resume.data,h.now);check(h.sender->snapshots()[0].state==State::Recovering);
            for(int n=0;n<20&&h.sender->snapshots()[0].state==State::Recovering;++n)
                h.sender->tick(h.now++,[](const std::string&){return 1;},[](const std::string&,const Blob&){return true;});
            check(h.sender->snapshots()[0].state==State::Failed);
        }
        {
            Harness h(root/L"partial-tail");auto data=bytes(100001);h.offer(data);h.partial();h.sender.reset();h.receiver.reset();h.ja.reset();h.jb.reset();
            // Simulate append/flush before crash, without committed progress/ACK.
            auto path=h.destination/(h.id+".peerchat-part");{std::ofstream f(path,std::ios::binary|std::ios::app);f<<"unacknowledged";}
            h.open();check(std::filesystem::file_size(path)==ChunkBytes);h.finish();check(h.result()==data);
        }
        {
            Harness h(root/L"partial-missing");h.offer(bytes(100001));h.partial();h.sender.reset();h.receiver.reset();h.ja.reset();h.jb.reset();
            std::filesystem::remove(h.destination/(h.id+".peerchat-part"));h.open();check(h.receiver->snapshots()[0].state==State::Failed);
        }
        {
            Harness h(root/L"rename-crash");auto data=bytes(100001);h.offer(data);
            for(int n=0;n<1000&&(h.receiver->snapshots().empty()||h.receiver->snapshots()[0].done!=data.size());++n)h.pump();
            check(h.receiver->snapshots()[0].done==data.size());h.sender.reset();h.receiver.reset();
            auto d=h.jb->load()[0];
            {auto sink=windows::FileSink::reopen(d);sink->commit();} // Crash after atomic rename, before Complete journal.
            h.ja.reset();h.jb.reset();h.open();
            for(int n=0;n<20&&h.receiver->snapshots()[0].state==State::Recovering;++n)
                h.receiver->tick(h.now++,[](const std::string&)->std::uint64_t{return 0;},[](const std::string&,const Blob&){throw std::runtime_error("Offline receiver sent control");return false;});
            check(h.receiver->snapshots()[0].state==State::Complete&&h.result()==data);
            h.finish();check(h.result()==data);
        }
        {
            Harness h(root/L"full-prefix-corrupt");auto data=bytes(100001);h.offer(data);
            for(int n=0;n<1000&&(h.receiver->snapshots().empty()||h.receiver->snapshots()[0].done!=data.size());++n)h.pump();
            check(h.receiver->snapshots()[0].done==data.size());h.sender.reset();h.receiver.reset();h.ja.reset();h.jb.reset();
            data[0]^=1;write(h.destination/(h.id+".peerchat-part"),data);h.open();
            for(int n=0;n<20&&h.receiver->snapshots()[0].state==State::Recovering;++n)
                h.receiver->tick(h.now++,[](const std::string&)->std::uint64_t{return 0;},[](const std::string&,const Blob&){return false;});
            check(h.receiver->snapshots()[0].state==State::Failed);
        }
        {
            Harness h(root/L"tamper");h.offer(bytes(100001));h.partial();h.sender.reset();h.receiver.reset();h.ja.reset();h.jb.reset();
            auto path=h.pb/L"transfer-resume"/L"state.protected";std::ifstream f(path,std::ios::binary);Blob protectedState(std::istreambuf_iterator<char>(f),{});f.close();
            check(!protectedState.empty());protectedState[protectedState.size()/2]^=1;write(path,protectedState);
            rejected([&]{h.open();});check(std::filesystem::exists(h.destination/(h.id+".peerchat-part")));
        }
        {
            Harness h(root/L"cancel");h.offer(bytes(100001));h.partial();h.receiver->cancel(h.id);
            check(!std::filesystem::exists(h.destination/(h.id+".peerchat-part")));h.restart();check(h.receiver->snapshots()[0].state==State::Cancelled);
        }
        {
            Harness h(root/L"wrong-profile");h.offer(bytes(100001));h.partial();h.sender.reset();h.receiver.reset();h.ja.reset();h.jb.reset();
            windows::DurableJournal wrong(h.pb.wstring(),std::string(64,'c'));rejected([&]{wrong.load();});
        }
        {
            Harness h(root/L"expiry");h.offer(bytes(100001));h.partial();h.receiver.reset();
            FaultStore expired(*h.jb);expired.offset=RetentionMilliseconds+1000;
            TransferManager restored([]{return std::make_unique<windows::Sha256>();},&expired);restored.restore(h.now);
            check(restored.snapshots()[0].state==State::Failed&&!std::filesystem::exists(h.destination/(h.id+".peerchat-part")));
        }
        {
            Harness h(root/L"journal-failure");h.offer(bytes(100001));h.partial();h.receiver.reset();
            FaultStore fault(*h.jb);TransferManager restored([]{return std::make_unique<windows::Sha256>();},&fault);restored.restore(h.now);
            fault.broken=true;unsigned sends=0;
            rejected([&]{restored.tick(h.now,[](const std::string&){return 1;},[&](const std::string&,const Blob&){++sends;return true;});});
            check(sends==0&&std::filesystem::exists(h.destination/(h.id+".peerchat-part")));
            fault.broken=false;
            for(int n=0;n<20;++n)restored.tick(h.now+251+n,[](const std::string&){return 1;},[&](const std::string&,const Blob&){++sends;return true;});
            check(sends==0&&restored.snapshots()[0].state==State::Transferring); // Rebind before replying on a new route.
            auto m=restored.snapshots()[0].metadata;protocol::Writer offer(protocol::FileTransfer);offer.number(1);offer.number(static_cast<unsigned>(Kind::Offer));offer.string(m.id);offer.string(m.name);offer.number(m.size);offer.bytes(Blob(m.digest.begin(),m.digest.end()));
            restored.receive(h.a,offer.data,h.now+280);restored.tick(h.now+281,[](const std::string&){return 1;},[&](const std::string&,const Blob&){++sends;return true;});check(sends==1);
        }
        {
            Harness h(root/L"preparation-journal-backoff");write(h.source,bytes(4*1024*1024));
            FaultStore fault(*h.ja);TransferManager manager([]{return std::make_unique<windows::Sha256>();},&fault);
            manager.offer(h.b,h.id,std::make_unique<windows::FileSource>(h.source.wstring()),h.now);
            check(manager.hasPreparationWork(h.now));fault.broken=true;unsigned sends=0;
            rejected([&]{manager.tick(h.now,[](const std::string&){return 1;},[&](const std::string&,const Blob&){++sends;return true;});});
            check(sends==0&&!manager.hasPreparationWork(h.now+249)&&manager.hasPreparationWork(h.now+250));
            fault.broken=false;manager.tick(h.now+250,[](const std::string&){return 1;},[](const std::string&,const Blob&){return true;});
            check(manager.hasPreparationWork(h.now+250));manager.cancel(h.id);check(!manager.hasPreparationWork(h.now+250));
        }
        {
            Harness h(root/L"pipeline-tuning-restart");h.sender->configure("",{65536,8,4000000});h.offer(bytes(1048577));h.sender->configure(h.id,{65536,4,3000000});
            h.sender->capability(h.b,h.route,true);h.receiver->capability(h.a,h.route,true);
            for(int n=0;n<500&&(!h.accepted||h.receiver->snapshots()[0].done<65536);++n)h.pump();
            check(h.receiver->snapshots()[0].done>=65536);h.restart();
            check(h.sender->options().bitsPerSecond==4000000&&h.sender->options(h.id).chunkBytes==65536&&h.sender->options(h.id).window==4&&h.sender->options(h.id).bitsPerSecond==3000000);
            h.sender->capability(h.b,h.route,true);h.receiver->capability(h.a,h.route,true);h.finish();check(h.result()==bytes(1048577));
        }
        {
            Harness h(root/L"default-snapshot-restart");h.sender->configure("",{65536,8,0});h.offer(bytes(100001));h.sender->configure("",{32768,1,1000000});h.restart();
            check(h.sender->options().window==1&&h.sender->options(h.id).window==8&&h.sender->options(h.id).chunkBytes==65536);h.sender->capability(h.b,h.route,true);h.receiver->capability(h.a,h.route,true);h.finish();check(h.result()==bytes(100001));
        }
        std::filesystem::remove_all(root);
        std::cout<<"Durable transfer restart: resumed prefix, duplicate, source change, uncommitted tail, missing partial, rename crash, DPAPI tamper, cancellation, profile/device binding PASS\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";std::filesystem::remove_all(root);return 1;}
}
