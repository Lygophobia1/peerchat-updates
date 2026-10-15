#define main legacy_transfer_manager_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "transfer_manager_test.cpp"
#pragma GCC diagnostic pop
#undef main
#include "windows_download_save.hpp"
#include <iomanip>
namespace peerchat {std::string Storage::newMessageId(){static unsigned next=1;std::ostringstream out;out<<std::setfill('0')<<std::setw(32)<<std::hex<<next++;return out.str();}}
namespace ds=peerchat::download_save;
namespace out=peerchat::group_attachments::windows;
namespace io=peerchat::transfer::windows;
static Blob read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);check(bool(file));return Blob(std::istreambuf_iterator<char>(file),{});}
static void pump(ds::Coordinator& owner,const std::vector<DurableState>& records,const std::string& id){for(unsigned n=0;n<1000;++n){owner.tick(records);auto state=owner.status(id,true).state;if(state=="complete"||state=="failed")return;}throw std::runtime_error("Save coordinator did not terminate");}
int main(int argc,char** argv){try{
 check(argc==2);auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root/L"profile");std::filesystem::create_directory(root/L"chosen");const std::string device(64,'b'),id(32,'a');auto bytes=data(131073);io::Sha256 hash;for(size_t at=0;at<bytes.size();at+=ChunkBytes)hash.update(bytes.data()+at,std::min<size_t>(ChunkBytes,bytes.size()-at));DurableState record;record.metadata={id,"original.png",bytes.size(),hash.finish()};record.state=State::Complete;record.done=bytes.size();
 auto profile=(root/L"profile").wstring();auto target=root/L"chosen"/L"cat2-日本.png";
 {ds::Coordinator owner(profile,device);owner.select(id,target.wstring(),{});auto stage=owner.staging(profile);io::FileSink sink(stage,record.metadata,nullptr,true,true);for(size_t offset=0;offset<bytes.size();offset+=ChunkBytes)sink.append(Blob(bytes.begin()+offset,bytes.begin()+std::min(bytes.size(),offset+ChunkBytes)));record.location=sink.resumeToken();sink.commit();owner.tick({record});check(!std::filesystem::exists(target));}
 {ds::Coordinator owner(profile,device);owner.tick({record});check(!std::filesystem::exists(target));}
 {ds::Coordinator owner(profile,device);pump(owner,{record},id);check(owner.status(id,true).state=="complete"&&read(target)==bytes);}
 {ds::Coordinator owner(profile,device);check(owner.status(id,true).path==target.wstring());}
 // Source remains available even after the transfer journal no longer lists it.
 {ds::Coordinator owner(profile,device);auto next=root/L"chosen"/L"restored.png";owner.retry(id,next.wstring(),{},&record);pump(owner,{},id);check(read(next)==bytes);}
 auto absent=root/L"unavailable"/L"cat.png";
 {ds::Coordinator owner(profile,device);owner.retry(id,absent.wstring(),{},&record);pump(owner,{},id);check(owner.status(id,true).state=="failed");}
 {ds::Coordinator owner(profile,device);check(owner.status(id,true).state=="failed");auto recovered=root/L"chosen"/L"retry.png";owner.retry(id,recovered.wstring(),{},&record);pump(owner,{},id);check(read(recovered)==bytes);}
 // No-consent collision preserves both good copies and allows another target.
 {ds::Coordinator owner(profile,device);auto collision=root/L"chosen"/L"collision.png";owner.retry(id,collision.wstring(),{},&record);owner.tick({record});{std::ofstream f(collision);f<<"old";}pump(owner,{},id);check(owner.status(id,true).state=="failed"&&read(collision)==Blob({'o','l','d'}));auto fresh=root/L"chosen"/L"fresh.png";owner.retry(id,fresh.wstring(),{},&record);pump(owner,{},id);check(read(fresh)==bytes);}
 // Explicit replacement is separately identity-bound and survives restart.
 {ds::Coordinator owner(profile,device);auto approval=out::captureOverwriteTarget(target.wstring());owner.retry(id,target.wstring(),approval,&record);owner.tick({record});}
 {ds::Coordinator owner(profile,device);pump(owner,{},id);check(owner.status(id,true).state=="complete"&&read(target)==bytes);}
 // A bounded hot index must not become a lifetime download-count limit.
 auto internal=std::filesystem::path(profile)/L"attachment-received"/(id+".peerchat-received")/L"original.png";io::FileSource retained(internal.wstring(),"original.png");std::string first;std::wstring firstPath;
 {ds::Coordinator owner(profile,device);for(unsigned n=0;n<MaxRecords+2;++n){auto next=record;next.metadata.id=Storage::newMessageId();next.location=retained.resumeToken();auto destination=root/L"chosen"/("history-"+std::to_string(n)+".png");owner.retry(next.metadata.id,destination.wstring(),{},&next,true);pump(owner,{},next.metadata.id);check(owner.status(next.metadata.id,true).state=="complete");if(n==0){first=next.metadata.id;firstPath=destination.wstring();}}check(owner.status(first,true).path==firstPath);}
 {ds::Coordinator owner(profile,device);check(owner.status(first,true).path==firstPath);}
 std::cout<<"PASS automatic verified save, durable exact Unicode target, restart, retained source after journal eviction, missing destination/retry, collision preservation, explicit overwrite\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
