#include "storage.cpp"
#undef NOMINMAX
#define main legacy_transfer_manager_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "transfer_manager_test.cpp"
#pragma GCC diagnostic pop
#undef main
#include "group_attachment_staging.hpp"

using namespace peerchat::group_attachments;
void stage(const std::filesystem::path& root){
 Storage storage(root.wstring());auto profile=storage.profile();StagingJournal journal(storage,profile.peerId);check(journal.load().empty());
 std::filesystem::create_directory(root/L"ciphertext");MemorySource source(data(1048577),u8"private-📷-日本.png");
 auto id=Storage::newMessageId();Metadata metadata;metadata.id=id;metadata.name="encrypted.bin";
 windows::FileSink target((root/L"ciphertext").wstring(),metadata,nullptr,true);windows::Sha256 plain,cipher;
 auto manifest=seal(source,target,{id,std::string(32,'2'),profile.peerId,4,7},plain,cipher);
 windows::FileSource saved(target.path(),"ciphertext.peerchat",Purpose::GroupCacheCiphertext);Staged value{std::move(manifest),saved.resumeToken(),1700000000000ull,1700000000000ull+transfer::RetentionMilliseconds};
 std::vector<Staged> records;records.push_back(std::move(value));journal.save(records);
 // Legacy transfer data is separate and remains readable by old clients.
 protocol::Writer old(protocol::FileTransfer);old.string("PeerChat/ProtectedTransferJournal/v1");old.string(profile.peerId);old.number(transfer::ChunkBytes);old.number(0);
 storage.saveTransferJournal(profile.peerId,{},old.data);
}
void verify(const std::filesystem::path& root){
 Storage storage(root.wstring());auto profile=storage.profile();auto legacy=storage.transferJournal(profile.peerId);
 StagingJournal journal(storage,profile.peerId),stale(storage,profile.peerId);auto records=journal.load();auto staleRecords=stale.load();check(records.size()==1&&staleRecords.size()==1);
 auto& record=records[0];check(record.manifest.name==u8"private-📷-日本.png"&&record.manifest.size==1048577&&record.manifest.binding.account==4&&record.manifest.binding.epoch==7);
 DurableState sourceState;sourceState.purpose=Purpose::GroupCacheCiphertext;sourceState.location=record.location;auto source=windows::FileSource::reopen(sourceState);check(source->name()=="ciphertext.peerchat");
 Result result;MemorySink output(result);windows::Sha256 ph,ch;open(*source,output,record.manifest,ph,ch);check(result.committed&&result.bytes==data(1048577));
 auto previous=storage.groupAttachmentStaging(profile.peerId);journal.save(records);check(storage.groupAttachmentStaging(profile.peerId)==previous);
 record.expires--;rejected([&]{journal.save(records);});record.expires++;
 std::vector<Staged> empty;journal.save(empty);rejected([&]{stale.save(staleRecords);});journal.save(records);
 record.manifest.binding.sender=std::string(64,'a');rejected([&]{journal.save(records);});record.manifest.binding.sender=profile.peerId;
 auto key=record.manifest.key;record.manifest.key[0]^=1;Result bad;MemorySink partial(bad);windows::Sha256 p2,c2;rejected([&]{open(*source,partial,record.manifest,p2,c2);});check(!bad.committed);record.manifest.key[0]^=1;
 check(storage.transferJournal(profile.peerId)==legacy);
 auto alternate=std::string(64,profile.peerId[0]=='b'?'c':'b');storage.saveGroupAttachmentStaging(alternate,{},previous);StagingJournal wrong(storage,alternate);rejected([&]{wrong.load();});
 // Protected SQLite values never store the key, filename or opaque token in clear.
 using namespace peerchat::storage_detail;Sqlite sql;sqlite3* raw{};check(sql.open16((root/L"peerchat.sqlite").c_str(),&raw)==0);
 {WindowsStatement q(sql,raw,"SELECT b FROM objects WHERE k=?");q.text(1,"group-attachment-staging/v1/"+profile.peerId);check(q.row());auto sealed=q.bytes(0);auto name=Blob(record.manifest.name.begin(),record.manifest.name.end());
  check(std::search(sealed.begin(),sealed.end(),key.begin(),key.end())==sealed.end()&&std::search(sealed.begin(),sealed.end(),name.begin(),name.end())==sealed.end()&&std::search(sealed.begin(),sealed.end(),record.location.begin(),record.location.end())==sealed.end());}
 {WindowsStatement q(sql,raw,"UPDATE objects SET b=zeroblob(length(b)) WHERE k=?");q.text(1,"group-attachment-staging/v1/"+profile.peerId);check(!q.row());}sql.close(raw);
 StagingJournal corrupt(storage,profile.peerId);rejected([&]{corrupt.load();});check(storage.transferJournal(profile.peerId)==legacy);
}
int main(int argc,char** argv){try{
 auto root=std::filesystem::absolute(std::filesystem::path(argc>1?argv[1]:"build/tests/staging"));
 if(argc==3&&std::string(argv[2])=="--seed"){std::filesystem::create_directories(root);stage(root);ExitProcess(0);}
 std::filesystem::create_directories(root);auto fixture=root/Storage::newMessageId();wchar_t executable[32768]{};check(GetModuleFileNameW(nullptr,executable,32768)>0);
 auto command=L"\""+std::wstring(executable)+L"\" \""+fixture.wstring()+L"\" --seed";STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
 check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process));check(WaitForSingleObject(process.hProcess,30000)==WAIT_OBJECT_0);DWORD exit=1;check(GetExitCodeProcess(process.hProcess,&exit)&&exit==0);CloseHandle(process.hThread);CloseHandle(process.hProcess);
 verify(fixture);std::cout<<"PASS immutable encrypted staging: actual process exit/reopen, exact Unicode/plaintext/digests, protected key/token, CAS conflict, corruption/device rejection and unchanged legacy journal; no production group transport claim\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
