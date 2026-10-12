#define NOMINMAX
#include "../windows_transfer_io.hpp"
#include "../protected_text_source.hpp"
#include <iostream>
#include <deque>
#include <fstream>

using namespace peerchat;
using namespace peerchat::transfer;
void check(bool b){if(!b)throw std::runtime_error("Transfer assertion failed");}
template<class F> void rejected(F f){bool bad=false;try{f();}catch(...){bad=true;}check(bad);}
struct MemorySource:Source {
    Blob bytes;std::string filename;
    MemorySource(Blob b,std::string name="file.bin"):bytes(std::move(b)),filename(name){}
    std::uint64_t size()const override{return bytes.size();}
    std::string name()const override{return filename;}
    Blob read(std::uint64_t off,size_t limit)override{return Blob(bytes.begin()+off,bytes.begin()+off+std::min<std::uint64_t>(limit,bytes.size()-off));}
};
struct Result {Blob bytes;bool committed=false,destroyed=false;bool diskFailure=false;};
struct MemoryRecord:MemorySource {
    explicit MemoryRecord(Blob bytes):MemorySource(std::move(bytes),ProtectedTextSource::Name){}
    Blob resumeToken()const override{return Blob{1,2,3};}
};
struct MemorySink:Sink {
    Result& result;explicit MemorySink(Result& r):result(r){}
    ~MemorySink(){result.destroyed=true;}
    void append(const Blob& b)override{if(result.diskFailure)throw std::runtime_error("disk full");result.bytes.insert(result.bytes.end(),b.begin(),b.end());}
    void commit()override{result.committed=true;}
};
struct Harness {
    std::string a=std::string(64,'a'),b=std::string(64,'b'),id=std::string(32,'1');
    TransferManager sender{[]{return std::make_unique<windows::Sha256>();}},receiver{[]{return std::make_unique<windows::Sha256>();}};
    struct Packet {bool toReceiver;Blob bytes;};std::deque<Packet> packets;
    std::uint64_t now=1,route=1;
    Result result;bool accepted=false,corrupt=false,duplicate=false;
    void pump(){
        auto available=[&](const std::string&){return route;};
        sender.tick(now,available,[&](const std::string& peer,const Blob& bytes){check(peer==b);packets.push_back({true,bytes});return true;});
        receiver.tick(now,available,[&](const std::string& peer,const Blob& bytes){check(peer==a);packets.push_back({false,bytes});return true;});
        while(!packets.empty()){
            auto p=std::move(packets.front());packets.pop_front();
            if(p.toReceiver){
                protocol::Reader read(p.bytes);read.number();auto kind=read.number();
                if(corrupt&&kind==static_cast<unsigned>(Kind::Chunk)){p.bytes.back()^=1;corrupt=false;}
                try{receiver.receive(a,p.bytes,now);}catch(...){std::cerr<<"receiver rejected kind "<<kind<<" tick "<<now<<"\n";throw;}
                if(duplicate)try{receiver.receive(a,p.bytes,now);}catch(...){std::cerr<<"receiver rejected kind "<<kind<<" tick "<<now<<"\n";throw;}
            }else {try{sender.receive(b,p.bytes,now);}catch(...){protocol::Reader r(p.bytes);r.number();std::cerr<<"sender rejected kind "<<r.number()<<" tick "<<now<<"\n";throw;}}
        }
        if(!accepted&&!receiver.snapshots().empty()&&receiver.snapshots()[0].state==State::AwaitingAcceptance){
            receiver.accept(id,std::make_unique<MemorySink>(result),now);accepted=true;
        }
        now+=10;
    }
    void offer(Blob bytes,std::string name="file.bin"){sender.offer(b,id,std::make_unique<MemorySource>(std::move(bytes),name),now);}
    void finish(){for(int n=0;n<3000&&sender.snapshots()[0].state!=State::Complete&&sender.snapshots()[0].state!=State::Failed&&sender.snapshots()[0].state!=State::Cancelled;++n)pump();}
};
Blob data(size_t n){Blob b(n);for(size_t i=0;i<n;++i)b[i]=static_cast<unsigned char>(i*17);return b;}
int main(){
    try{
        {
            auto protector=std::make_shared<platform::WindowsRecordProtector>();auto id=std::string(32,'e');
            std::string text;for(unsigned n=0;n<3000;++n)text+=u8"Exact multiline 日本語 😀\r\n";
            Blob plain(text.begin(),text.end());auto sealed=ProtectedTextSource::seal(plain,id,*protector);
            check(sealed!=plain&&std::search(sealed.begin(),sealed.end(),plain.begin(),plain.end())==sealed.end());
            ProtectedTextSource source(std::make_unique<MemoryRecord>(sealed),id,protector);
            DurableState record;record.metadata={id,ProtectedTextSource::Name,plain.size(),{}};record.location=source.resumeToken();
            for(unsigned n=0;n<32;++n){
                auto reopened=ProtectedTextSource::reopen(record,protector,[&](const DurableState& row){
                    check(row.metadata.size==sealed.size()&&row.location==Blob({1,2,3}));return std::make_unique<MemoryRecord>(sealed);});
                Blob restored;for(size_t off=0;off<plain.size();off+=ChunkBytes){auto part=reopened->read(off,std::min(ChunkBytes,plain.size()-off));restored.insert(restored.end(),part.begin(),part.end());}
                check(restored==plain);rejected([&]{reopened->read(0,ChunkBytes+1);});
            }
            auto bad=sealed;bad.back()^=1;rejected([&]{ProtectedTextSource wrong(std::make_unique<MemoryRecord>(bad),id,protector);});
            rejected([&]{ProtectedTextSource wrong(std::make_unique<MemoryRecord>(sealed),std::string(32,'f'),protector);});
            rejected([&]{ProtectedTextSource::seal(Blob(ProtectedTextSource::MaxBytes+1,'x'),id,*protector);});
            rejected([&]{ProtectedTextSource::seal(Blob{0xff},id,*protector);});
        }
        {
            TransferManager manager{[]{return std::make_unique<windows::Sha256>();}};
            auto peer=std::string(64,'a'),live=std::string(32,'1'),archive=std::string(32,'2');
            manager.offer(peer,live,std::make_unique<MemorySource>(data(1)),1);
            manager.offer(peer,archive,std::make_unique<MemorySource>(data(1)),1);
            std::vector<std::string> sent;
            // Same peer, distinct purposes: an available DM route must never
            // release an archive offer through a grant-less transport.
            for(unsigned n=0;n<3;++n)manager.tickScoped(n+2,[&](const Snapshot& row){return row.metadata.id==live?101u:0u;},
                [&](const Snapshot& row,const Blob&){sent.push_back(row.metadata.id);return true;});
            check(sent==std::vector<std::string>{live});
            manager.tickScoped(6,[](const Snapshot&){return 102u;},[&](const Snapshot& row,const Blob&){sent.push_back(row.metadata.id);return true;});
            check(sent.size()==3&&std::count(sent.begin(),sent.end(),archive)==1);
        }
        for(size_t size:{size_t(0),size_t(1),size_t(32768),size_t(32769),size_t(2*1024*1024)}){
            Harness h;auto bytes=data(size);h.offer(bytes,u8"résumé-日本.bin");h.duplicate=true;h.finish();
            check(h.sender.snapshots()[0].state==State::Complete&&h.result.committed&&h.result.bytes==bytes);
        }
        {Harness h;auto bytes=data(100000);h.offer(bytes);while(h.result.bytes.size()<32768)h.pump();h.route=0;h.pump();check(!h.result.committed);h.route=2;h.finish();check(h.result.bytes==bytes&&h.result.committed);}
        {Harness h;h.offer(data(50000));h.corrupt=true;h.finish();check(!h.result.committed&&h.receiver.snapshots()[0].state==State::Failed&&h.result.destroyed);}
        {Harness h;auto content=data(50000);windows::Sha256 hash;hash.update(content.data(),content.size()>ChunkBytes?ChunkBytes:content.size());
            hash.update(content.data()+ChunkBytes,content.size()-ChunkBytes);auto expected=hash.finish();
            h.sender.offer(h.b,h.id,std::make_unique<MemorySource>(content),h.now,&expected);h.finish();check(h.result.committed&&h.result.bytes==content);}
        {Harness h;Digest wrong{};h.sender.offer(h.b,h.id,std::make_unique<MemorySource>(data(50000)),h.now,&wrong);
            h.finish();check(h.sender.snapshots()[0].state==State::Failed&&!h.result.committed&&h.receiver.snapshots().empty());}
        {Harness h;h.offer(data(50000));h.result.diskFailure=true;h.finish();check(!h.result.committed&&h.receiver.snapshots()[0].state==State::Failed);}
        {Harness h;h.offer(data(50000));while(!h.accepted)h.pump();h.receiver.cancel(h.id);h.pump();check(h.result.destroyed&&!h.result.committed&&h.sender.snapshots()[0].state==State::Cancelled);}
        for(auto name:{"../bad","C:bad","CON.txt","NUL","LPT1.bin","bad\\name","bad.","bad ","a\nb"})check(!safeName(name));
        {Harness h;h.offer(data(100));while(!h.accepted)h.pump();h.receiver.cancel(h.id);
            protocol::Writer chunk(protocol::FileTransfer);chunk.number(1);chunk.number(static_cast<unsigned>(Kind::Chunk));chunk.string(h.id);chunk.number(0);chunk.bytes(data(10));
            h.receiver.receive(h.a,chunk.data,h.now);check(h.receiver.snapshots()[0].state==State::Cancelled&&!h.result.committed);
            protocol::Writer finish(protocol::FileTransfer);finish.number(1);finish.number(static_cast<unsigned>(Kind::Finish));finish.string(h.id);
            h.receiver.receive(h.a,finish.data,h.now);check(h.receiver.snapshots()[0].state==State::Cancelled);
        }
        {Harness h;h.offer(data(100));h.sender.unsupportedPeer(h.b);check(h.sender.snapshots()[0].state==State::Failed);}
        {Harness h;h.offer(data(1));h.pump();h.now+=IdleMilliseconds+1;h.pump();check(h.sender.snapshots()[0].state==State::Failed);}
        {Harness h;h.offer(data(100));while(!h.accepted)h.pump();auto w=protocol::Writer(static_cast<protocol::Type>(30));w.number(1);w.number(static_cast<unsigned>(Kind::Chunk));w.string(h.id);w.number(0);w.bytes(data(1));rejected([&]{h.receiver.receive(std::string(64,'c'),w.data,h.now);});}
        {Harness h;for(int n=0;n<4;++n)h.sender.offer(h.b,std::string(32,char('1'+n)),std::make_unique<MemorySource>(data(1)),1);rejected([&]{h.sender.offer(h.b,std::string(32,'5'),std::make_unique<MemorySource>(data(1)),1);});}
        // Real Windows source/sink: Unicode, collision safety, hash+atomic
        // completion, rejected preexisting partial, and owned-partial cleanup.
        auto directory=std::filesystem::temp_directory_path()/("peerchat-transfer-"+std::to_string(GetCurrentProcessId()));
        check(std::filesystem::create_directory(directory));
        try{
            Metadata m;m.id=std::string(32,'a');m.name=u8"résumé-日本.bin";auto bytes=data(100000);
            std::wstring target;
            {windows::FileSink sink(directory.wstring(),m);target=sink.path();for(size_t offset=0;offset<bytes.size();offset+=ChunkBytes)sink.append(Blob(bytes.begin()+offset,bytes.begin()+std::min(bytes.size(),offset+ChunkBytes)));sink.commit();}
            {windows::FileSource source(target);check(source.size()==bytes.size());check(source.read(32768,32768)==Blob(bytes.begin()+32768,bytes.begin()+65536));}
            {windows::FileSink sink(directory.wstring(),m);sink.append(data(1));rejected([&]{sink.commit();});} // Must not replace target.
            check(std::filesystem::file_size(target)==bytes.size());
            check(!std::filesystem::exists(directory/(m.id+".peerchat-part")));
            windows::CleanupJournal journal(directory.wstring());
            auto partial=directory/(std::string(32,'c')+".peerchat-part");
            auto createPartial=[&]{
                HANDLE file=CreateFileW(partial.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
                check(file!=INVALID_HANDLE_VALUE);DWORD written=0;check(WriteFile(file,"test",4,&written,nullptr)&&written==4);return file;
            };
            auto handle=createPartial();auto record=journal.record(std::string(32,'c'),partial.wstring(),handle);CloseHandle(handle);
            // A journal is protected local metadata, never a plaintext path.
            {std::ifstream file(std::filesystem::path(record),std::ios::binary);std::string encrypted((std::istreambuf_iterator<char>(file)),{});check(encrypted.find(unicode::encode(partial.wstring()))==std::string::npos);}
            journal.recover();check(!std::filesystem::exists(partial)&&!std::filesystem::exists(record));
            handle=createPartial();record=journal.record(std::string(32,'c'),partial.wstring(),handle);CloseHandle(handle);
            Blob protectedRecord;
            {std::ifstream file(std::filesystem::path(record),std::ios::binary);protectedRecord.assign(std::istreambuf_iterator<char>(file),{});}
            {auto tampered=protectedRecord;tampered[tampered.size()/2]^=1;std::ofstream file(std::filesystem::path(record),std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<char*>(tampered.data()),tampered.size());}
            rejected([&]{journal.recover();});check(std::filesystem::exists(partial));
            {std::ofstream file(std::filesystem::path(record),std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<char*>(protectedRecord.data()),protectedRecord.size());}
            journal.recover();check(!std::filesystem::exists(partial));
            handle=createPartial();record=journal.record(std::string(32,'c'),partial.wstring(),handle);CloseHandle(handle);
            // Replacement under the same pathname must not be deleted.
            auto original=partial;original+=L".original";check(MoveFileW(partial.c_str(),original.c_str())!=0);
            handle=createPartial();CloseHandle(handle);rejected([&]{journal.recover();});check(std::filesystem::exists(partial)&&std::filesystem::exists(original));
            m.id=std::string(32,'b');
            {windows::FileSink sink(directory.wstring(),m);rejected([&]{windows::FileSink duplicate(directory.wstring(),m);});sink.append(data(3));}
            check(!std::filesystem::exists(directory/(m.id+".peerchat-part")));
        }catch(...){std::filesystem::remove_all(directory);throw;}
        std::filesystem::remove_all(directory);
        std::cout<<"Transfer manager: binary/Unicode/empty/bounded chunks/duplicate/reconnect/hash tamper/disk failure/cancel/expiry/peer binding/limits/atomic no-overwrite PASS\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
