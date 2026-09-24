#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "storage.hpp"
#include "group.hpp"
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace peerchat {
namespace storage_detail {
constexpr int MaxRecord=1024*1024;
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
void crypto(NTSTATUS status) {require(status>=0,"Windows cryptographic operation failed");}
std::string utf8(const std::wstring& s) {
    require(s.size()<=32768,"Text field too long");if(s.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    require(n>0,"Invalid Unicode text");std::string out(n,'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;
}
std::wstring wide(const std::string& s) {
    if(s.empty())return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    require(n>0,"Invalid stored Unicode text");std::wstring out(n,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n);return out;
}
std::string hex(const Blob& bytes) {
    constexpr char digits[]="0123456789abcdef";std::string s;
    for(auto b:bytes){s+=digits[b>>4];s+=digits[b&15];}return s;
}
bool identifier(const std::string& s,size_t size=64) {
    return s.size()==size&&std::all_of(s.begin(),s.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
struct Writer {
    Blob data{'P','C','S',1};
    ~Writer(){SecureZeroMemory(data.data(),data.size());}
    void number(std::uint64_t v){for(int i=0;i<8;i++)data.push_back(static_cast<unsigned char>(v>>(8*i)));}
    void bytes(const Blob& b){require(b.size()<=MaxRecord,"Record field too large");number(b.size());data.insert(data.end(),b.begin(),b.end());}
    void string(const std::string& s){bytes(Blob(s.begin(),s.end()));}
    void text(const std::wstring& s){string(utf8(s));}
};
struct Reader {
    const Blob& data;size_t offset=4;
    explicit Reader(const Blob& b):data(b){require(b.size()>=4&&b[0]=='P'&&b[1]=='C'&&b[2]=='S'&&b[3]==1,"Unsupported stored record version");}
    std::uint64_t number(){require(data.size()-offset>=8,"Truncated record");std::uint64_t n=0;for(int i=0;i<8;i++)n|=std::uint64_t(data[offset++])<<(8*i);return n;}
    Blob bytes(){auto n=number();require(n<=MaxRecord&&n<=data.size()-offset,"Invalid record length");Blob b(data.begin()+offset,data.begin()+offset+static_cast<size_t>(n));offset+=static_cast<size_t>(n);return b;}
    std::string string(){auto b=bytes();return {b.begin(),b.end()};}
    std::wstring text(){return wide(string());}
    size_t remaining()const{return data.size()-offset;}
    void end(){require(offset==data.size(),"Trailing stored data");}
};
struct Secret {
    Blob data;
    ~Secret(){SecureZeroMemory(data.data(),data.size());}
};
Blob protect(const Blob& plain,const std::string& context,bool encrypt) {
    require(!plain.empty()&&plain.size()<=MaxRecord,"Invalid protected record size");
    DATA_BLOB in{static_cast<DWORD>(plain.size()),const_cast<BYTE*>(plain.data())};
    DATA_BLOB entropy{static_cast<DWORD>(context.size()),reinterpret_cast<BYTE*>(const_cast<char*>(context.data()))},out{};
    BOOL ok=encrypt?CryptProtectData(&in,L"PeerChat record",&entropy,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out)
                   :CryptUnprotectData(&in,nullptr,&entropy,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out);
    if(ok==FALSE){DWORD error=GetLastError();throw std::runtime_error(std::string(encrypt?"Could not encrypt local data":"Local data failed authentication or belongs to another Windows account")+" (DPAPI Win32 error "+std::to_string(error)+")");}
    Blob result(out.pbData,out.pbData+out.cbData);SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);return result;
}
struct Algorithm {BCRYPT_ALG_HANDLE h{};~Algorithm(){if(h)BCryptCloseAlgorithmProvider(h,0);}};
struct Key {BCRYPT_KEY_HANDLE h{};~Key(){if(h)BCryptDestroyKey(h);}};
Blob exportKey(BCRYPT_KEY_HANDLE key,LPCWSTR kind) {
    ULONG size=0;crypto(BCryptExportKey(key,nullptr,kind,nullptr,0,&size,0));Blob b(size);
    crypto(BCryptExportKey(key,nullptr,kind,b.data(),size,&size,0));return b;
}
void validateProfile(const Profile& p) {
    require(!p.name.empty()&&p.name.size()<=64&&p.listenPort!=0,"Invalid profile settings");
    require(p.privateKey.size()<=1024&&p.publicKey.size()<=1024,"Invalid identity size");
    Algorithm alg;Key key;crypto(BCryptOpenAlgorithmProvider(&alg.h,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0));
    crypto(BCryptImportKeyPair(alg.h,nullptr,BCRYPT_ECCPRIVATE_BLOB,&key.h,const_cast<PUCHAR>(p.privateKey.data()),static_cast<ULONG>(p.privateKey.size()),0));
    require(exportKey(key.h,BCRYPT_ECCPUBLIC_BLOB)==p.publicKey&&Storage::publicKeyId(p.publicKey)==p.peerId,"Identity key mismatch");
}

// Windows ships the established SQLite implementation. Resolve only from System32
// to avoid loading a DLL planted beside a downloaded app. ABI declarations below
// cover the small stable public SQLite API subset used by this store.
struct sqlite3;struct sqlite3_stmt;
struct Sqlite {
    HMODULE module{};
    int (*open16)(const void*,sqlite3**){};int (*close)(sqlite3*){};
    int (*exec)(sqlite3*,const char*,int(*)(void*,int,char**,char**),void*,char**){};
    int (*prepare_v2)(sqlite3*,const char*,int,sqlite3_stmt**,const char**){};
    int (*finalize)(sqlite3_stmt*){};int (*step)(sqlite3_stmt*){};
    int (*bind_text)(sqlite3_stmt*,int,const char*,int,void(*)(void*)){};
    int (*bind_blob)(sqlite3_stmt*,int,const void*,int,void(*)(void*)){};
    int (*bind_int64)(sqlite3_stmt*,int,std::int64_t){};
    const void* (*column_blob)(sqlite3_stmt*,int){};int (*column_bytes)(sqlite3_stmt*,int){};
    const unsigned char* (*column_text)(sqlite3_stmt*,int){};
    std::int64_t (*column_int64)(sqlite3_stmt*,int){};
    int (*busy_timeout)(sqlite3*,int){};
    template<class T> void load(T& target,const char* name){auto address=GetProcAddress(module,name);require(address!=nullptr,"Windows SQLite API unavailable");static_assert(sizeof(target)==sizeof(address));memcpy(&target,&address,sizeof(target));}
    Sqlite(){
        module=LoadLibraryExW(L"winsqlite3.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);require(module!=nullptr,"Windows SQLite is unavailable; Windows 10/11 is required");
        try {
#define SQL_LOAD(name) load(name,"sqlite3_" #name)
            SQL_LOAD(open16);SQL_LOAD(close);SQL_LOAD(exec);SQL_LOAD(prepare_v2);SQL_LOAD(finalize);SQL_LOAD(step);
            SQL_LOAD(bind_text);SQL_LOAD(bind_blob);SQL_LOAD(bind_int64);SQL_LOAD(column_blob);SQL_LOAD(column_bytes);SQL_LOAD(column_text);SQL_LOAD(column_int64);SQL_LOAD(busy_timeout);
#undef SQL_LOAD
        }catch(...){FreeLibrary(module);throw;}
    }
    ~Sqlite(){FreeLibrary(module);}
};
struct Statement {
    Sqlite& api;sqlite3_stmt* handle{};
    Statement(Sqlite& a,sqlite3* db,const char* sql):api(a){require(api.prepare_v2(db,sql,-1,&handle,nullptr)==0,"Local database statement failed");}
    ~Statement(){if(handle)api.finalize(handle);}
    void text(int i,const std::string& s){require(api.bind_text(handle,i,s.data(),static_cast<int>(s.size()),reinterpret_cast<void(*)(void*)>(-1))==0,"Database text binding failed");}
    void blob(int i,const Blob& b){require(api.bind_blob(handle,i,b.data(),static_cast<int>(b.size()),reinterpret_cast<void(*)(void*)>(-1))==0,"Database blob binding failed");}
    void number(int i,std::int64_t n){require(api.bind_int64(handle,i,n)==0,"Database number binding failed");}
    bool row(){int result=api.step(handle);require(result==100||result==101,"Local database read/write failed");return result==100;}
    Blob bytes(int i){int n=api.column_bytes(handle,i);require(n>0&&n<=MaxRecord,"Invalid encrypted record size");const auto* b=static_cast<const unsigned char*>(api.column_blob(handle,i));require(b!=nullptr,"Invalid stored blob");return {b,b+n};}
    std::string text(int i){const auto* s=api.column_text(handle,i);require(s!=nullptr,"Invalid stored identifier");return reinterpret_cast<const char*>(s);}
};
struct Transaction {
    Sqlite& api;sqlite3* db;bool committed=false;
    Transaction(Sqlite& api,sqlite3* db):api(api),db(db){require(api.exec(db,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr)==0,"Database is busy; try again");}
    void commit(){require(api.exec(db,"COMMIT",nullptr,nullptr,nullptr)==0,"Could not commit local data");committed=true;}
    ~Transaction(){if(!committed)api.exec(db,"ROLLBACK",nullptr,nullptr,nullptr);}
};
StoredMessage decodeMessage(const Blob& plain) {
    Reader r(plain);StoredMessage m;m.id=r.string();m.sender=r.string();m.recipient=r.string();m.text=r.text();m.utcMilliseconds=r.number();auto state=r.number();
    // v0.2 encrypted records end after delivery. New records append the logical
    // position; accepting the old shape keeps existing history readable.
    if(r.remaining()>=8)m.logicalOrder=r.number();
    r.end();
    require(identifier(m.id,32)&&identifier(m.sender)&&identifier(m.recipient)&&state<=2&&!m.text.empty()&&m.text.size()<=16384,"Invalid stored message");m.delivery=static_cast<Delivery>(state);return m;
}
void encodeMessage(Writer& w,const StoredMessage& m){w.string(m.id);w.string(m.sender);w.string(m.recipient);w.text(m.text);w.number(m.utcMilliseconds);w.number(static_cast<unsigned char>(m.delivery));w.number(m.logicalOrder);}
}

using namespace storage_detail;
struct Storage::Impl {
    Sqlite sql;sqlite3* db{};
    explicit Impl(const std::wstring& directory) {
        std::filesystem::create_directories(directory);
        auto path=(std::filesystem::path(directory)/L"peerchat.sqlite").wstring();
        if(sql.open16(path.c_str(),&db)!=0){if(db)sql.close(db);db=nullptr;throw std::runtime_error("Cannot open local database");}
        try {
            require(sql.busy_timeout(db,1500)==0,"Cannot configure database timeout");
            execute("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA temp_store=MEMORY; PRAGMA secure_delete=ON;");
            execute("CREATE TABLE IF NOT EXISTS objects(k TEXT PRIMARY KEY,b BLOB NOT NULL);"
                    "CREATE TABLE IF NOT EXISTS messages(id TEXT UNIQUE NOT NULL,peer TEXT NOT NULL,b BLOB NOT NULL);"
                    "CREATE INDEX IF NOT EXISTS conversation_index ON messages(peer);");
            // Separate outbox index contains identifiers only, never message text.
            execute("CREATE TABLE IF NOT EXISTS outbox(id TEXT PRIMARY KEY,peer TEXT NOT NULL);"
                    "CREATE TABLE IF NOT EXISTS group_events(event_id TEXT PRIMARY KEY,group_id TEXT NOT NULL,logical_order INTEGER NOT NULL,b BLOB NOT NULL);"
                    "CREATE INDEX IF NOT EXISTS group_event_order ON group_events(group_id,logical_order,event_id);"
                    "CREATE TABLE IF NOT EXISTS group_outbox(event_id TEXT NOT NULL REFERENCES group_events(event_id) ON DELETE CASCADE,peer_id TEXT NOT NULL,PRIMARY KEY(event_id,peer_id));"
                    "CREATE INDEX IF NOT EXISTS group_outbox_peer ON group_outbox(peer_id,event_id);");
        }catch(...){sql.close(db);db=nullptr;throw;}
    }
    ~Impl(){if(db)sql.close(db);}
    void execute(const char* statement){require(sql.exec(db,statement,nullptr,nullptr,nullptr)==0,"Local database operation failed");}
    Blob get(const std::string& key) {
        Statement s(sql,db,"SELECT b FROM objects WHERE k=?");s.text(1,key);if(!s.row())return {};
        return protect(s.bytes(0),"PeerChat.v1/"+key,false);
    }
    void put(const std::string& key,const Blob& plain) {
        Blob encrypted=protect(plain,"PeerChat.v1/"+key,true);
        Statement s(sql,db,"INSERT INTO objects(k,b) VALUES(?,?) ON CONFLICT(k) DO UPDATE SET b=excluded.b");s.text(1,key);s.blob(2,encrypted);s.row();
    }
    void erase(const std::string& key) {
        Statement statement(sql,db,"DELETE FROM objects WHERE k=?");statement.text(1,key);statement.row();
    }
};
Storage::Storage(const std::wstring& directory):impl_(std::make_unique<Impl>(directory)){}
Storage::~Storage()=default;
std::string Storage::newMessageId(){Blob id(16);crypto(BCryptGenRandom(nullptr,id.data(),16,BCRYPT_USE_SYSTEM_PREFERRED_RNG));id[6]=(id[6]&15)|64;id[8]=(id[8]&63)|128;return hex(id);}
std::string Storage::publicKeyId(const Blob& key) {
    Algorithm alg;crypto(BCryptOpenAlgorithmProvider(&alg.h,BCRYPT_SHA256_ALGORITHM,nullptr,0));
    BCRYPT_HASH_HANDLE hash{};crypto(BCryptCreateHash(alg.h,&hash,nullptr,0,nullptr,0,0));Blob digest(32);
    NTSTATUS status=BCryptHashData(hash,const_cast<PUCHAR>(key.data()),static_cast<ULONG>(key.size()),0);
    if(status>=0)status=BCryptFinishHash(hash,digest.data(),32,0);
    BCryptDestroyHash(hash);crypto(status);return hex(digest);
}
Profile Storage::profile() {
    Transaction transaction(impl_->sql,impl_->db);
    Secret saved{impl_->get("profile")};Profile p;
    if(saved.data.empty()) {
        Algorithm alg;Key key;crypto(BCryptOpenAlgorithmProvider(&alg.h,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0));
        crypto(BCryptGenerateKeyPair(alg.h,&key.h,256,0));crypto(BCryptFinalizeKeyPair(key.h,0));
        p.publicKey=exportKey(key.h,BCRYPT_ECCPUBLIC_BLOB);p.privateKey=exportKey(key.h,BCRYPT_ECCPRIVATE_BLOB);p.peerId=publicKeyId(p.publicKey);
        saveProfile(p);transaction.commit();return p;
    }
    Reader r(saved.data);p.peerId=r.string();p.name=r.text();auto port=r.number();require(port>0&&port<=65535,"Invalid stored listen port");p.listenPort=static_cast<std::uint16_t>(port);
    p.publicKey=r.bytes();p.privateKey=r.bytes();r.end();validateProfile(p);transaction.commit();return p;
}
void Storage::saveProfile(const Profile& p) {
    validateProfile(p);Writer w;w.string(p.peerId);w.text(p.name);w.number(p.listenPort);w.bytes(p.publicKey);w.bytes(p.privateKey);impl_->put("profile",w.data);
}
void Storage::saveContact(const Contact& c) {
    require(identifier(c.peerId)&&(c.publicKey.empty()||publicKeyId(c.publicKey)==c.peerId)&&!c.name.empty()&&c.name.size()<=64,"Invalid contact identity");
    require(c.endpoints.size()<=128,"Too many endpoints for one contact");Writer w;w.string(c.peerId);w.text(c.name);w.bytes(c.publicKey);w.number(c.verified);w.number(c.endpoints.size());
    for(const auto& e:c.endpoints){require(!e.host.empty()&&e.host.size()<=253&&e.port,"Invalid endpoint");w.text(e.host);w.number(e.port);}
    impl_->put("contact/"+c.peerId,w.data);
}
void Storage::removeContact(const std::string& peerId) {
    require(identifier(peerId),"Invalid contact identity");
    impl_->erase("contact/"+peerId);
}
std::vector<Contact> Storage::contacts() {
    Statement s(impl_->sql,impl_->db,"SELECT k,b FROM objects WHERE k LIKE 'contact/%' ORDER BY k");std::vector<Contact> result;
    while(s.row()) {
        std::string key=s.text(0);Secret plain{protect(s.bytes(1),"PeerChat.v1/"+key,false)};Reader r(plain.data);Contact c;
        c.peerId=r.string();c.name=r.text();c.publicKey=r.bytes();auto trust=r.number(),count=r.number();
        require(trust<=1&&count<=128&&key=="contact/"+c.peerId&&(c.publicKey.empty()||publicKeyId(c.publicKey)==c.peerId),"Invalid contact record");c.verified=trust==1;
        for(std::uint64_t i=0;i<count;i++){Endpoint e;e.host=r.text();auto port=r.number();require(port>0&&port<=65535,"Invalid contact port");e.port=static_cast<std::uint16_t>(port);c.endpoints.push_back(std::move(e));}
        r.end();result.push_back(std::move(c));
    }
    return result;
}
bool Storage::append(const std::string& conversation,const StoredMessage& m) {
    require(identifier(conversation)&&identifier(m.id,32)&&identifier(m.sender)&&identifier(m.recipient)&&
        (conversation==m.sender||conversation==m.recipient)&&!m.text.empty()&&m.text.size()<=16384&&static_cast<unsigned char>(m.delivery)<=2,"Invalid message");
    Transaction transaction(impl_->sql,impl_->db);
    Statement existing(impl_->sql,impl_->db,"SELECT peer,b FROM messages WHERE id=?");existing.text(1,m.id);
    if(existing.row()) {
        require(existing.text(0)==conversation,"Message ID reused in another conversation");
        Secret plain{protect(existing.bytes(1),"PeerChat.v1/message/"+m.id+"/"+conversation,false)};auto old=decodeMessage(plain.data);
        require(old.id==m.id&&old.sender==m.sender&&old.recipient==m.recipient&&old.text==m.text&&old.utcMilliseconds==m.utcMilliseconds,"Conflicting duplicate message ID");transaction.commit();return false;
    }
    Writer plain;encodeMessage(plain,m);Blob encrypted=protect(plain.data,"PeerChat.v1/message/"+m.id+"/"+conversation,true);
    Statement insert(impl_->sql,impl_->db,"INSERT INTO messages(id,peer,b) VALUES(?,?,?)");insert.text(1,m.id);insert.text(2,conversation);insert.blob(3,encrypted);insert.row();
    if(m.delivery!=Delivery::Delivered){Statement pending(impl_->sql,impl_->db,"INSERT INTO outbox(id,peer) VALUES(?,?)");pending.text(1,m.id);pending.text(2,conversation);pending.row();}
    transaction.commit();return true;
}
void Storage::acknowledge(const std::string& id,Delivery delivery) {
    require(identifier(id,32)&&static_cast<unsigned char>(delivery)<=2,"Invalid delivery acknowledgement");
    Transaction transaction(impl_->sql,impl_->db);
    Statement s(impl_->sql,impl_->db,"SELECT peer,b FROM messages WHERE id=?");s.text(1,id);if(!s.row()){transaction.commit();return;}
    std::string peer=s.text(0);Secret plain{protect(s.bytes(1),"PeerChat.v1/message/"+id+"/"+peer,false)};auto m=decodeMessage(plain.data);
    require(m.id==id,"Stored message ID mismatch");if(delivery<=m.delivery){transaction.commit();return;}m.delivery=delivery;
    Writer writer;encodeMessage(writer,m);Blob encrypted=protect(writer.data,"PeerChat.v1/message/"+id+"/"+peer,true);
    Statement update(impl_->sql,impl_->db,"UPDATE messages SET b=? WHERE id=?");update.blob(1,encrypted);update.text(2,id);update.row();
    if(delivery==Delivery::Delivered){Statement remove(impl_->sql,impl_->db,"DELETE FROM outbox WHERE id=?");remove.text(1,id);remove.row();}
    transaction.commit();
}
std::uint64_t Storage::nextLogicalOrder() {
    Transaction transaction(impl_->sql,impl_->db);std::uint64_t current=0;Blob saved=impl_->get("logical-clock");
    if(!saved.empty()){Reader r(saved);current=r.number();r.end();}
    require(current!=~std::uint64_t{0},"Logical message order exhausted");++current;
    Writer writer;writer.number(current);impl_->put("logical-clock",writer.data);transaction.commit();return current;
}
void Storage::observeLogicalOrder(std::uint64_t remoteOrder) {
    if(remoteOrder==0)return;
    Transaction transaction(impl_->sql,impl_->db);std::uint64_t current=0;Blob saved=impl_->get("logical-clock");
    if(!saved.empty()){Reader r(saved);current=r.number();r.end();}
    if(remoteOrder>current){Writer writer;writer.number(remoteOrder);impl_->put("logical-clock",writer.data);}
    transaction.commit();
}
std::vector<StoredMessage> Storage::history(const std::string& conversation,int limit,std::int64_t beforeSequence) {
    require(identifier(conversation)&&limit>0&&limit<=1000,"Invalid history page request");
    Statement s(impl_->sql,impl_->db,"SELECT rowid,id,b FROM messages WHERE peer=? AND rowid<? ORDER BY rowid DESC LIMIT ?");
    s.text(1,conversation);s.number(2,beforeSequence);s.number(3,limit);std::vector<StoredMessage> result;
    while(s.row()) {
        auto id=s.text(1);Secret plain{protect(s.bytes(2),"PeerChat.v1/message/"+id+"/"+conversation,false)};auto m=decodeMessage(plain.data);require(m.id==id,"Stored message ID mismatch");m.sequence=impl_->sql.column_int64(s.handle,0);result.push_back(std::move(m));
    }
    std::reverse(result.begin(),result.end());return result;
}
void Storage::eraseHistory(const std::string& conversation) {
    require(identifier(conversation),"Invalid conversation identity");
    Transaction transaction(impl_->sql,impl_->db);
    Statement removeOutbox(impl_->sql,impl_->db,"DELETE FROM outbox WHERE peer=?");removeOutbox.text(1,conversation);removeOutbox.row();
    Statement removeMessages(impl_->sql,impl_->db,"DELETE FROM messages WHERE peer=?");removeMessages.text(1,conversation);removeMessages.row();
    transaction.commit();
}
std::vector<StoredMessage> Storage::pending(const std::string& conversation,int limit) {
    require(identifier(conversation)&&limit>0&&limit<=1000,"Invalid outbox page request");
    Statement s(impl_->sql,impl_->db,"SELECT m.id,m.b FROM outbox o JOIN messages m ON o.id=m.id WHERE o.peer=? ORDER BY m.rowid LIMIT ?");s.text(1,conversation);s.number(2,limit);
    std::vector<StoredMessage> result;
    while(s.row()){auto id=s.text(0);Secret plain{protect(s.bytes(1),"PeerChat.v1/message/"+id+"/"+conversation,false)};auto m=decodeMessage(plain.data);require(m.id==id,"Outbox identity mismatch");result.push_back(std::move(m));}
    return result;
}
bool Storage::appendGroupEvent(const GroupEvent& event) {
    require(identifier(event.groupId,32)&&identifier(event.eventId)&&event.logicalOrder>0&&event.logicalOrder<=static_cast<std::uint64_t>(INT64_MAX),"Invalid group event identity");
    Blob plain=encodeGroupEvent(event);Transaction transaction(impl_->sql,impl_->db);
    Statement existing(impl_->sql,impl_->db,"SELECT group_id,b FROM group_events WHERE event_id=?");existing.text(1,event.eventId);
    if(existing.row()){
        require(existing.text(0)==event.groupId,"Group event ID reused in another group");
        Secret saved{protect(existing.bytes(1),"PeerChat.v1/group-event/"+event.eventId+"/"+event.groupId,false)};
        require(saved.data==plain,"Conflicting duplicate group event ID");transaction.commit();return false;
    }
    Blob encrypted=protect(plain,"PeerChat.v1/group-event/"+event.eventId+"/"+event.groupId,true);
    Statement insert(impl_->sql,impl_->db,"INSERT INTO group_events(event_id,group_id,logical_order,b) VALUES(?,?,?,?)");insert.text(1,event.eventId);insert.text(2,event.groupId);insert.number(3,static_cast<std::int64_t>(event.logicalOrder));insert.blob(4,encrypted);insert.row();transaction.commit();return true;
}
std::vector<GroupEvent> Storage::groupEvents(const std::string& groupId) {
    require(identifier(groupId,32),"Invalid Group ID");Statement statement(impl_->sql,impl_->db,"SELECT event_id,b FROM group_events WHERE group_id=? ORDER BY logical_order,event_id");statement.text(1,groupId);std::vector<GroupEvent> events;
    while(statement.row()){std::string eventId=statement.text(0);Secret plain{protect(statement.bytes(1),"PeerChat.v1/group-event/"+eventId+"/"+groupId,false)};auto event=decodeGroupEvent(plain.data);require(event.eventId==eventId&&event.groupId==groupId,"Stored group event identity mismatch");events.push_back(std::move(event));}return events;
}
std::vector<std::string> Storage::groupIds() {
    Statement statement(impl_->sql,impl_->db,"SELECT DISTINCT group_id FROM group_events ORDER BY group_id");std::vector<std::string> groups;while(statement.row()){auto id=statement.text(0);require(identifier(id,32),"Invalid stored Group ID");groups.push_back(std::move(id));}return groups;
}
GroupAccount Storage::groupAccount(){Blob saved=impl_->get("group-account");if(saved.empty())return {};Reader reader(saved);GroupAccount account;account.accountId=reader.number();account.name=reader.text();reader.end();require(account.accountId>0&&!account.name.empty()&&account.name.size()<=64,"Invalid group account binding");return account;}
void Storage::saveGroupAccount(const GroupAccount& account){require(account.accountId>0&&!account.name.empty()&&account.name.size()<=64,"Invalid group account binding");Writer writer;writer.number(account.accountId);writer.text(account.name);impl_->put("group-account",writer.data);}
void Storage::queueGroupDelivery(const std::string& eventId,const std::string& peerId){require(identifier(eventId)&&identifier(peerId),"Invalid group delivery identity");Statement statement(impl_->sql,impl_->db,"INSERT OR IGNORE INTO group_outbox(event_id,peer_id) VALUES(?,?)");statement.text(1,eventId);statement.text(2,peerId);statement.row();}
void Storage::acknowledgeGroupDelivery(const std::string& eventId,const std::string& peerId){require(identifier(eventId)&&identifier(peerId),"Invalid group delivery acknowledgement");Statement statement(impl_->sql,impl_->db,"DELETE FROM group_outbox WHERE event_id=? AND peer_id=?");statement.text(1,eventId);statement.text(2,peerId);statement.row();}
std::vector<GroupEvent> Storage::pendingGroupEvents(const std::string& peerId,int limit){require(identifier(peerId)&&limit>0&&limit<=256,"Invalid group outbox request");Statement statement(impl_->sql,impl_->db,"SELECT e.event_id,e.group_id,e.b FROM group_outbox o JOIN group_events e ON e.event_id=o.event_id WHERE o.peer_id=? ORDER BY e.logical_order,e.event_id LIMIT ?");statement.text(1,peerId);statement.number(2,limit);std::vector<GroupEvent> events;while(statement.row()){auto eventId=statement.text(0),groupId=statement.text(1);Secret plain{protect(statement.bytes(2),"PeerChat.v1/group-event/"+eventId+"/"+groupId,false)};auto event=decodeGroupEvent(plain.data);require(event.eventId==eventId&&event.groupId==groupId,"Group outbox identity mismatch");events.push_back(std::move(event));}return events;}
}
