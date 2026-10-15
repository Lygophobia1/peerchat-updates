#include "storage.cpp"
#undef NOMINMAX
#define main legacy_transfer_manager_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "transfer_manager_test.cpp"
#pragma GCC diagnostic pop
#undef main
#include "windows_group_attachment_source.hpp"
using namespace peerchat::group_attachments;
using CipherSource=peerchat::group_attachments::windows::StagedCipherSource;
namespace io=peerchat::transfer::windows;
void seed(const std::filesystem::path& root,const std::string& mode){
 std::filesystem::create_directories(root);Storage storage(root.wstring());auto profile=storage.profile();StagingJournal journal(storage,profile.peerId);check(journal.load().empty());
 std::filesystem::create_directory(root/L"ciphertext");io::CleanupJournal cleanup(root.wstring());
 auto id=std::string(32,'a');Metadata metadata{id,"ciphertext.peerchat",0,{}};io::FileSink sink((root/L"ciphertext").wstring(),metadata,&cleanup,true);
 MemorySource source(data(1048577),u8"original-📷-日本.png");io::Sha256 ph,ch;Sealer preparation(source,sink,{id,std::string(32,'b'),profile.peerId,4,7},ph,ch);
 if(mode=="orphan"){preparation.step();preparation.step();sink.flush();ExitProcess(0);}
 while(!preparation.step()){}
 auto manifest=preparation.takeManifest();std::vector<Staged> records;
 records.push_back({std::move(manifest),CipherSource::location(sink),1700000000000ull,1700000000000ull+transfer::RetentionMilliseconds});journal.save(records);
 if(mode=="renamed")sink.commit();
 // Exit without sink/codec cleanup at either side of the rename boundary.
 ExitProcess(0);
}
void child(const std::filesystem::path& root,const std::string& mode){
 wchar_t executable[32768]{};check(GetModuleFileNameW(nullptr,executable,32768)>0);
 auto command=L"\""+std::wstring(executable)+L"\" \""+root.wstring()+L"\" "+unicode::decode(mode);
 STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION process{};
 check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&process));
 check(WaitForSingleObject(process.hProcess,30000)==WAIT_OBJECT_0);DWORD exit=1;check(GetExitCodeProcess(process.hProcess,&exit)&&exit==0);CloseHandle(process.hThread);CloseHandle(process.hProcess);
}
int main(int argc,char** argv){try{
 auto root=std::filesystem::absolute(std::filesystem::path(argc>1?argv[1]:"build/tests/source-recovery"));
 if(argc==3){seed(root,argv[2]);return 1;}
 std::filesystem::create_directories(root);
 for(const auto& mode:{"partial","renamed","orphan","substitution","oversize","header","payload"}){
  auto fixture=root/mode;child(fixture,mode);Storage storage(fixture.wstring());auto device=storage.profile().peerId;StagingJournal journal(storage,device);auto records=journal.load();
  io::CleanupJournal cleanup(fixture.wstring());auto partial=fixture/L"ciphertext"/(std::string(32,'a')+".peerchat-part");auto final=fixture/L"ciphertext"/(std::string(32,'a')+"-ciphertext.peerchat");
  if(std::string(mode)=="orphan"){check(records.empty()&&std::filesystem::exists(partial));cleanup.recover();check(!std::filesystem::exists(partial)&&!std::filesystem::exists(final));continue;}
  check(records.size()==1);auto& staged=records[0];cleanup.recover({staged.manifest.binding.attachment});
  auto recordFile=fixture/L"transfer-cleanup"/(std::string(32,'a')+".cleanup");check(std::filesystem::exists(recordFile));
  if(std::string(mode)=="substitution"){
   auto original=fixture/L"ciphertext"/L"original-preserved.fixture";std::filesystem::rename(partial,original);std::filesystem::copy_file(original,partial);
   auto size=std::filesystem::file_size(partial);rejected([&]{CipherSource::reopen(staged,device);});check(std::filesystem::exists(original)&&std::filesystem::file_size(partial)==size&&!std::filesystem::exists(final));continue;
  }
  if(std::string(mode)=="oversize"){
   {std::ofstream file(partial,std::ios::binary|std::ios::app);file.put('x');check(bool(file));}auto size=std::filesystem::file_size(partial);
   rejected([&]{CipherSource::reopen(staged,device);});check(std::filesystem::file_size(partial)==size&&!std::filesystem::exists(final));continue;
  }
  if(std::string(mode)=="header"){
   {std::fstream file(partial,std::ios::binary|std::ios::in|std::ios::out);file.put('x');check(bool(file));}
   rejected([&]{CipherSource::reopen(staged,device);});check(std::filesystem::exists(partial)&&!std::filesystem::exists(final));continue;
  }
  if(std::string(mode)=="payload"){
   {std::fstream file(partial,std::ios::binary|std::ios::in|std::ios::out);file.seekg(HeaderBytes+33);auto prior=file.get();check(prior!=std::char_traits<char>::eof());file.seekp(HeaderBytes+33);file.put(static_cast<char>(prior^1));check(bool(file));}
   auto source=CipherSource::reopen(staged,device);Result result;MemorySink output(result);io::Sha256 ph,ch;
   rejected([&]{group_attachments::open(*source,output,staged.manifest,ph,ch);});check(!result.committed&&result.bytes.empty()&&journal.load().size()==1);continue;
  }
  rejected([&]{CipherSource::reopen(staged,std::string(64,'0'));});auto source=CipherSource::reopen(staged,device);
  check(source->resumeToken()==staged.location&&source->name()=="ciphertext.peerchat"&&!std::filesystem::exists(partial)&&std::filesystem::exists(final));cleanup.recover();check(!std::filesystem::exists(recordFile));
  Result result;MemorySink output(result);io::Sha256 ph,ch;group_attachments::open(*source,output,staged.manifest,ph,ch);check(result.committed&&result.bytes==data(1048577));source.reset();
  auto again=CipherSource::reopen(staged,device);check(again->resumeToken()==staged.location);check(journal.load().size()==1);
 }
 std::cout<<"PASS real process death before/after ciphertext rename, protected key/token recovery, exact decrypt, orphan cleanup, wrong device/file substitution/length/header rejection without deleting retained files; no enabled upload controller\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
