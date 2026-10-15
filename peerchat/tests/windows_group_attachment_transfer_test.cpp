#define main legacy_transfer_manager_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "transfer_manager_test.cpp"
#pragma GCC diagnostic pop
#undef main
#include "windows_group_attachment_transfer.hpp"
namespace ga=peerchat::group_attachments;
namespace cache=peerchat::group_attachments::windows;
namespace io=peerchat::transfer::windows;
#define demand(value) do{if(!(value))throw std::runtime_error("Cache resume assertion at line "+std::to_string(__LINE__));}while(false)
const std::string sourceDevice(64,'a'),recipientDevice(64,'b'),server(64,'c'),object(32,'d');
constexpr size_t Length=131073;
Blob manifestBytes(const ga::Manifest& manifest){protocol::Writer writer(protocol::GroupEvent);ga::writePrivateManifest(writer,manifest);return std::move(writer.data);}
ga::Manifest manifestFrom(const Blob& bytes){protocol::Reader reader(bytes);auto manifest=ga::readPrivateManifest(reader);reader.end();return manifest;}
Metadata metadata(const ga::Manifest& m){return {m.binding.attachment,"ciphertext.peerchat",m.cipherSize(),m.cipherDigest};}
struct Pair {
 std::filesystem::path root;io::DurableJournal txLegacy,rxLegacy;cache::CacheStateStore txStore,rxStore;TransferManager tx,rx;std::uint64_t now=1;std::deque<std::pair<bool,Blob>> queue;
 explicit Pair(const std::filesystem::path& folder):root(folder),txLegacy((root/L"tx").wstring(),sourceDevice),rxLegacy((root/L"rx").wstring(),recipientDevice),txStore((root/L"tx").wstring(),sourceDevice,server,txLegacy),rxStore((root/L"rx").wstring(),recipientDevice,server,rxLegacy),tx([]{return std::make_unique<io::Sha256>();},&txStore),rx([]{return std::make_unique<io::Sha256>();},&rxStore){tx.restore(now);rx.restore(now);}
 void pump(const ga::Manifest& m){auto route=[](const Snapshot& row){demand(row.purpose==Purpose::GroupCacheCiphertext);return 5;};
  tx.tickScoped(++now,route,[&](const Snapshot&,const Blob& bytes){queue.emplace_back(true,bytes);return true;});rx.tickScoped(now,route,[&](const Snapshot&,const Blob& bytes){queue.emplace_back(false,bytes);return true;});
  while(!queue.empty()){auto frame=std::move(queue.front());queue.pop_front();if(frame.first)rx.receiveCacheCiphertext(sourceDevice,metadata(m),m.size,frame.second,now);else tx.receiveCacheCiphertext(recipientDevice,metadata(m),m.size,frame.second,now);}
  if(!rx.snapshots().empty()&&rx.snapshots()[0].state==State::AwaitingAcceptance)rx.accept(object,std::make_unique<io::FileSink>((root/L"received").wstring(),metadata(m),nullptr,true),now);
 }
 void finish(const ga::Manifest& m){for(unsigned n=0;n<4000&&(tx.snapshots()[0].state!=State::Complete||rx.snapshots().empty()||rx.snapshots()[0].state!=State::Complete);++n)pump(m);demand(tx.snapshots()[0].state==State::Complete&&rx.snapshots()[0].state==State::Complete);}
};
void seed(const std::filesystem::path& root,const std::string& mode){
 for(const auto& directory:{L"tx",L"rx",L"cipher",L"received",L"output"})std::filesystem::create_directories(root/directory);
 MemorySource input(data(Length),u8"原本-📷.bin");io::FileSink cipher((root/L"cipher").wstring(),{object,"ciphertext.peerchat",0,{}});io::Sha256 ph,ch;auto manifest=ga::seal(input,cipher,{object,std::string(32,'e'),sourceDevice,4,7},ph,ch);
 Pair pair(root);pair.rxStore.saveAppState({},manifestBytes(manifest));pair.tx.offerCacheCiphertext(recipientDevice,metadata(manifest),manifest.size,std::make_unique<io::FileSource>(cipher.path(),"ciphertext.peerchat",Purpose::GroupCacheCiphertext),1);
 if(mode=="download"){for(unsigned n=0;n<1000;++n){pair.pump(manifest);if(!pair.rx.snapshots().empty()&&pair.rx.snapshots()[0].done>=ChunkBytes)ExitProcess(0);}throw std::runtime_error("No durable download prefix");}
 pair.finish(manifest);
 if(mode=="cipher-corrupt"||mode=="intent-orphan"){
  if(mode=="intent-orphan"){io::FileSink orphan((root/L"output").wstring(),{object,manifest.name,manifest.size,manifest.plainDigest},&pair.rxStore.outputCleanup(),true);orphan.append(Blob{1});orphan.flush();}
  ExitProcess(0);
 }
 cache::ReceiverFinalizer finish(pair.rxStore,pair.rx.completedRecords()[0],manifest,(root/L"output").wstring());
 if(mode=="plaintext-partial"){demand(!finish.step());demand(!finish.step());ExitProcess(0);}
 if(mode=="plaintext-full"){while(finish.processedBytes()<Length)demand(!finish.step());ExitProcess(0);}
 while(!finish.step()){}
 demand(finish.verified());ExitProcess(0);
}
void child(const std::filesystem::path& root,const std::string& mode){wchar_t executable[32768]{};demand(GetModuleFileNameW(nullptr,executable,32768)>0);auto command=L"\""+std::wstring(executable)+L"\" \""+root.wstring()+L"\" "+unicode::decode(mode);STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION process{};demand(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&process));demand(WaitForSingleObject(process.hProcess,30000)==WAIT_OBJECT_0);DWORD exit=1;demand(GetExitCodeProcess(process.hProcess,&exit)&&exit==0);CloseHandle(process.hThread);CloseHandle(process.hProcess);}
void corrupt(const std::filesystem::path& file,size_t offset){std::fstream stream(file,std::ios::binary|std::ios::in|std::ios::out);stream.seekg(offset);auto value=stream.get();demand(value!=std::char_traits<char>::eof());stream.seekp(offset);stream.put(static_cast<char>(value^1));demand(bool(stream));}
int main(int argc,char** argv){try{
 demand(argc>=2);auto root=std::filesystem::absolute(argv[1]);if(argc==3){seed(root,argv[2]);return 1;}std::filesystem::create_directories(root);
 for(const auto& mode:{"download","plaintext-partial","plaintext-full","renamed","cipher-corrupt","plaintext-corrupt","intent-orphan"}){
  auto fixture=root/mode;child(fixture,mode);Pair pair(fixture);auto app=pair.rxStore.appState();auto manifest=manifestFrom(app);demand(pair.txLegacy.load().empty()&&pair.rxLegacy.load().empty());
  rejected([&]{pair.rxStore.saveAppState(Blob{1},app);});demand(pair.rxStore.appState()==app);rejected([&]{pair.rxStore.saveAppState(app,Blob(cache::CacheStateStore::MaxAppState+1));});
  if(std::string(mode)=="download"){demand(pair.rx.snapshots()[0].done>=ChunkBytes&&pair.rx.snapshots()[0].done<manifest.cipherSize());pair.finish(manifest);}
  auto received=fixture/L"received"/(object+"-ciphertext.peerchat");auto output=fixture/L"output"/unicode::decode(object+"-"+manifest.name);
  if(std::string(mode)=="cipher-corrupt")corrupt(received,ga::HeaderBytes+9);
  if(std::string(mode)=="plaintext-corrupt")corrupt(output,15);
  cache::ReceiverFinalizer finish(pair.rxStore,pair.rx.completedRecords()[0],manifest,(fixture/L"output").wstring());rejected([&]{finish.path();});rejected([&]{finish.token();});
  if(std::string(mode)=="cipher-corrupt"||std::string(mode)=="plaintext-corrupt"){rejected([&]{while(!finish.step()){};});demand(finish.failed()&&!finish.verified()&&!finish.error().empty());if(std::string(mode)=="cipher-corrupt")demand(!std::filesystem::exists(output));continue;}
  unsigned steps=0;std::uint64_t prior=0;while(!finish.step()){demand(finish.processedBytes()>=prior&&finish.processedBytes()-prior<=ga::BlockBytes);prior=finish.processedBytes();demand(++steps<100);}
  demand(finish.verified()&&finish.path()==output.wstring()&&finish.processedBytes()==Length&&pair.rxStore.outputToken(manifest)==finish.token());
  // Verification exposes a path only after commit, and an existing output is
  // checked again after fresh-process reopen above; no replacement is used.
  std::ifstream file(output,std::ios::binary);Blob actual((std::istreambuf_iterator<char>(file)),{});demand(actual==data(Length));
  rejected([&]{pair.rxStore.forgetOutput(manifest,Blob{1});});pair.rxStore.forgetOutput(manifest,finish.token());demand(pair.rxStore.outputToken(manifest).empty()&&std::filesystem::exists(output));
  std::cout<<"PASS fresh-process "<<mode<<" bounded finalization, exact Unicode output\n";
 }
 // Supplemental snapshot authenticity and ownership are independent of the
 // legacy journal; corruption must never cause a replacement empty snapshot.
 auto profiles=root/L"binding";std::filesystem::create_directories(profiles);Blob saved;
 {io::DurableJournal legacy(profiles.wstring(),recipientDevice);cache::CacheStateStore store(profiles.wstring(),recipientDevice,server,legacy);demand(store.load().empty());store.saveAppState({},Blob{3,4,5});rejected([&]{cache::CacheStateStore second(profiles.wstring(),recipientDevice,server,legacy);});saved=store.appState();}
 {io::DurableJournal legacy(profiles.wstring(),recipientDevice);cache::CacheStateStore wrong(profiles.wstring(),recipientDevice,std::string(64,'f'),legacy);rejected([&]{wrong.load();});rejected([&]{wrong.saveAppState({},{});});}
 auto state=profiles/L"group-attachment-transfers"/L"state.protected";corrupt(state,24);
 {io::DurableJournal legacy(profiles.wstring(),recipientDevice);cache::CacheStateStore store(profiles.wstring(),recipientDevice,server,legacy);rejected([&]{store.load();});rejected([&]{store.save({});});}
 auto mixed=root/L"mixed";std::filesystem::create_directories(mixed);
 {io::DurableJournal legacy(mixed.wstring(),recipientDevice);cache::CacheStateStore store(mixed.wstring(),recipientDevice,legacy);demand(store.load().empty());DurableState ordinary;ordinary.metadata={std::string(32,'1'),"ordinary.bin",0,{}};ordinary.peer=sourceDevice;ordinary.state=State::Complete;ordinary.created=ordinary.updated=store.wallTime();ordinary.expires=ordinary.created+RetentionMilliseconds;
  auto encrypted=ordinary;encrypted.metadata={object,"ciphertext.peerchat",37,{}};encrypted.purpose=Purpose::GroupCacheCiphertext;encrypted.state=State::AwaitingAcceptance;encrypted.digestReady=true;store.save({ordinary,encrypted});auto old=legacy.load();demand(old.size()==1&&old[0].metadata.name=="ordinary.bin"&&old[0].purpose==Purpose::PeerFile);
  auto changed=encrypted;changed.metadata.digest[0]=1;rejected([&]{store.save({ordinary,changed});});changed=encrypted;changed.purpose=Purpose::PeerFile;rejected([&]{store.save({ordinary,changed});});
  // A locked scratch file models local persistence failure. The previously
  // durable cache cursor and app state remain readable after reopening.
  auto next=mixed/L"group-attachment-transfers"/L"state.next";HANDLE lock=CreateFileW(next.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);demand(lock!=INVALID_HANDLE_VALUE);rejected([&]{store.saveAppState({},Blob{7});});CloseHandle(lock);demand(store.appState().empty());store.saveAppState({},Blob(cache::CacheStateStore::MaxAppState,9));
 }
 {io::DurableJournal legacy(mixed.wstring(),recipientDevice);cache::CacheStateStore store(mixed.wstring(),recipientDevice,legacy);auto rows=store.load();demand(rows.size()==2&&store.appState()==Blob(cache::CacheStateStore::MaxAppState,9));demand(legacy.load().size()==1);}
 auto senderPreview=root/L"sender-preview";std::filesystem::create_directories(senderPreview/L"cipher");std::filesystem::create_directory(senderPreview/L"plain");
 {io::DurableJournal legacy(senderPreview.wstring(),sourceDevice);cache::CacheStateStore store(senderPreview.wstring(),sourceDevice,legacy);demand(store.load().empty());MemorySource input(data(Length),u8"sender-📷.bin");io::FileSink cipher((senderPreview/L"cipher").wstring(),{object,"ciphertext.peerchat",0,{}});io::Sha256 ph,ch;ga::Sealer sealer(input,cipher,{object,std::string(32,'e'),sourceDevice,4,7},ph,ch);while(!sealer.step()){}auto manifest=sealer.takeManifest();auto now=store.wallTime();ga::Staged staged{std::move(manifest),cache::StagedCipherSource::location(cipher),now,now+RetentionMilliseconds};cipher.commit();cache::ReceiverFinalizer preview(store,cache::StagedCipherSource::reopen(staged,sourceDevice),staged.manifest,(senderPreview/L"plain").wstring());while(!preview.step()){}demand(preview.verified());std::ifstream read(std::filesystem::path(preview.path()),std::ios::binary);Blob plain((std::istreambuf_iterator<char>(read)),{});demand(plain==data(Length));}
 std::cout<<"PASS supplemental purpose isolation, CAS, bounds, exclusive owner, wrong-server/corruption rejection; shipping cache gate unchanged\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
