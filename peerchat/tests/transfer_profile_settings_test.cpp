#include "../windows_transfer_io.hpp"
#include "../portable/transfer_storage.hpp"
#include <filesystem>
#include <iostream>
using namespace peerchat;
using namespace peerchat::transfer;
struct BrokenDownloadSettings final:StateStore {
 std::uint64_t wallTime()const override{return 1;}
 std::vector<DurableState> load()override{DurableState d;d.metadata.id=std::string(32,'1');d.metadata.name="file.bin";d.peer=std::string(64,'a');d.state=State::Complete;d.created=d.updated=1;d.expires=1000;return {d};}
 void save(const std::vector<DurableState>&)override{}
 std::unique_ptr<Source> source(const DurableState&)override{return {};}
 std::unique_ptr<Sink> sink(const DurableState&)override{return {};}
 std::optional<std::uint64_t> loadDownloadLimit()override{throw std::runtime_error("Isolated corruption");}
};
int main(){try{
 auto check=[](bool ok){if(!ok)throw std::runtime_error("Profile tuning assertion failed");};
 auto root=std::filesystem::current_path()/"build"/"tests"/("transfer-settings-"+Storage::newMessageId());
 std::string device;
 auto source=[](const DurableState&)->std::unique_ptr<Source>{throw std::runtime_error("No source fixture");};
 auto sink=[](const DurableState&)->std::unique_ptr<Sink>{throw std::runtime_error("No sink fixture");};
 {
  Storage storage(root.wstring());device=storage.profile().peerId;ProtectedStorageJournal journal(storage,device,source,sink);
  TransferManager manager([]{return std::make_unique<windows::Sha256>();},&journal);manager.restore(1);manager.defaults({65536,16,10000000},10000000);
  check(manager.options().chunkBytes==65536&&manager.options().window==16&&manager.options().bitsPerSecond==10000000&&manager.downloadLimit()==10000000);
  manager.configure("",{32768,4,5000000});manager.configureDownload(0);
 }
 {
  Storage storage(root.wstring());ProtectedStorageJournal journal(storage,device,source,sink);
  TransferManager manager([]{return std::make_unique<windows::Sha256>();},&journal);manager.restore(1);manager.defaults({65536,16,10000000},10000000);
  check(manager.options().chunkBytes==32768&&manager.options().window==4&&manager.options().bitsPerSecond==5000000&&manager.downloadLimit()==0);
  auto sealed=storage.transferTuning(device);manager.configureDownload(0);check(storage.transferTuning(device)==sealed);
 }
 auto windowsRoot=root/"windows";
 std::filesystem::create_directories(windowsRoot);
 {
  windows::DurableJournal journal(windowsRoot.wstring(),device);TransferManager manager([]{return std::make_unique<windows::Sha256>();},&journal);manager.restore(1);manager.defaults({65536,16,0});
  check(manager.options().chunkBytes==65536&&manager.options().window==16&&manager.options().bitsPerSecond==0&&manager.downloadLimit()==0);
  manager.configure("",{32768,1,1250000});manager.configureDownload(5000000);
 }
 {
  windows::DurableJournal journal(windowsRoot.wstring(),device);TransferManager manager([]{return std::make_unique<windows::Sha256>();},&journal);manager.restore(1);manager.defaults({65536,16,0});
  check(manager.options().chunkBytes==32768&&manager.options().window==1&&manager.options().bitsPerSecond==1250000&&manager.downloadLimit()==5000000);
 }
 {
  BrokenDownloadSettings journal;TransferManager manager([]{return std::make_unique<windows::Sha256>();},&journal);manager.restore(1);manager.defaults({65536,16,0});
  check(manager.snapshots().size()==1&&manager.completedRecords().size()==1&&!manager.hasTransmissionWork());
  manager.configure("",{65536,16,0});check(manager.diagnostics(2)[0].find("settings_unavailable=1")!=std::string::npos);
 }
 std::cout<<"PASS Android shared protected tuning + Windows sidecar: first-run defaults, custom preservation, 0 unlimited, separate caps, reopen and unchanged-write suppression\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
