#pragma once
#include "windows_group_attachment_transfer.hpp"
#include "platform/windows_sql_database.hpp"

namespace peerchat::group_attachments::windows {
// Supplemental application history, never message Storage or transfer state.
// SQLite supplies transactions/paging through the existing platform provider;
// private bodies, receipts, filenames and local tokens remain DPAPI protected.
// The caller owns serialization and schedules writes only when a job changes.
class HistoryStore {
public:
 static constexpr size_t MaxRecordBytes=512*1024,MaxPage=64,MaxActive=64;
 static constexpr std::uint64_t DefaultMaximumBytes=512ull*1024*1024;
 enum class Filter {Any,Active,Completed};
 struct Binding {std::string id,group,server;std::uint64_t time=0;};
 struct Entry {Binding binding;std::uint64_t sequence=0,revision=0;bool active=false;};
 struct Record:Entry {SecretRecord payload;};
 struct Page {std::vector<Entry> items;std::uint64_t cursor=0;bool more=false;};
private:
 std::filesystem::path root_;std::string context_;HANDLE directory_=INVALID_HANDLE_VALUE,file_=INVALID_HANDLE_VALUE;
 std::shared_ptr<platform::SqlDatabase> database_;platform::WindowsRecordProtector protector_;std::uint64_t maximumBytes_;
 using Statement=platform::CheckedSqlStatement;
 static constexpr const char* OwnerSchema="CREATE TABLE attachment_history_owner(singleton INTEGER PRIMARY KEY CHECK(singleton=1),b BLOB NOT NULL)";
 static constexpr const char* RowsSchema="CREATE TABLE attachment_history_rows(sequence INTEGER PRIMARY KEY AUTOINCREMENT,id TEXT UNIQUE NOT NULL,group_id TEXT NOT NULL,server_id TEXT NOT NULL,created INTEGER NOT NULL,revision INTEGER NOT NULL,active INTEGER NOT NULL CHECK(active IN (0,1)),b BLOB NOT NULL)";
 static void validate(const Binding& b){protocol::check(protocol::id(b.id,32)&&protocol::id(b.group,32)&&protocol::id(b.server)&&b.time&&b.time<=253402300799000ull);}
 static bool same(const Binding& a,const Binding& b){return a.id==b.id&&a.group==b.group&&a.server==b.server&&a.time==b.time;}
 std::string protectionContext(const std::string& id)const{return context_+"/"+id;}
 static void putBinding(protocol::Writer& w,const Binding& b){validate(b);w.string(b.id);w.string(b.group);w.string(b.server);w.number(b.time);}
 static Binding readBinding(protocol::Reader& r){Binding b{r.string(),r.string(),r.string(),r.number()};validate(b);return b;}
 static void putPayload(protocol::Writer& w,const Blob& payload){protocol::check(!payload.empty()&&payload.size()<=MaxRecordBytes);w.number(payload.size());for(size_t offset=0;offset<payload.size();offset+=transfer::ChunkBytes){SecretRecord part(Blob(payload.begin()+offset,payload.begin()+std::min(payload.size(),offset+transfer::ChunkBytes)));w.bytes(part.bytes);}}
 static SecretRecord readPayload(protocol::Reader& r){auto length=r.number();protocol::check(length&&length<=MaxRecordBytes);SecretRecord result;result.bytes.reserve(static_cast<size_t>(length));while(result.bytes.size()<length){SecretRecord part(r.bytes());protocol::check(part.bytes.size()==std::min<std::uint64_t>(transfer::ChunkBytes,length-result.bytes.size()));result.bytes.insert(result.bytes.end(),part.bytes.begin(),part.bytes.end());}return result;}
 Blob seal(const Binding& b,std::uint64_t revision,bool active,const Blob& payload){protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/AttachmentHistoryRecord/v1");putBinding(w,b);w.number(revision);w.number(active);putPayload(w,payload);SecretRecord plain(std::move(w.data));return protector_.protect(plain.bytes,protectionContext(b.id));}
 Record read(Statement& row){
  Record result;result.binding={row.text(0),row.text(1),row.text(2),static_cast<std::uint64_t>(row.number(3))};validate(result.binding);auto sequence=row.number(4),revision=row.number(5),active=row.number(6);protocol::check(sequence>0&&revision>0&&(active==0||active==1));result.sequence=static_cast<std::uint64_t>(sequence);result.revision=static_cast<std::uint64_t>(revision);result.active=active!=0;
  auto encrypted=row.bytes(7);protocol::check(encrypted.size()<=MaxRecordBytes+16384);SecretRecord plain(protector_.unprotect(encrypted,protectionContext(result.binding.id)));protocol::Reader r(plain.bytes);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/AttachmentHistoryRecord/v1"&&same(readBinding(r),result.binding)&&r.number()==result.revision&&r.number()==static_cast<unsigned>(result.active));auto payload=readPayload(r);r.end();result.payload.bytes.swap(payload.bytes);return result;
 }
 std::uint64_t scalar(const std::string& sql){Statement query(*database_,sql);protocol::check(query.row());auto number=query.number(0);protocol::check(number>=0&&!query.row());return static_cast<std::uint64_t>(number);}
 void safeFiles()const{for(const auto& suffix:{L"",L"-journal",L"-wal",L"-shm"}){auto path=root_/(std::wstring(L"peerchat.sqlite")+suffix);HANDLE file=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(file==INVALID_HANDLE_VALUE){transfer::windows::require(GetLastError()==ERROR_FILE_NOT_FOUND);continue;}BY_HANDLE_FILE_INFORMATION info{};bool safe=GetFileInformationByHandle(file,&info)&&info.nNumberOfLinks==1&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));CloseHandle(file);transfer::windows::require(safe);}}
 void space(size_t bytes){safeFiles();ULARGE_INTEGER available{};transfer::windows::require(GetDiskFreeSpaceExW(root_.c_str(),&available,nullptr,nullptr)!=0);protocol::check(available.QuadPart>=16ull*1024*1024+3ull*bytes);auto pageSize=scalar("PRAGMA page_size"),pages=scalar("PRAGMA page_count");protocol::check(pageSize>0&&pages<=maximumBytes_/pageSize);}
 std::string condition(const std::string& group,Filter filter)const{protocol::check(group.empty()||protocol::id(group,32));protocol::check(filter==Filter::Any||filter==Filter::Active||filter==Filter::Completed);return std::string(group.empty()?"1":"group_id=?")+(filter==Filter::Any?"":filter==Filter::Active?" AND active=1":" AND active=0");}
 void initialize(){
  // Validate ownership before creating or changing any application schema.
  bool owner=false,rows=false;{Statement tables(*database_,"SELECT name,sql,type FROM sqlite_master WHERE name NOT LIKE 'sqlite_%'");size_t seen=0;while(tables.row()){protocol::check(++seen<=4);auto name=tables.text(0),sql=tables.text(1),type=tables.text(2);if(name=="attachment_history_owner"){protocol::check(type=="table"&&sql==OwnerSchema);owner=true;}else if(name=="attachment_history_rows"){protocol::check(type=="table"&&sql==RowsSchema);rows=true;}else protocol::check(type=="index"&&(name=="attachment_history_group_sequence"||name=="attachment_history_active_sequence"));}}
  protocol::check(owner==rows);if(owner){Statement marker(*database_,"SELECT b FROM attachment_history_owner WHERE singleton=1");protocol::check(marker.row());SecretRecord secret(protector_.unprotect(marker.bytes(0),context_+"/owner"));protocol::Reader r(secret.bytes);protocol::check(r.type()==protocol::FileTransfer&&r.string()==context_);r.end();protocol::check(!marker.row());}
  auto pageSize=scalar("PRAGMA page_size");protocol::check(pageSize&&maximumBytes_/pageSize>=16&&scalar("PRAGMA page_count")<=maximumBytes_/pageSize);database_->execute("PRAGMA synchronous=FULL; PRAGMA secure_delete=ON; PRAGMA temp_store=MEMORY;");auto maximumPages=maximumBytes_/pageSize;protocol::check(scalar("PRAGMA max_page_count="+std::to_string(maximumPages))==maximumPages);
  if(!owner){space(16384);platform::SqlTransaction transaction(*database_);database_->execute(OwnerSchema);database_->execute(RowsSchema);database_->execute("CREATE INDEX attachment_history_group_sequence ON attachment_history_rows(group_id,sequence); CREATE INDEX attachment_history_active_sequence ON attachment_history_rows(active,sequence);");protocol::Writer w(protocol::FileTransfer);w.string(context_);auto encrypted=protector_.protect(w.data,context_+"/owner");Statement marker(*database_,"INSERT INTO attachment_history_owner VALUES(1,?)");marker.blob(1,encrypted);marker.row();transaction.commit();}
 }
public:
 explicit HistoryStore(const std::wstring& profile,const std::string& device,std::uint64_t maximumBytes=DefaultMaximumBytes):root_(std::filesystem::absolute(profile)/L"group-attachment-history"),maximumBytes_(maximumBytes){
  protocol::check(protocol::id(device)&&maximumBytes>=65536&&maximumBytes<=INT64_MAX);context_="PeerChat/AttachmentHistory/v1/"+device+"/"+unicode::encode(std::filesystem::absolute(profile).wstring());std::filesystem::create_directory(root_);
  try{directory_=CreateFileW(root_.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);transfer::windows::require(directory_!=INVALID_HANDLE_VALUE);BY_HANDLE_FILE_INFORMATION info{};transfer::windows::require(GetFileInformationByHandle(directory_,&info)&&(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT));auto path=root_/L"peerchat.sqlite";file_=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);transfer::windows::require(file_!=INVALID_HANDLE_VALUE&&GetFileInformationByHandle(file_,&info)&&info.nNumberOfLinks==1&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)));safeFiles();database_=platform::openWindowsSqlDatabase(root_.wstring());initialize();}
  catch(...){database_.reset();if(file_!=INVALID_HANDLE_VALUE)CloseHandle(file_);if(directory_!=INVALID_HANDLE_VALUE)CloseHandle(directory_);throw;}
 }
 ~HistoryStore(){database_.reset();if(file_!=INVALID_HANDLE_VALUE)CloseHandle(file_);if(directory_!=INVALID_HANDLE_VALUE)CloseHandle(directory_);}
 HistoryStore(const HistoryStore&)=delete;HistoryStore& operator=(const HistoryStore&)=delete;
 std::optional<Record> get(const std::string& id){protocol::check(protocol::id(id,32));Statement query(*database_,"SELECT id,group_id,server_id,created,sequence,revision,active,b FROM attachment_history_rows WHERE id=?");query.text(1,id);if(!query.row())return {};auto result=read(query);protocol::check(!query.row());return std::optional<Record>(std::move(result));}
 std::uint64_t count(const std::string& group={},Filter filter=Filter::Any){auto where=condition(group,filter);Statement query(*database_,"SELECT COUNT(*) FROM attachment_history_rows WHERE "+where);if(!group.empty())query.text(1,group);protocol::check(query.row());auto value=query.number(0);protocol::check(value>=0&&!query.row());return static_cast<std::uint64_t>(value);}
 Page page(const std::string& group={},std::uint64_t before=INT64_MAX,size_t limit=32,Filter filter=Filter::Any){
  protocol::check(before>0&&before<=INT64_MAX&&limit&&limit<=MaxPage);auto where=condition(group,filter);Statement query(*database_,"SELECT id,group_id,server_id,created,sequence,revision,active,b FROM attachment_history_rows WHERE "+where+" AND sequence<? ORDER BY sequence DESC LIMIT ?");int parameter=1;if(!group.empty())query.text(parameter++,group);query.number(parameter++,static_cast<std::int64_t>(before));query.number(parameter,static_cast<std::int64_t>(limit+1));Page result;
  while(query.row()){if(result.items.size()==limit){result.more=true;break;}auto record=read(query);result.cursor=record.sequence;result.items.push_back(static_cast<const Entry&>(record));}return result;
 }
 // Revision 0 means insert-only. Payload plus revision form the expected
 // snapshot; immutable binding and active-state CAS cannot be bypassed by an
 // unchanged payload. No-op saves do not open a write transaction or reencrypt.
 std::uint64_t save(const Binding& binding,const Blob& expected,const Blob& next,bool active,std::uint64_t expectedRevision=0){
  validate(binding);protocol::check(!next.empty()&&next.size()<=MaxRecordBytes&&expected.size()<=MaxRecordBytes&&expectedRevision<INT64_MAX);auto old=get(binding.id);protocol::check(old?expectedRevision==old->revision&&same(binding,old->binding)&&expected==old->payload.bytes:expectedRevision==0&&expected.empty());if(old&&old->active==active&&old->payload.bytes==next)return old->revision;
  space(next.size());platform::SqlTransaction transaction(*database_);auto current=get(binding.id);protocol::check(current?expectedRevision==current->revision&&same(binding,current->binding)&&expected==current->payload.bytes:expectedRevision==0&&expected.empty());if(active&&(!current||!current->active))protocol::check(count({},Filter::Active)<MaxActive);auto revision=expectedRevision+1;auto protectedBytes=seal(binding,revision,active,next);
  Statement update(*database_,"INSERT INTO attachment_history_rows(id,group_id,server_id,created,revision,active,b) VALUES(?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET revision=excluded.revision,active=excluded.active,b=excluded.b");update.text(1,binding.id);update.text(2,binding.group);update.text(3,binding.server);update.number(4,static_cast<std::int64_t>(binding.time));update.number(5,static_cast<std::int64_t>(revision));update.number(6,active?1:0);update.blob(7,protectedBytes);update.row();transaction.commit();return revision;
 }
};
}
