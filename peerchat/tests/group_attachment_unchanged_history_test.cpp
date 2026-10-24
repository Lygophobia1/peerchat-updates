#define NOMINMAX
#include "../storage.cpp"
#include "../windows_group_attachment_transfer.hpp"
#include "../windows_group_attachment_keys.hpp"
#include "../windows_group_attachment_history.hpp"
#include "../windows_group_attachment_output.hpp"
#include "../group_attachment_staging.hpp"
#include "../group_attachment_key_envelope.hpp"
#include "../windows_group_attachment_rpc.hpp"
#include <deque>
#include <filesystem>
#define private public
#include "../group_attachment_application.hpp"
#undef private
#include "../windows_group_attachment_application.hpp"
#include <iostream>
using namespace peerchat;
namespace ga=group_attachments;namespace cache=ga::windows;
static void check(bool value){if(!value)throw std::runtime_error("Unchanged group history assertion");}
template<class F>static void rejects(F action){bool failed=false;try{action();}catch(...){failed=true;}check(failed);}
struct CountingHistory:cache::HistoryStore {
 using cache::HistoryStore::HistoryStore;inline static unsigned gets=0,saves=0;inline static bool failNext=false;
 std::optional<Record> get(const std::string& id){++gets;return cache::HistoryStore::get(id);}
 std::uint64_t save(const Binding& binding,const Blob& expected,const Blob& next,bool active,std::uint64_t revision=0){++saves;if(failNext){failNext=false;throw std::runtime_error("Injected protected history save failure");}return cache::HistoryStore::save(binding,expected,next,active,revision);}
 static void reset(){gets=saves=0;}
};
struct CountingPlatform:cache::ApplicationPlatform {
 using cache::ApplicationPlatform::ApplicationPlatform;using HistoryStore=CountingHistory;
 static std::unique_ptr<HistoryStore> history(Storage&,const Profile& p,const std::wstring& directory){return std::make_unique<HistoryStore>(directory,p.peerId);}
};
using Application=ga::BasicApplication<CountingPlatform>;
int main(int argc,char** argv){try{
 check(argc==2);auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);Storage storage(root.wstring());auto profile=storage.profile();const std::string server(64,'b'),group(32,'c');transfer::windows::DurableJournal ordinary(root.wstring(),profile.peerId);
 cache::CacheStateStore journal(root.wstring(),profile.peerId,server,ordinary,[](const transfer::DurableState&)->std::unique_ptr<transfer::Source>{throw std::runtime_error("Unexpected source");});transfer::TransferManager transfers([]{return std::make_unique<transfer::windows::Sha256>();});
 Application application(storage,profile,transfers,journal,root.wstring(),[](const auto&,const auto*)->Application::Context{throw std::runtime_error("Unexpected context");},[](const auto&){throw std::runtime_error("Unexpected key access");});
 for(unsigned n=0;n<64;++n){auto suffix=std::to_string(n);auto job=std::make_unique<Application::Job>();job->id=std::string(32-suffix.size(),'e')+suffix;job->local=group;job->server=server;job->name="history.bin";job->state="canceled";job->sending=true;job->time=1700000000000ull+n;application.jobs_.emplace(job->id,std::move(job));}
 application.save();check(CountingHistory::gets==64&&CountingHistory::saves==64);CountingHistory::reset();auto began=std::chrono::steady_clock::now();for(unsigned n=0;n<100;++n)application.save();auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-began).count();check(CountingHistory::gets==0&&CountingHistory::saves==0);
 std::cout<<"HOST GROUP HISTORY: unchanged_rows=64 saves=100 protected_gets=0 history_saves=0 elapsed_us="<<elapsed<<'\n';
 auto& job=*application.jobs_.begin()->second;auto id=job.id;auto before=job.revision;job.receipt={1,2,3};application.save();check(job.revision==before+1&&CountingHistory::gets==1&&CountingHistory::saves==1);auto saved=job.saved.bytes;
 CountingHistory::reset();job.exportState="pending";application.save();check(job.saved.bytes==saved&&job.revision==before+2&&job.savedActive&&*job.savedActive&&CountingHistory::gets==1&&CountingHistory::saves==1);job.exportState="none";application.save();check(job.savedActive&&!*job.savedActive);
 // Unknown initial cache state must validate its existing protected row.
 job.savedActive.reset();CountingHistory::reset();application.save();check(CountingHistory::gets==1&&CountingHistory::saves==1);saved=job.saved.bytes;before=job.revision;
 job.receipt={4,5,6};CountingHistory::failNext=true;rejects([&]{application.save();});check(job.saved.bytes==saved&&job.revision==before);application.save();check(job.saved.bytes!=saved&&job.revision==before+1);
 // Restored records get their flag only from validated protected metadata.
 auto record=application.history_->get(id);check(record.has_value());application.jobs_.erase(id);application.loadRecord(std::move(*record));CountingHistory::reset();application.save();check(CountingHistory::gets==0&&CountingHistory::saves==0);
 auto& restored=*application.jobs_.at(id);auto stored=application.history_->get(id);auto changed=stored->payload.bytes;changed.back()^=1;application.history_->save(stored->binding,stored->payload.bytes,changed,stored->active,stored->revision);restored.receipt={7};rejects([&]{application.save();});
 auto raw=platform::openWindowsSqlDatabase((root/L"group-attachment-history").wstring());raw->execute("UPDATE attachment_history_rows SET b=X'00'");rejects([&]{application.save();});rejects([&]{application.history_->get(id);});
 std::cout<<"PASS exact group saved bytes+active skip unchanged protected reads; changed receipts/active, unknown/restored state, failed-save retry, stale CAS and tampered history validation retained\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
