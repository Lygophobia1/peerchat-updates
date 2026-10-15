#include "storage.cpp"
#include "windows_group_attachment_history.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
namespace history=peerchat::group_attachments::windows;
using namespace peerchat;
void check(bool value){if(!value)throw std::runtime_error("Attachment history assertion failed");}
template<class F> void rejects(F&& operation){bool failed=false;try{operation();}catch(...){failed=true;}check(failed);}
std::string id(unsigned value){std::ostringstream out;out<<std::hex<<std::setw(32)<<std::setfill('0')<<value;return out.str();}
const std::string device(64,'a'),server(64,'b'),group(32,'c');
history::HistoryStore::Binding binding(unsigned n){return {id(n),group,server,1700000000000ull+n};}
Blob value(unsigned n){auto text=std::string("PRIVATE attachment body receipt filename 日本語 ")+std::to_string(n);return Blob(text.begin(),text.end());}
Blob encrypted(platform::SqlDatabase& db,const std::string& object){platform::CheckedSqlStatement row(db,"SELECT b FROM attachment_history_rows WHERE id=?");row.text(1,object);check(row.row());return row.bytes(0);}
int main(int argc,char** argv){try{
 check(argc==2);auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);auto profile=root/L"main";std::filesystem::create_directory(profile);
 {history::HistoryStore store(profile.wstring(),device);check(store.count()==0&&store.page().items.empty());
  for(unsigned n=1;n<=140;++n)check(store.save(binding(n),{},value(n),false)==1);
  for(unsigned n=141;n<=204;++n)check(store.save(binding(n),{},value(n),true)==1);
  check(store.count()==204&&store.count(group,history::HistoryStore::Filter::Completed)==140&&store.count({},history::HistoryStore::Filter::Active)==64);
  rejects([&]{store.save(binding(205),{},value(205),true);});check(store.count()==204&&!store.get(id(205)));
  std::set<std::string> seen;std::uint64_t before=INT64_MAX;unsigned pages=0;for(;;){auto page=store.page(group,before,17);check(page.items.size()<=17);for(const auto& entry:page.items)check(seen.insert(entry.binding.id).second);if(!page.more)break;check(page.cursor<before);before=page.cursor;++pages;}check(seen.size()==204&&pages>8);
  auto row=store.get(id(141));check(row&&row->payload.bytes==value(141)&&row->active&&row->revision==1);check(store.save(row->binding,row->payload.bytes,row->payload.bytes,false,row->revision)==2);check(store.count({},history::HistoryStore::Filter::Active)==63);check(store.save(binding(205),{},value(205),true)==1);
  rejects([&]{store.save(row->binding,row->payload.bytes,row->payload.bytes,true,row->revision);});rejects([&]{store.save(binding(1),{},value(1),false);});auto changed=binding(1);changed.server=std::string(64,'d');rejects([&]{store.save(changed,value(1),value(2),false,1);});changed=binding(1);changed.group=std::string(32,'e');rejects([&]{store.save(changed,value(1),value(2),false,1);});
  auto raw=platform::openWindowsSqlDatabase((profile/L"group-attachment-history").wstring());auto beforeBytes=encrypted(*raw,id(1));auto privateValue=value(1);check(std::search(beforeBytes.begin(),beforeBytes.end(),privateValue.begin(),privateValue.end())==beforeBytes.end());raw->execute("BEGIN IMMEDIATE");check(store.save(binding(1),value(1),value(1),false,1)==1);rejects([&]{store.save(binding(1),value(1),value(2),false,1);});raw->execute("ROLLBACK");check(encrypted(*raw,id(1))==beforeBytes&&store.get(id(1))->payload.bytes==value(1));
  check(store.save(binding(1),value(1),Blob(history::HistoryStore::MaxRecordBytes,42),false,1)==2);rejects([&]{store.save(binding(206),{},Blob(history::HistoryStore::MaxRecordBytes+1),false);});rejects([&]{store.page({},INT64_MAX,65);});
 }
 {history::HistoryStore reopened(profile.wstring(),device);check(reopened.count()==205&&reopened.get(id(1))->payload.bytes==Blob(history::HistoryStore::MaxRecordBytes,42));check(reopened.page({},INT64_MAX,64,history::HistoryStore::Filter::Active).items.size()==64);}
 rejects([&]{history::HistoryStore wrong(profile.wstring(),std::string(64,'f'));});
 // A copied supplemental database cannot be read in a different profile.
 auto other=root/L"copied";std::filesystem::create_directory(other);std::filesystem::create_directory(other/L"group-attachment-history");std::filesystem::copy_file(profile/L"group-attachment-history"/L"peerchat.sqlite",other/L"group-attachment-history"/L"peerchat.sqlite");rejects([&]{history::HistoryStore wrong(other.wstring(),device);});
 // Tampering index metadata is rejected against its protected immutable copy.
 {auto raw=platform::openWindowsSqlDatabase((profile/L"group-attachment-history").wstring());platform::CheckedSqlStatement update(*raw,"UPDATE attachment_history_rows SET group_id=? WHERE id=?");update.text(1,std::string(32,'e'));update.text(2,id(2));update.row();}
 {history::HistoryStore store(profile.wstring(),device);rejects([&]{store.get(id(2));});rejects([&]{store.save(binding(2),value(2),value(3),false,1);});}
 auto tiny=root/L"capacity";std::filesystem::create_directory(tiny);{history::HistoryStore store(tiny.wstring(),device,65536);check(store.save(binding(1),{},value(1),false)==1);rejects([&]{store.save(binding(2),{},Blob(256*1024,7),false);});check(store.count()==1&&store.get(id(1))->payload.bytes==value(1)&&!store.get(id(2)));}
 // An unrelated SQLite database must not receive this owner's tables.
 auto foreign=root/L"foreign";std::filesystem::create_directories(foreign/L"group-attachment-history");{auto raw=platform::openWindowsSqlDatabase((foreign/L"group-attachment-history").wstring());raw->execute("CREATE TABLE unrelated(value INTEGER); INSERT INTO unrelated VALUES(23);");}rejects([&]{history::HistoryStore store(foreign.wstring(),device);});
 {auto raw=platform::openWindowsSqlDatabase((foreign/L"group-attachment-history").wstring());platform::CheckedSqlStatement checkRows(*raw,"SELECT COUNT(*) FROM sqlite_master WHERE name='attachment_history_rows'");check(checkRows.row()&&checkRows.number(0)==0);}
 std::cout<<"PASS protected per-job history: 205 lifetime records/64 active, bounded group paging/count, CAS/revision/binding, no-op write avoidance, reopen/profile copy rejection, metadata tamper, capacity rollback, foreign schema isolation\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
