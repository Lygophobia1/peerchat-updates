#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "storage.hpp"
#include "group.hpp"
#include "mailbox_protocol.hpp"
#include "platform/identity_provider.hpp"
#include "platform/storage_backend.hpp"
#include "portable/utf8.hpp"
#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#include "platform/windows_record_protection.hpp"
#include "platform/windows_sql_database.hpp"
#include <bcrypt.h>
#endif
#if !defined(_WIN32) || defined(PEERCHAT_PORTABLE_CRYPTO)
#include "platform/portable_crypto.hpp"
#endif
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace peerchat {
namespace storage_detail {
constexpr int MaxRecord=1024*1024;
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
#if defined(_WIN32)
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
#else
std::string utf8(const std::wstring& value){require(unicode::utf16Units(value)<=32768,"Text field too long");return unicode::encode(value);}
std::wstring wide(const std::string& value){return unicode::decode(value);}
#endif
void wipe(void* value,size_t length){
#if defined(_WIN32)
    SecureZeroMemory(value,length);
#else
    platform::portable_crypto::wipe(value,length);
#endif
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
    ~Writer(){wipe(data.data(),data.size());}
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
    ~Secret(){wipe(data.data(),data.size());}
};
#if defined(_WIN32)
Blob protect(const Blob& plain,const std::string& context,bool encrypt) {
    platform::WindowsRecordProtector provider;
    return encrypt?provider.protect(plain,context):provider.unprotect(plain,context);
}
struct Algorithm {BCRYPT_ALG_HANDLE h{};~Algorithm(){if(h)BCryptCloseAlgorithmProvider(h,0);}};
struct Key {BCRYPT_KEY_HANDLE h{};~Key(){if(h)BCryptDestroyKey(h);}};
Blob exportKey(BCRYPT_KEY_HANDLE key,LPCWSTR kind) {
    ULONG size=0;crypto(BCryptExportKey(key,nullptr,kind,nullptr,0,&size,0));Blob b(size);
    crypto(BCryptExportKey(key,nullptr,kind,b.data(),size,&size,0));return b;
}
#endif
void validateProfile(const Profile& p) {
    require(!p.name.empty()&&unicode::utf16Units(p.name)<=64&&p.listenPort!=0,"Invalid profile settings");
    require(p.privateKey.size()<=1024&&p.publicKey.size()<=1024,"Invalid identity size");
    if(p.identity){
        require(p.privateKey.empty()&&p.identity->publicKey()==p.publicKey&&Storage::publicKeyId(p.publicKey)==p.peerId,"Opaque profile identity mismatch");
        const Blob proof{'P','e','e','r','C','h','a','t',' ','p','r','o','f','i','l','e',' ','b','i','n','d','i','n','g'};
        require(verifyDevicePayload(proof,p.identity->sign(proof),p.publicKey),"Opaque profile signing failed");
        return;
    }
#if defined(_WIN32)
    Algorithm alg;Key key;crypto(BCryptOpenAlgorithmProvider(&alg.h,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0));
    crypto(BCryptImportKeyPair(alg.h,nullptr,BCRYPT_ECCPRIVATE_BLOB,&key.h,const_cast<PUCHAR>(p.privateKey.data()),static_cast<ULONG>(p.privateKey.size()),0));
    require(exportKey(key.h,BCRYPT_ECCPUBLIC_BLOB)==p.publicKey&&Storage::publicKeyId(p.publicKey)==p.peerId,"Identity key mismatch");
#else
    throw std::runtime_error("Opaque identity provider required");
#endif
}

#if defined(_WIN32)
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
// Windows owns path resolution and the SQLite handle. Storage owns schema and
// record semantics. Keeping construction here permits independent platform
// opening tests before the SQL execution path is changed.
struct WindowsStorageConnection {
    Sqlite api;sqlite3* handle{};
    explicit WindowsStorageConnection(const std::wstring& directory){
        std::filesystem::create_directories(directory);
        auto path=(std::filesystem::path(directory)/L"peerchat.sqlite").wstring();
        if(api.open16(path.c_str(),&handle)!=0){if(handle)api.close(handle);handle=nullptr;throw std::runtime_error("Cannot open local database");}
        if(api.busy_timeout(handle,1500)!=0){api.close(handle);handle=nullptr;throw std::runtime_error("Cannot configure database timeout");}
    }
    ~WindowsStorageConnection(){if(handle)api.close(handle);}
    WindowsStorageConnection(const WindowsStorageConnection&)=delete;
    WindowsStorageConnection& operator=(const WindowsStorageConnection&)=delete;
};
struct WindowsStatement {
    Sqlite& api;sqlite3_stmt* handle{};
    WindowsStatement(Sqlite& a,sqlite3* db,const char* sql):api(a){require(api.prepare_v2(db,sql,-1,&handle,nullptr)==0,"Local database statement failed");}
    ~WindowsStatement(){if(handle)api.finalize(handle);}
    void text(int i,const std::string& s){require(api.bind_text(handle,i,s.data(),static_cast<int>(s.size()),reinterpret_cast<void(*)(void*)>(-1))==0,"Database text binding failed");}
    void blob(int i,const Blob& b){require(api.bind_blob(handle,i,b.data(),static_cast<int>(b.size()),reinterpret_cast<void(*)(void*)>(-1))==0,"Database blob binding failed");}
    void number(int i,std::int64_t n){require(api.bind_int64(handle,i,n)==0,"Database number binding failed");}
    bool row(){int result=api.step(handle);require(result==100||result==101,"Local database read/write failed");return result==100;}
    Blob bytes(int i){int n=api.column_bytes(handle,i);require(n>0&&n<=MaxRecord,"Invalid encrypted record size");const auto* b=static_cast<const unsigned char*>(api.column_blob(handle,i));require(b!=nullptr,"Invalid stored blob");return {b,b+n};}
    std::string text(int i){const auto* s=api.column_text(handle,i);require(s!=nullptr,"Invalid stored identifier");return reinterpret_cast<const char*>(s);}
};
// Execution adapter only: schema, record encryption, history and outbox policy
// remain in Storage.
struct WindowsSqlStatement final : platform::SqlStatement {
    WindowsStatement statement;
    WindowsSqlStatement(Sqlite& api,sqlite3* db,const std::string& sql):statement(api,db,sql.c_str()){}
    void bindText(int i,const std::string& value) override{statement.text(i,value);}
    void bindBlob(int i,const Blob& value) override{statement.blob(i,value);}
    void bindNumber(int i,std::int64_t value) override{statement.number(i,value);}
    bool row() override{return statement.row();}
    Blob bytes(int i) override{return statement.bytes(i);}
    std::string text(int i) override{return statement.text(i);}
    std::int64_t number(int i) override{return statement.api.column_int64(statement.handle,i);}
};
struct WindowsSqlDatabase final : platform::SqlDatabase {
    WindowsStorageConnection connection;
    Sqlite& api;sqlite3*& handle;
    explicit WindowsSqlDatabase(const std::wstring& directory):connection(directory),api(connection.api),handle(connection.handle){}
    void execute(const std::string& sql) override{require(api.exec(handle,sql.c_str(),nullptr,nullptr,nullptr)==0,"Local database operation failed");}
    std::unique_ptr<platform::SqlStatement> prepare(const std::string& sql) override{return std::make_unique<WindowsSqlStatement>(api,handle,sql);}
};
#endif
struct Statement : platform::CheckedSqlStatement {
    using platform::CheckedSqlStatement::CheckedSqlStatement;
};
struct Transaction : platform::SqlTransaction {
    using platform::SqlTransaction::SqlTransaction;
};
StoredMessage decodeMessage(const Blob& plain) {
    Reader r(plain);StoredMessage m;m.id=r.string();m.sender=r.string();m.recipient=r.string();m.text=r.text();m.utcMilliseconds=r.number();auto state=r.number();
    // v0.2 encrypted records end after delivery. New records append the logical
    // position; accepting the old shape keeps existing history readable.
    if(r.remaining()>=8)m.logicalOrder=r.number();
    r.end();
    require(identifier(m.id,32)&&identifier(m.sender)&&identifier(m.recipient)&&state<=3&&!m.text.empty()&&unicode::utf16Units(m.text)<=16384,"Invalid stored message");m.delivery=static_cast<Delivery>(state);return m;
}
void encodeMessage(Writer& w,const StoredMessage& m){w.string(m.id);w.string(m.sender);w.string(m.recipient);w.text(m.text);w.number(m.utcMilliseconds);w.number(static_cast<unsigned char>(m.delivery));w.number(m.logicalOrder);}
}

using namespace storage_detail;
struct Storage::Impl {
    std::shared_ptr<platform::SqlDatabase> database;
    std::shared_ptr<platform::RecordProtector> records;
    std::shared_ptr<platform::IdentityProvider> identity;
    platform::SqlDatabase& sql;
    static platform::SqlDatabase& checked(const std::shared_ptr<platform::SqlDatabase>& value){
        require(value!=nullptr,"Storage database provider missing");return *value;
    }
    explicit Impl(std::shared_ptr<platform::SqlDatabase> value,std::shared_ptr<platform::RecordProtector> protection):database(std::move(value)),records(std::move(protection)),sql(checked(database)) {
            require(records!=nullptr,"Storage record provider missing");
            execute("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA temp_store=MEMORY; PRAGMA secure_delete=ON;");
            execute("CREATE TABLE IF NOT EXISTS objects(k TEXT PRIMARY KEY,b BLOB NOT NULL);"
                    "CREATE TABLE IF NOT EXISTS messages(id TEXT UNIQUE NOT NULL,peer TEXT NOT NULL,b BLOB NOT NULL);"
                    "CREATE INDEX IF NOT EXISTS conversation_index ON messages(peer);");
            // Cursor metadata only. AUTOINCREMENT never reuses a deleted history
            // position; the ordinary rowid/UI history semantics remain unchanged.
            execute("CREATE TABLE IF NOT EXISTS message_archive_index(sequence INTEGER PRIMARY KEY AUTOINCREMENT,message_id TEXT UNIQUE NOT NULL);"
                    "CREATE TRIGGER IF NOT EXISTS message_archive_insert AFTER INSERT ON messages BEGIN INSERT INTO message_archive_index(message_id) VALUES(NEW.id); END;"
                    "CREATE TRIGGER IF NOT EXISTS message_archive_delete AFTER DELETE ON messages BEGIN DELETE FROM message_archive_index WHERE message_id=OLD.id; END;"
                    "INSERT INTO message_archive_index(message_id) SELECT m.id FROM messages m WHERE NOT EXISTS(SELECT 1 FROM message_archive_index a WHERE a.message_id=m.id) ORDER BY m.rowid;");
            execute("CREATE TABLE IF NOT EXISTS group_message_index(sequence INTEGER PRIMARY KEY AUTOINCREMENT,server_group TEXT NOT NULL,local_group TEXT NOT NULL,message_id TEXT NOT NULL,UNIQUE(server_group,message_id));"
                    "CREATE INDEX IF NOT EXISTS group_message_history ON group_message_index(local_group,sequence);");
            // Additive index only; names, paths, digests and provenance remain
            // in the existing platform-protected object backend.
            execute("CREATE TABLE IF NOT EXISTS attachment_history_index(sequence INTEGER PRIMARY KEY AUTOINCREMENT,attachment_id TEXT UNIQUE NOT NULL);");
            // Separate outbox index contains identifiers only, never message text.
            execute("CREATE TABLE IF NOT EXISTS outbox(id TEXT PRIMARY KEY,peer TEXT NOT NULL);"
                    "CREATE TABLE IF NOT EXISTS group_events(event_id TEXT PRIMARY KEY,group_id TEXT NOT NULL,logical_order INTEGER NOT NULL,b BLOB NOT NULL);"
                    "CREATE INDEX IF NOT EXISTS group_event_order ON group_events(group_id,logical_order,event_id);"
                    "CREATE TABLE IF NOT EXISTS group_outbox(event_id TEXT NOT NULL REFERENCES group_events(event_id) ON DELETE CASCADE,peer_id TEXT NOT NULL,PRIMARY KEY(event_id,peer_id));"
                    "CREATE INDEX IF NOT EXISTS group_outbox_peer ON group_outbox(peer_id,event_id);"
                    "CREATE TABLE IF NOT EXISTS group_key_outbox(id TEXT PRIMARY KEY,group_id TEXT NOT NULL,key_generation INTEGER NOT NULL,recipient_peer TEXT NOT NULL,b BLOB NOT NULL);"
                    "CREATE INDEX IF NOT EXISTS group_key_outbox_recipient ON group_key_outbox(recipient_peer,id);"
                    "CREATE TABLE IF NOT EXISTS group_key_receipts(group_id TEXT NOT NULL,key_generation INTEGER NOT NULL,recipient_peer TEXT NOT NULL,PRIMARY KEY(group_id,key_generation,recipient_peer));");
    }
    void execute(const char* statement){sql.execute(statement);}
    Blob protect(const Blob& value,const std::string& context,bool encrypt){
        auto result=encrypt?records->protect(value,context):records->unprotect(value,context);
        require(!result.empty()&&result.size()<=MaxRecord,"Invalid protected record provider result");
        return result;
    }
    Blob get(const std::string& key) {
        Statement s(sql,"SELECT b FROM objects WHERE k=?");s.text(1,key);if(!s.row())return {};
        return protect(s.bytes(0),"PeerChat.v1/"+key,false);
    }
    void put(const std::string& key,const Blob& plain) {
        Blob encrypted=protect(plain,"PeerChat.v1/"+key,true);
        Statement s(sql,"INSERT INTO objects(k,b) VALUES(?,?) ON CONFLICT(k) DO UPDATE SET b=excluded.b");s.text(1,key);s.blob(2,encrypted);s.row();
    }
    void erase(const std::string& key) {
        Statement statement(sql,"DELETE FROM objects WHERE k=?");statement.text(1,key);statement.row();
    }
};
#if defined(_WIN32)
std::shared_ptr<platform::SqlDatabase> platform::openWindowsSqlDatabase(const std::wstring& directory){return std::make_shared<WindowsSqlDatabase>(directory);}
Storage::Storage(const std::wstring& directory):Storage(std::make_shared<WindowsSqlDatabase>(directory)){}
Storage::Storage(std::shared_ptr<platform::SqlDatabase> database):Storage(std::move(database),std::make_shared<platform::WindowsRecordProtector>()){}
#else
Storage::Storage(const std::wstring&){throw std::runtime_error("Storage platform providers required");}
Storage::Storage(std::shared_ptr<platform::SqlDatabase>){throw std::runtime_error("Storage record provider required");}
#endif
Storage::Storage(std::shared_ptr<platform::SqlDatabase> database,std::shared_ptr<platform::RecordProtector> records):impl_(std::make_unique<Impl>(std::move(database),std::move(records))){}
Storage::Storage(std::shared_ptr<platform::StoragePlatform> platform){
    require(platform&&platform->database&&platform->records&&platform->identity,"Incomplete opaque storage platform");
    impl_=std::make_unique<Impl>(platform->database,platform->records);
    impl_->identity=platform->identity;
}
Storage::~Storage()=default;
Blob Storage::archiveAuthorizationState(){return impl_->get("archive-authorization/v1");}
void Storage::saveArchiveAuthorizationState(const Blob& expected,const Blob& next){
    require(!next.empty()&&next.size()<=65536,"Invalid archive authorization state");
    Transaction transaction(impl_->sql);
    require(impl_->get("archive-authorization/v1")==expected,"Archive authorization changed concurrently");
    impl_->put("archive-authorization/v1",next);transaction.commit();
}
std::string Storage::newMessageId(){
#if defined(_WIN32) && !defined(PEERCHAT_PORTABLE_CRYPTO)
    Blob id(16);crypto(BCryptGenRandom(nullptr,id.data(),16,BCRYPT_USE_SYSTEM_PREFERRED_RNG));
#else
    Blob id=platform::portable_crypto::random(16);
#endif
    id[6]=(id[6]&15)|64;id[8]=(id[8]&63)|128;return hex(id);
}
std::string Storage::publicKeyId(const Blob& key) {
#if defined(_WIN32) && !defined(PEERCHAT_PORTABLE_CRYPTO)
    Algorithm alg;crypto(BCryptOpenAlgorithmProvider(&alg.h,BCRYPT_SHA256_ALGORITHM,nullptr,0));
    BCRYPT_HASH_HANDLE hash{};crypto(BCryptCreateHash(alg.h,&hash,nullptr,0,nullptr,0,0));Blob digest(32);
    NTSTATUS status=BCryptHashData(hash,const_cast<PUCHAR>(key.data()),static_cast<ULONG>(key.size()),0);
    if(status>=0)status=BCryptFinishHash(hash,digest.data(),32,0);
    BCryptDestroyHash(hash);crypto(status);return hex(digest);
#else
    return hex(platform::portable_crypto::sha256(key));
#endif
}
Profile Storage::profile() {
    Transaction transaction(impl_->sql);
    Secret saved{impl_->get("profile")};Profile p;
    if(saved.data.empty()) {
        if(impl_->identity){
            p.identity=impl_->identity;p.publicKey=p.identity->publicKey();p.peerId=publicKeyId(p.publicKey);
            saveProfile(p);transaction.commit();return p;
        }
#if defined(_WIN32)
        Algorithm alg;Key key;crypto(BCryptOpenAlgorithmProvider(&alg.h,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0));
        crypto(BCryptGenerateKeyPair(alg.h,&key.h,256,0));crypto(BCryptFinalizeKeyPair(key.h,0));
        p.publicKey=exportKey(key.h,BCRYPT_ECCPUBLIC_BLOB);p.privateKey=exportKey(key.h,BCRYPT_ECCPRIVATE_BLOB);p.peerId=publicKeyId(p.publicKey);
        saveProfile(p);transaction.commit();return p;
#else
        throw std::runtime_error("Opaque identity provider required");
#endif
    }
    Reader r(saved.data);p.peerId=r.string();p.name=r.text();auto port=r.number();require(port>0&&port<=65535,"Invalid stored listen port");p.listenPort=static_cast<std::uint16_t>(port);
    p.publicKey=r.bytes();p.privateKey=r.bytes();r.end();p.identity=impl_->identity;validateProfile(p);transaction.commit();return p;
}
void Storage::saveProfile(const Profile& p) {
    require(p.identity==impl_->identity,"Profile identity provider does not belong to this store");
    validateProfile(p);Writer w;w.string(p.peerId);w.text(p.name);w.number(p.listenPort);w.bytes(p.publicKey);w.bytes(p.privateKey);impl_->put("profile",w.data);
}
void Storage::saveContact(const Contact& c) {
    require(identifier(c.peerId)&&(c.publicKey.empty()||publicKeyId(c.publicKey)==c.peerId)&&!c.name.empty()&&unicode::utf16Units(c.name)<=64,"Invalid contact identity");
    require(c.endpoints.size()<=128,"Too many endpoints for one contact");Writer w;w.string(c.peerId);w.text(c.name);w.bytes(c.publicKey);w.number(c.verified);w.number(c.endpoints.size());
    for(const auto& e:c.endpoints){require(!e.host.empty()&&unicode::utf16Units(e.host)<=253&&e.port,"Invalid endpoint");w.text(e.host);w.number(e.port);}
    impl_->put("contact/"+c.peerId,w.data);
}
void Storage::removeContact(const std::string& peerId) {
    require(identifier(peerId),"Invalid contact identity");
    impl_->erase("contact/"+peerId);
}
std::vector<Contact> Storage::contacts() {
    Statement s(impl_->sql,"SELECT k,b FROM objects WHERE k LIKE 'contact/%' ORDER BY k");std::vector<Contact> result;
    while(s.row()) {
        std::string key=s.text(0);Secret plain{impl_->protect(s.bytes(1),"PeerChat.v1/"+key,false)};Reader r(plain.data);Contact c;
        c.peerId=r.string();c.name=r.text();c.publicKey=r.bytes();auto trust=r.number(),count=r.number();
        require(trust<=1&&count<=128&&key=="contact/"+c.peerId&&(c.publicKey.empty()||publicKeyId(c.publicKey)==c.peerId),"Invalid contact record");c.verified=trust==1;
        for(std::uint64_t i=0;i<count;i++){Endpoint e;e.host=r.text();auto port=r.number();require(port>0&&port<=65535,"Invalid contact port");e.port=static_cast<std::uint16_t>(port);c.endpoints.push_back(std::move(e));}
        r.end();result.push_back(std::move(c));
    }
    return result;
}
bool Storage::append(const std::string& conversation,const StoredMessage& m) {
    require(identifier(conversation)&&identifier(m.id,32)&&identifier(m.sender)&&identifier(m.recipient)&&
        (conversation==m.sender||conversation==m.recipient)&&!m.text.empty()&&unicode::utf16Units(m.text)<=16384&&static_cast<unsigned char>(m.delivery)<=3,"Invalid message");
    Transaction transaction(impl_->sql);
    Statement existing(impl_->sql,"SELECT peer,b FROM messages WHERE id=?");existing.text(1,m.id);
    if(existing.row()) {
        require(existing.text(0)==conversation,"Message ID reused in another conversation");
        Secret plain{impl_->protect(existing.bytes(1),"PeerChat.v1/message/"+m.id+"/"+conversation,false)};auto old=decodeMessage(plain.data);
        require(old.id==m.id&&old.sender==m.sender&&old.recipient==m.recipient&&old.text==m.text&&old.utcMilliseconds==m.utcMilliseconds&&old.logicalOrder==m.logicalOrder,"Conflicting duplicate message ID");transaction.commit();return false;
    }
    Writer plain;encodeMessage(plain,m);Blob encrypted=impl_->protect(plain.data,"PeerChat.v1/message/"+m.id+"/"+conversation,true);
    Statement insert(impl_->sql,"INSERT INTO messages(id,peer,b) VALUES(?,?,?)");insert.text(1,m.id);insert.text(2,conversation);insert.blob(3,encrypted);insert.row();
    if(m.delivery!=Delivery::Delivered){Statement pending(impl_->sql,"INSERT INTO outbox(id,peer) VALUES(?,?)");pending.text(1,m.id);pending.text(2,conversation);pending.row();}
    transaction.commit();return true;
}
std::optional<StoredMessage> Storage::message(const std::string& id){
    require(identifier(id,32),"Invalid message identity");Statement query(impl_->sql,"SELECT peer,b FROM messages WHERE id=?");query.text(1,id);
    if(!query.row())return std::nullopt;
    auto peer=query.text(0);Secret plain{impl_->protect(query.bytes(1),"PeerChat.v1/message/"+id+"/"+peer,false)};
    auto message=decodeMessage(plain.data);require(message.id==id,"Stored message ID mismatch");return message;
}
bool Storage::appendArchiveHistory(const std::string& conversation,const StoredMessage& original,
                                  const Blob& assertion,bool recipientAlias){
    require(identifier(conversation)&&identifier(original.id,32)&&identifier(original.sender)&&identifier(original.recipient)&&
        original.sender!=original.recipient&&(conversation==original.sender||conversation==original.recipient)&&
        !original.text.empty()&&unicode::utf16Units(original.text)<=16384&&!assertion.empty()&&assertion.size()<=70000,
        "Invalid verified archive record");
    Transaction transaction(impl_->sql);
    auto existing=message(original.id);
    if(existing){
        require(existing->sender==original.sender&&(existing->recipient==original.recipient||recipientAlias)&&
            existing->text==original.text&&existing->utcMilliseconds==original.utcMilliseconds&&
            existing->logicalOrder==original.logicalOrder,"Conflicting archived logical message");
        // Preserve the first durable body and real local ACK/outbox state.
        if(impl_->get("archive-history-proof/"+original.id).empty())impl_->put("archive-history-proof/"+original.id,assertion);
        transaction.commit();return false;
    }
    auto imported=original;imported.delivery=Delivery::Sent; // Not a fabricated recipient ACK.
    Writer plain;encodeMessage(plain,imported);
    auto encrypted=impl_->protect(plain.data,"PeerChat.v1/message/"+original.id+"/"+conversation,true);
    Statement insert(impl_->sql,"INSERT INTO messages(id,peer,b) VALUES(?,?,?)");
    insert.text(1,original.id);insert.text(2,conversation);insert.blob(3,encrypted);insert.row();
    impl_->put("archive-history-proof/"+original.id,assertion);
    // Intentionally no outbox or mailbox upload: this is accepted history only.
    transaction.commit();return true;
}
Blob Storage::archiveHistoryProof(const std::string& id){
    require(identifier(id,32),"Invalid archive message identity");return impl_->get("archive-history-proof/"+id);
}
Blob Storage::archiveSyncState(const std::string& context){
    require(identifier(context),"Invalid archive sync context");return impl_->get("archive-sync/"+context);
}
Blob Storage::transferJournal(const std::string& device){require(identifier(device),"Invalid transfer device");return impl_->get("transfer-journal/v1/"+device);}
void Storage::saveTransferJournal(const std::string& device,const Blob& expected,const Blob& next){
    require(identifier(device)&&!next.empty()&&next.size()<=262144,"Invalid transfer journal");
    Transaction transaction(impl_->sql);require(transferJournal(device)==expected,"Transfer journal changed concurrently");
    impl_->put("transfer-journal/v1/"+device,next);transaction.commit();
}
Blob Storage::attachmentMetadata(const std::string& id){
    require(identifier(id,32),"Invalid attachment identity");return impl_->get("attachment-history/v1/"+id);
}
void Storage::saveAttachmentMetadata(const std::string& id,const Blob& expected,const Blob& record){
    require(identifier(id,32)&&!record.empty()&&record.size()<=16384,"Invalid attachment metadata");
    Transaction transaction(impl_->sql);
    require(attachmentMetadata(id)==expected,"Attachment metadata changed concurrently");
    impl_->put("attachment-history/v1/"+id,record);
    Statement index(impl_->sql,"INSERT INTO attachment_history_index(attachment_id) VALUES(?) ON CONFLICT(attachment_id) DO NOTHING");index.text(1,id);index.row();
    transaction.commit();
}
std::int64_t Storage::attachmentMetadataWatermark(){
    Statement query(impl_->sql,"SELECT seq FROM sqlite_sequence WHERE name='attachment_history_index'");
    if(!query.row())return 0;
    auto value=query.number(0);require(value>=0,"Invalid attachment watermark");return value;
}
AttachmentMetadataPage Storage::attachmentMetadataPage(std::int64_t after,std::int64_t through,int limit){
    require(after>=0&&through>=after&&through<=attachmentMetadataWatermark()&&limit>0&&limit<=8,"Invalid attachment page bounds");
    AttachmentMetadataPage page;page.next=after;page.through=through;
    Statement query(impl_->sql,"SELECT sequence,attachment_id FROM attachment_history_index WHERE sequence>? AND sequence<=? ORDER BY sequence LIMIT ?");
    query.number(1,after);query.number(2,through);query.number(3,limit+1);
    while(query.row()){
        if(page.records.size()==static_cast<size_t>(limit))return page;
        AttachmentMetadataRecord record;record.sequence=query.number(0);record.id=query.text(1);record.record=attachmentMetadata(record.id);
        require(record.sequence>page.next&&!record.record.empty()&&record.record.size()<=16384,"Invalid attachment index record");
        page.next=record.sequence;page.records.push_back(std::move(record));
    }
    page.complete=true;page.next=through;return page;
}
void Storage::saveArchiveSyncState(const std::string& context,const Blob& expected,const Blob& next){
    require(identifier(context)&&!next.empty()&&next.size()<=4096,"Invalid archive sync state");
    Transaction transaction(impl_->sql);
    require(impl_->get("archive-sync/"+context)==expected,"Archive sync changed concurrently");
    impl_->put("archive-sync/"+context,next);transaction.commit();
}
std::string Storage::archiveStoreId(){
    Transaction transaction(impl_->sql);auto saved=impl_->get("archive-store-id/v1");
    if(saved.empty()){auto id=newMessageId();saved.assign(id.begin(),id.end());impl_->put("archive-store-id/v1",saved);}
    std::string id(saved.begin(),saved.end());require(identifier(id,32),"Invalid archive store identity");transaction.commit();return id;
}
std::int64_t Storage::archiveHistoryWatermark(){
    Statement query(impl_->sql,"SELECT seq FROM sqlite_sequence WHERE name='message_archive_index'");
    if(!query.row())return 0;
    auto value=query.number(0);require(value>=0,"Invalid archive high watermark");return value;
}
ArchiveHistoryPage Storage::archiveHistoryPage(const std::string& conversation,std::int64_t after,
                                              std::int64_t through,int limit,size_t maximumBytes){
    require(identifier(conversation)&&after>=0&&through>=after&&through<=archiveHistoryWatermark()&&
        limit>0&&limit<=16&&maximumBytes>=128*1024&&maximumBytes<=512*1024,"Invalid archive cursor/page bounds");
    ArchiveHistoryPage page;page.nextCursor=after;page.throughCursor=through;size_t bytes=0;
    Statement query(impl_->sql,"SELECT a.sequence,m.id,m.b FROM message_archive_index a JOIN messages m ON m.id=a.message_id WHERE m.peer=? AND a.sequence>? AND a.sequence<=? ORDER BY a.sequence LIMIT ?");
    query.text(1,conversation);query.number(2,after);query.number(3,through);query.number(4,limit);
    while(query.row()){
        auto id=query.text(1);Secret plain{impl_->protect(query.bytes(2),"PeerChat.v1/message/"+id+"/"+conversation,false)};
        require(plain.data.size()<=maximumBytes,"Archive record exceeds page budget");
        if(bytes>maximumBytes-plain.data.size())break;
        auto m=decodeMessage(plain.data);require(m.id==id,"Stored archive message identity mismatch");
        bytes+=plain.data.size();page.nextCursor=query.number(0);page.cursors.push_back(page.nextCursor);page.messages.push_back(std::move(m));
    }
    Statement more(impl_->sql,"SELECT 1 FROM message_archive_index a JOIN messages m ON m.id=a.message_id WHERE m.peer=? AND a.sequence>? AND a.sequence<=? LIMIT 1");
    more.text(1,conversation);more.number(2,page.nextCursor);more.number(3,through);page.complete=!more.row();
    if(page.complete)page.nextCursor=through;
    return page;
}
ArchiveConversationPage Storage::archiveConversationPage(std::int64_t after,std::int64_t through,int limit){
    require(after>=0&&through>=after&&through<=archiveHistoryWatermark()&&limit>0&&limit<=16,"Invalid archive manifest bounds");
    ArchiveConversationPage page;page.nextCursor=after;page.throughCursor=through;
    // Discovery uses the first durable archive position, not a lexical message
    // ID or timestamp. SQL limits the materialized result to one bounded page.
    Statement query(impl_->sql,"SELECT m.peer,MIN(a.sequence) AS first_position FROM message_archive_index a JOIN messages m ON m.id=a.message_id WHERE a.sequence<=? GROUP BY m.peer HAVING MIN(a.sequence)>? ORDER BY first_position LIMIT ?");
    query.number(1,through);query.number(2,after);query.number(3,limit+1);
    page.complete=true;
    while(query.row()){
        if(page.conversations.size()==static_cast<size_t>(limit)){page.complete=false;break;}
        auto peer=query.text(0);require(identifier(peer),"Invalid archived conversation identity");
        auto cursor=query.number(1);require(cursor>page.nextCursor&&cursor<=through,"Invalid archived conversation cursor");
        page.nextCursor=cursor;page.cursors.push_back(cursor);page.conversations.push_back(std::move(peer));
    }
    if(page.complete)page.nextCursor=through;
    return page;
}
std::optional<std::uint64_t> Storage::historicalDeviceAccount(const std::string& device){
    require(identifier(device),"Invalid historical device identity");
    auto bytes=impl_->get("authenticated-device-account/v1/"+device);if(bytes.empty())return {};
    Reader r(bytes);require(r.string()=="PeerChat/AuthenticatedDeviceAccount/v1"&&r.string()==device,"Historical account binding mismatch");
    auto account=r.number();r.end();require(account>0,"Invalid historical account binding");return account;
}
void Storage::rememberAuthenticatedDeviceAccount(const std::string& device,std::uint64_t account){
    require(identifier(device)&&account>0,"Invalid authenticated account binding");
    auto cached=historicalDeviceAccount(device);
    if(cached){require(*cached==account,"Conflicting authenticated device ownership");return;}
    Transaction transaction(impl_->sql);auto previous=historicalDeviceAccount(device);
    require(!previous||*previous==account,"Conflicting authenticated device ownership");
    if(!previous){Writer w;w.string("PeerChat/AuthenticatedDeviceAccount/v1");w.string(device);w.number(account);impl_->put("authenticated-device-account/v1/"+device,w.data);}
    transaction.commit();
}
void Storage::acknowledge(const std::string& id,Delivery delivery) {
    require(identifier(id,32)&&static_cast<unsigned char>(delivery)<=3,"Invalid delivery acknowledgement");
    Transaction transaction(impl_->sql);
    Statement s(impl_->sql,"SELECT peer,b FROM messages WHERE id=?");s.text(1,id);if(!s.row()){transaction.commit();return;}
    std::string peer=s.text(0);Secret plain{impl_->protect(s.bytes(1),"PeerChat.v1/message/"+id+"/"+peer,false)};auto m=decodeMessage(plain.data);
    require(m.id==id,"Stored message ID mismatch");if(deliveryRank(delivery)<=deliveryRank(m.delivery)){transaction.commit();return;}m.delivery=delivery;
    Writer writer;encodeMessage(writer,m);Blob encrypted=impl_->protect(writer.data,"PeerChat.v1/message/"+id+"/"+peer,true);
    Statement update(impl_->sql,"UPDATE messages SET b=? WHERE id=?");update.blob(1,encrypted);update.text(2,id);update.row();
    if(delivery==Delivery::Delivered){Statement remove(impl_->sql,"DELETE FROM outbox WHERE id=?");remove.text(1,id);remove.row();}
    transaction.commit();
}
Blob Storage::mailboxEnvelope(const std::string& id,const std::string& device){
    require(identifier(id,32)&&identifier(device),"Invalid mailbox submission identity");
    return impl_->get("mailbox-envelope/"+id+"/"+device);
}
void Storage::saveMailboxEnvelope(const std::string& id,const std::string& device,const Blob& wire){
    auto envelope=mailbox::parse(wire);
    require(envelope.header.messageId==id&&envelope.header.recipientDevice==device,"Mailbox envelope destination mismatch");
    Transaction transaction(impl_->sql);Statement message(impl_->sql,"SELECT peer,b FROM messages WHERE id=?");message.text(1,id);
    require(message.row(),"Mailbox requires durable local message");const auto canonicalPeer=message.text(0);
    Secret plain{impl_->protect(message.bytes(1),"PeerChat.v1/message/"+id+"/"+canonicalPeer,false)};auto stored=decodeMessage(plain.data);
    // DeliveryManager authorizes every additional device against the same
    // authenticated account. History keeps its original canonical conversation.
    require(stored.sender==envelope.header.senderDevice&&stored.recipient==canonicalPeer&&stored.logicalOrder==envelope.header.logicalOrder,"Mailbox logical message mismatch");
    auto key="mailbox-envelope/"+id+"/"+device;auto previous=impl_->get(key);
    require(previous.empty()||previous==wire,"Conflicting durable mailbox envelope");
    if(previous.empty())impl_->put(key,wire);
    transaction.commit();
}
void Storage::mailboxStored(const std::string& id,const std::string& device,const Blob& expected){
    require(!expected.empty()&&mailboxEnvelope(id,device)==expected,"Mailbox store response has no matching submission");
    acknowledge(id,Delivery::Stored);
    // Mark only after real server acceptance. A crash between advancement and
    // this protected marker safely retries the identical ciphertext.
    impl_->put("mailbox-uploaded/"+id+"/"+device,expected);
}
std::vector<StoredMessage> Storage::pendingMailboxUploads(int limit){
    require(limit>0&&limit<=64,"Invalid mailbox upload page");
    Statement query(impl_->sql,"SELECT m.id,m.peer,m.b FROM messages m WHERE EXISTS(SELECT 1 FROM objects e WHERE e.k LIKE 'mailbox-envelope/'||m.id||'/%' AND NOT EXISTS(SELECT 1 FROM objects a WHERE a.k='mailbox-uploaded/'||substr(e.k,18))) ORDER BY m.rowid LIMIT ?");query.number(1,limit);
    std::vector<StoredMessage> result;
    while(query.row()){const auto id=query.text(0),peer=query.text(1);Secret plain{impl_->protect(query.bytes(2),"PeerChat.v1/message/"+id+"/"+peer,false)};auto m=decodeMessage(plain.data);require(m.id==id&&m.recipient==peer,"Mailbox upload identity mismatch");result.push_back(std::move(m));}
    return result;
}
bool Storage::mailboxUploadAccepted(const std::string& id,const std::string& device){
    require(identifier(id,32)&&identifier(device),"Invalid mailbox upload identity");auto accepted=impl_->get("mailbox-uploaded/"+id+"/"+device);
    if(accepted.empty())return false;
    require(accepted==mailboxEnvelope(id,device),"Mailbox upload acceptance mismatch");return true;
}
Blob Storage::mailboxReceipt(const std::string& id,const std::string& device,const Blob& digest){
    require(identifier(id,32)&&identifier(device)&&digest.size()==32,"Invalid mailbox receipt identity");return impl_->get("mailbox-receipt/"+id+"/"+device+"/"+hex(digest));
}
void Storage::saveMailboxReceipt(const std::string& id,const std::string& sender,const Blob& wire){
    auto receipt=mailbox::parseReceipt(wire);require(receipt.messageId==id&&receipt.senderDevice==sender,"Mailbox receipt destination mismatch");
    Transaction transaction(impl_->sql);Statement message(impl_->sql,"SELECT peer,b FROM messages WHERE id=?");message.text(1,id);
    require(message.row()&&message.text(0)==sender,"Mailbox receipt requires durable receive");
    Secret plain{impl_->protect(message.bytes(1),"PeerChat.v1/message/"+id+"/"+sender,false)};auto stored=decodeMessage(plain.data);
    require(stored.sender==sender&&stored.recipient==receipt.recipientDevice&&stored.delivery==Delivery::Delivered,"Mailbox receipt requires verified durable receive");
    auto prefix="mailbox-receipt/"+id+"/"+sender+"/";auto key=prefix+hex(receipt.envelopeDigest);auto previous=impl_->get(key);
    if(!previous.empty())require(mailbox::receiptPayload(mailbox::parseReceipt(previous))==mailbox::receiptPayload(receipt),"Conflicting mailbox receipt");
    else {
        Statement count(impl_->sql,"SELECT COUNT(*) FROM objects WHERE k LIKE ?");count.text(1,prefix+"%");require(count.row()&&count.number(0)<32,"Mailbox receipt retention limit reached");impl_->put(key,wire);
    }
    transaction.commit();
}
std::uint64_t Storage::nextLogicalOrder() {
    Transaction transaction(impl_->sql);std::uint64_t current=0;Blob saved=impl_->get("logical-clock");
    if(!saved.empty()){Reader r(saved);current=r.number();r.end();}
    require(current!=~std::uint64_t{0},"Logical message order exhausted");++current;
    Writer writer;writer.number(current);impl_->put("logical-clock",writer.data);transaction.commit();return current;
}
void Storage::observeLogicalOrder(std::uint64_t remoteOrder) {
    if(remoteOrder==0)return;
    Transaction transaction(impl_->sql);std::uint64_t current=0;Blob saved=impl_->get("logical-clock");
    if(!saved.empty()){Reader r(saved);current=r.number();r.end();}
    if(remoteOrder>current){Writer writer;writer.number(remoteOrder);impl_->put("logical-clock",writer.data);}
    transaction.commit();
}
std::vector<StoredMessage> Storage::history(const std::string& conversation,int limit,std::int64_t beforeSequence) {
    require(identifier(conversation)&&limit>0&&limit<=1000,"Invalid history page request");
    Statement s(impl_->sql,"SELECT rowid,id,b FROM messages WHERE peer=? AND rowid<? ORDER BY rowid DESC LIMIT ?");
    s.text(1,conversation);s.number(2,beforeSequence);s.number(3,limit);std::vector<StoredMessage> result;
    while(s.row()) {
        auto id=s.text(1);Secret plain{impl_->protect(s.bytes(2),"PeerChat.v1/message/"+id+"/"+conversation,false)};auto m=decodeMessage(plain.data);require(m.id==id,"Stored message ID mismatch");m.sequence=s.number(0);result.push_back(std::move(m));
    }
    std::reverse(result.begin(),result.end());return result;
}
void Storage::eraseHistory(const std::string& conversation) {
    require(identifier(conversation),"Invalid conversation identity");
    Transaction transaction(impl_->sql);
    Statement mailboxRecords(impl_->sql,"DELETE FROM objects WHERE EXISTS(SELECT 1 FROM messages m WHERE m.peer=? AND (objects.k LIKE 'mailbox-envelope/'||m.id||'/%' OR objects.k LIKE 'mailbox-uploaded/'||m.id||'/%' OR objects.k LIKE 'mailbox-receipt/'||m.id||'/%' OR objects.k='archive-history-proof/'||m.id))");
    mailboxRecords.text(1,conversation);mailboxRecords.row();
    Statement removeOutbox(impl_->sql,"DELETE FROM outbox WHERE peer=?");removeOutbox.text(1,conversation);removeOutbox.row();
    Statement removeMessages(impl_->sql,"DELETE FROM messages WHERE peer=?");removeMessages.text(1,conversation);removeMessages.row();
    transaction.commit();
}
std::vector<StoredMessage> Storage::pending(const std::string& conversation,int limit) {
    require(identifier(conversation)&&limit>0&&limit<=1000,"Invalid outbox page request");
    Statement s(impl_->sql,"SELECT m.id,m.b FROM outbox o JOIN messages m ON o.id=m.id WHERE o.peer=? ORDER BY m.rowid LIMIT ?");s.text(1,conversation);s.number(2,limit);
    std::vector<StoredMessage> result;
    while(s.row()){auto id=s.text(0);Secret plain{impl_->protect(s.bytes(1),"PeerChat.v1/message/"+id+"/"+conversation,false)};auto m=decodeMessage(plain.data);require(m.id==id,"Outbox identity mismatch");result.push_back(std::move(m));}
    return result;
}
bool Storage::appendGroupEvent(const GroupEvent& event) {
    require(identifier(event.groupId,32)&&identifier(event.eventId)&&event.logicalOrder>0&&event.logicalOrder<=static_cast<std::uint64_t>(INT64_MAX),"Invalid group event identity");
    Blob plain=encodeGroupEvent(event);Transaction transaction(impl_->sql);
    Statement existing(impl_->sql,"SELECT group_id,b FROM group_events WHERE event_id=?");existing.text(1,event.eventId);
    if(existing.row()){
        require(existing.text(0)==event.groupId,"Group event ID reused in another group");
        Secret saved{impl_->protect(existing.bytes(1),"PeerChat.v1/group-event/"+event.eventId+"/"+event.groupId,false)};
        require(saved.data==plain,"Conflicting duplicate group event ID");transaction.commit();return false;
    }
    Blob encrypted=impl_->protect(plain,"PeerChat.v1/group-event/"+event.eventId+"/"+event.groupId,true);
    Statement insert(impl_->sql,"INSERT INTO group_events(event_id,group_id,logical_order,b) VALUES(?,?,?,?)");insert.text(1,event.eventId);insert.text(2,event.groupId);insert.number(3,static_cast<std::int64_t>(event.logicalOrder));insert.blob(4,encrypted);insert.row();transaction.commit();return true;
}
std::vector<GroupEvent> Storage::groupEvents(const std::string& groupId) {
    require(identifier(groupId,32),"Invalid Group ID");Statement statement(impl_->sql,"SELECT event_id,b FROM group_events WHERE group_id=? ORDER BY logical_order,event_id");statement.text(1,groupId);std::vector<GroupEvent> events;
    while(statement.row()){std::string eventId=statement.text(0);Secret plain{impl_->protect(statement.bytes(1),"PeerChat.v1/group-event/"+eventId+"/"+groupId,false)};auto event=decodeGroupEvent(plain.data);require(event.eventId==eventId&&event.groupId==groupId,"Stored group event identity mismatch");events.push_back(std::move(event));}return events;
}
std::vector<std::string> Storage::groupIds() {
    Statement statement(impl_->sql,"SELECT DISTINCT group_id FROM group_events ORDER BY group_id");std::vector<std::string> groups;while(statement.row()){auto id=statement.text(0);require(identifier(id,32),"Invalid stored Group ID");groups.push_back(std::move(id));}return groups;
}
GroupAccount Storage::groupAccount(){Blob saved=impl_->get("group-account");if(saved.empty())return {};Reader reader(saved);GroupAccount account;account.accountId=reader.number();account.name=reader.text();reader.end();require(account.accountId>0&&!account.name.empty()&&unicode::utf16Units(account.name)<=64,"Invalid group account binding");return account;}
void Storage::saveGroupAccount(const GroupAccount& account){require(account.accountId>0&&!account.name.empty()&&unicode::utf16Units(account.name)<=64,"Invalid group account binding");Writer writer;writer.number(account.accountId);writer.text(account.name);impl_->put("group-account",writer.data);}
void Storage::queueGroupDelivery(const std::string& eventId,const std::string& peerId){require(identifier(eventId)&&identifier(peerId),"Invalid group delivery identity");Statement statement(impl_->sql,"INSERT OR IGNORE INTO group_outbox(event_id,peer_id) VALUES(?,?)");statement.text(1,eventId);statement.text(2,peerId);statement.row();}
void Storage::acknowledgeGroupDelivery(const std::string& eventId,const std::string& peerId){require(identifier(eventId)&&identifier(peerId),"Invalid group delivery acknowledgement");Statement statement(impl_->sql,"DELETE FROM group_outbox WHERE event_id=? AND peer_id=?");statement.text(1,eventId);statement.text(2,peerId);statement.row();}
std::vector<GroupEvent> Storage::pendingGroupEvents(const std::string& peerId,int limit){require(identifier(peerId)&&limit>0&&limit<=256,"Invalid group outbox request");Statement statement(impl_->sql,"SELECT e.event_id,e.group_id,e.b FROM group_outbox o JOIN group_events e ON e.event_id=o.event_id WHERE o.peer_id=? ORDER BY e.logical_order,e.event_id LIMIT ?");statement.text(1,peerId);statement.number(2,limit);std::vector<GroupEvent> events;while(statement.row()){auto eventId=statement.text(0),groupId=statement.text(1);Secret plain{impl_->protect(statement.bytes(2),"PeerChat.v1/group-event/"+eventId+"/"+groupId,false)};auto event=decodeGroupEvent(plain.data);require(event.eventId==eventId&&event.groupId==groupId,"Group outbox identity mismatch");events.push_back(std::move(event));}return events;}
void Storage::saveGroupKey(const std::string& groupId,std::uint64_t generation,const Blob& key){
    require(identifier(groupId,32)&&generation>0&&generation<=INT64_MAX&&key.size()==32,"Invalid group key identity");
    std::string record="group-key/"+groupId+"/"+std::to_string(generation);
    Secret saved{impl_->get(record)};
    if(!saved.data.empty()){require(saved.data==key,"Conflicting group key generation");return;}
    impl_->put(record,key);
}
Blob Storage::groupKey(const std::string& groupId,std::uint64_t generation){
    require(identifier(groupId,32)&&generation>0&&generation<=INT64_MAX,"Invalid group key request");
    Blob key=impl_->get("group-key/"+groupId+"/"+std::to_string(generation));
    require(key.empty()||key.size()==32,"Invalid protected group key");return key;
}
bool Storage::appendAuthenticatedGroupMessage(const std::string& group,const std::string& id,const Blob& envelope,const Blob& plaintext,const std::string& local){
    require(identifier(group,32)&&identifier(id,32)&&envelope.size()>=240&&envelope.size()<=70000&&plaintext.size()<=65536,
        "Invalid authenticated group message");
    const std::string record="authenticated-group-message/"+group+"/"+id;
    Transaction transaction(impl_->sql);Secret old{impl_->get(record)};
    if(!old.data.empty()){
        Reader reader(old.data);auto previous=reader.bytes();Secret previousText{reader.bytes()};reader.end();
        require(previous==envelope&&previousText.data==plaintext,"Conflicting authenticated group message ID");
        transaction.commit();return false;
    }
    Writer writer;writer.bytes(envelope);writer.bytes(plaintext);impl_->put(record,writer.data);
    if(!local.empty()){
        require(identifier(local,32),"Invalid local group history identity");
        Statement index(impl_->sql,"INSERT INTO group_message_index(server_group,local_group,message_id) VALUES(?,?,?)");
        index.text(1,group);index.text(2,local);index.text(3,id);index.row();
    }
    transaction.commit();return true;
}
AuthenticatedGroupMessage Storage::authenticatedGroupMessage(const std::string& group,const std::string& id){
    require(identifier(group,32)&&identifier(id,32),"Invalid authenticated group history request");
    Secret saved{impl_->get("authenticated-group-message/"+group+"/"+id)};
    if(saved.data.empty())return {};
    Reader reader(saved.data);AuthenticatedGroupMessage result;result.envelope=reader.bytes();result.plaintext=reader.bytes();reader.end();
    require(result.envelope.size()<=70000&&result.plaintext.size()<=65536,"Invalid authenticated group history record");
    Statement index(impl_->sql,"SELECT sequence FROM group_message_index WHERE server_group=? AND message_id=?");index.text(1,group);index.text(2,id);if(index.row())result.sequence=index.number(0);
    Secret status{impl_->get("group-message-status/"+group+"/"+id)};if(!status.data.empty()){Reader fields(status.data);result.status=fields.string();if(fields.remaining())fields.number();fields.end();}return result;
}
std::vector<AuthenticatedGroupMessage> Storage::authenticatedGroupHistory(const std::string& local,int limit,std::int64_t before){
    require(identifier(local,32)&&limit>0&&limit<=100&&before>0,"Invalid group history page");
    Statement query(impl_->sql,"SELECT server_group,message_id,sequence FROM group_message_index WHERE local_group=? AND sequence<? ORDER BY sequence DESC LIMIT ?");
    query.text(1,local);query.number(2,before);query.number(3,limit);std::vector<AuthenticatedGroupMessage> page;
    while(query.row()){
        auto group=query.text(0),id=query.text(1);auto item=authenticatedGroupMessage(group,id);item.sequence=query.number(2);
        page.push_back(std::move(item));
    }
    std::reverse(page.begin(),page.end());return page;
}
bool Storage::setGroupMessageStatus(const std::string& group,const std::string& id,const std::string& status,std::uint32_t stage){
    require(identifier(group,32)&&identifier(id,32)&&status.size()<=160&&stage<=2,"Invalid group delivery status");
    Secret saved{impl_->get("authenticated-group-message/"+group+"/"+id)};require(!saved.data.empty(),"Unknown group message status");
    const auto key="group-message-status/"+group+"/"+id;Secret previous{impl_->get(key)};
    if(!previous.data.empty()){Reader reader(previous.data);reader.string();auto prior=reader.remaining()?reader.number():0;reader.end();if(prior>stage)return false;}
    // 0 local/uncertain, 1 server admission, 2 recipient validation report.
    // A late admission/disconnect cannot overwrite a known recipient receipt.
    Writer writer;writer.string(status);writer.number(stage);impl_->put(key,writer.data);return true;
}
bool Storage::reserveGroupNonce(const std::string& fingerprint,const Blob& nonce){
    require(identifier(fingerprint)&&nonce.size()==12,"Invalid group nonce reservation");
    const std::string scope="group-nonce/"+fingerprint,record=scope+"/"+hex(nonce);
    Transaction transaction(impl_->sql);Secret previous{impl_->get(record)};
    if(!previous.data.empty()){transaction.commit();return false;}
    Secret savedCount{impl_->get(scope+"/count")};std::uint64_t count=0;
    if(!savedCount.data.empty()){Reader reader(savedCount.data);count=reader.number();reader.end();}
    // Conservative per-device key usage ceiling for random 96-bit GCM nonces.
    require(count<(1u<<24),"Group encryption key usage limit reached");
    Writer writer;writer.number(count+1);impl_->put(scope+"/count",writer.data);impl_->put(record,Blob{1});
    transaction.commit();return true;
}
void Storage::queueGroupKeyHandoff(const GroupKeyHandoff& handoff){
    require(identifier(handoff.id,32)&&identifier(handoff.groupId,32)&&identifier(handoff.serverGroupId,32)&&identifier(handoff.distributorPeerId)&&identifier(handoff.recipientPeerId)&&handoff.distributorPeerId!=handoff.recipientPeerId&&handoff.rosterGeneration>0&&handoff.keyGeneration==handoff.rosterGeneration&&handoff.keyGeneration<=INT64_MAX&&handoff.contentKey.size()==32,"Invalid group key handoff");
    Writer writer;writer.string(handoff.id);writer.string(handoff.groupId);writer.string(handoff.serverGroupId);writer.string(handoff.distributorPeerId);writer.string(handoff.recipientPeerId);writer.number(handoff.rosterGeneration);writer.number(handoff.keyGeneration);writer.bytes(handoff.contentKey);
    std::string context="PeerChat.v1/group-key-handoff/"+handoff.id+"/"+handoff.recipientPeerId;
    Transaction transaction(impl_->sql);
    Statement previous(impl_->sql,"SELECT group_id,recipient_peer,b FROM group_key_outbox WHERE id=?");previous.text(1,handoff.id);
    if(previous.row()){require(previous.text(0)==handoff.groupId&&previous.text(1)==handoff.recipientPeerId,"Group key handoff ID reused for another destination");Secret saved{impl_->protect(previous.bytes(2),context,false)};require(saved.data==writer.data,"Conflicting group key handoff ID");transaction.commit();return;}
    Blob protectedRecord=impl_->protect(writer.data,context,true);
    Statement insert(impl_->sql,"INSERT INTO group_key_outbox(id,group_id,key_generation,recipient_peer,b) VALUES(?,?,?,?,?)");insert.text(1,handoff.id);insert.text(2,handoff.groupId);insert.number(3,static_cast<std::int64_t>(handoff.keyGeneration));insert.text(4,handoff.recipientPeerId);insert.blob(5,protectedRecord);insert.row();transaction.commit();
}
std::vector<GroupKeyHandoff> Storage::pendingGroupKeyHandoffs(const std::string& recipientPeerId,int limit){
    require(identifier(recipientPeerId)&&limit>0&&limit<=256,"Invalid group key outbox request");
    Statement statement(impl_->sql,"SELECT id,b FROM group_key_outbox WHERE recipient_peer=? ORDER BY rowid LIMIT ?");statement.text(1,recipientPeerId);statement.number(2,limit);
    std::vector<GroupKeyHandoff> pending;
    while(statement.row()){std::string id=statement.text(0);Secret plain{impl_->protect(statement.bytes(1),"PeerChat.v1/group-key-handoff/"+id+"/"+recipientPeerId,false)};Reader reader(plain.data);GroupKeyHandoff offer;offer.id=reader.string();offer.groupId=reader.string();offer.serverGroupId=reader.string();offer.distributorPeerId=reader.string();offer.recipientPeerId=reader.string();offer.rosterGeneration=reader.number();offer.keyGeneration=reader.number();offer.contentKey=reader.bytes();reader.end();require(offer.id==id&&offer.recipientPeerId==recipientPeerId&&identifier(offer.groupId,32)&&identifier(offer.serverGroupId,32)&&identifier(offer.distributorPeerId)&&offer.contentKey.size()==32,"Stored group key handoff identity mismatch");pending.push_back(std::move(offer));}
    return pending;
}
void Storage::acknowledgeGroupKeyHandoff(const std::string& id,const std::string& recipientPeerId){
    require(identifier(id,32)&&identifier(recipientPeerId),"Invalid group key acknowledgement");
    Transaction transaction(impl_->sql);
    Statement found(impl_->sql,"SELECT b FROM group_key_outbox WHERE id=? AND recipient_peer=?");found.text(1,id);found.text(2,recipientPeerId);
    require(found.row(),"Group key acknowledgement has no pending handoff");
    Secret plain{impl_->protect(found.bytes(0),"PeerChat.v1/group-key-handoff/"+id+"/"+recipientPeerId,false)};
    Reader reader(plain.data);require(reader.string()==id,"Stored group key handoff ID mismatch");
    std::string group=reader.string();reader.string();reader.string();require(reader.string()==recipientPeerId,"Stored group key recipient mismatch");
    reader.number();std::uint64_t generation=reader.number();Secret key{reader.bytes()};reader.end();
    require(identifier(group,32)&&generation>0&&generation<=INT64_MAX,"Invalid stored group key generation");
    Statement receipt(impl_->sql,"INSERT OR IGNORE INTO group_key_receipts(group_id,key_generation,recipient_peer) VALUES(?,?,?)");receipt.text(1,group);receipt.number(2,static_cast<std::int64_t>(generation));receipt.text(3,recipientPeerId);receipt.row();
    Statement removal(impl_->sql,"DELETE FROM group_key_outbox WHERE id=? AND recipient_peer=?");removal.text(1,id);removal.text(2,recipientPeerId);removal.row();transaction.commit();
}
bool Storage::hasGroupKeyReceipt(const std::string& groupId,std::uint64_t generation,const std::string& recipientPeerId){
    require(identifier(groupId,32)&&generation>0&&generation<=INT64_MAX&&identifier(recipientPeerId),"Invalid group key receipt request");
    Statement statement(impl_->sql,"SELECT 1 FROM group_key_receipts WHERE group_id=? AND key_generation=? AND recipient_peer=?");statement.text(1,groupId);statement.number(2,static_cast<std::int64_t>(generation));statement.text(3,recipientPeerId);return statement.row();
}
bool Storage::hasPendingGroupKeyHandoff(const std::string& groupId,std::uint64_t generation,const std::string& recipientPeerId){
    require(identifier(groupId,32)&&generation>0&&generation<=INT64_MAX&&identifier(recipientPeerId),"Invalid pending group key request");
    Statement statement(impl_->sql,"SELECT 1 FROM group_key_outbox WHERE group_id=? AND key_generation=? AND recipient_peer=?");statement.text(1,groupId);statement.number(2,static_cast<std::int64_t>(generation));statement.text(3,recipientPeerId);return statement.row();
}
void Storage::cancelGroupKeyHandoffsFor(const std::string& groupId,const std::string& recipientPeerId){
    require(identifier(groupId,32)&&identifier(recipientPeerId),"Invalid group key handoff cancellation");
    Statement statement(impl_->sql,"DELETE FROM group_key_outbox WHERE group_id=? AND recipient_peer=?");statement.text(1,groupId);statement.text(2,recipientPeerId);statement.row();
}
}
