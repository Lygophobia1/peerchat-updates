#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "storage.hpp"
#include "group.hpp"
#include "group_text.hpp"
#include "archive_provenance.hpp"
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
#include <functional>
#include <tuple>
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
void indexGroupOrder(platform::SqlDatabase& sql,std::int64_t sequence,const Blob& plaintext,const Blob& envelope){
    std::uint64_t logical=0;std::string sender;
    if(group_text::valid(plaintext))logical=group_text::decode(plaintext).order;
    // Public PCGE v1 sender offset; used only to index an already authenticated
    // protected record. Admission still uses the canonical envelope validator.
    constexpr size_t senderOffset=4+2+32+8+8+32+8;
    if(envelope.size()>=senderOffset+64&&envelope[0]=='P'&&envelope[1]=='C'&&envelope[2]=='G'&&envelope[3]=='E'){
        sender.assign(envelope.begin()+senderOffset,envelope.begin()+senderOffset+64);if(!identifier(sender))sender.clear();
    }
    Statement insert(sql,"INSERT INTO group_message_order_v1(sequence,logical_order,sender) VALUES(?,?,?)");insert.number(1,sequence);insert.number(2,logical);insert.text(3,sender);insert.row();
}
StoredMessage decodeMessage(const Blob& plain) {
    Reader r(plain);StoredMessage m;m.id=r.string();m.sender=r.string();m.recipient=r.string();m.text=r.text();m.utcMilliseconds=r.number();auto state=r.number();
    // v0.2 encrypted records end after delivery. New records append the logical
    // position; accepting the old shape keeps existing history readable.
    if(r.remaining()>=8)m.logicalOrder=r.number();
    r.end();
    require(identifier(m.id,32)&&identifier(m.sender)&&identifier(m.recipient)&&state<=3&&!m.text.empty()&&unicode::utf16Units(m.text)<=16384,"Invalid stored message");m.delivery=static_cast<Delivery>(state);return m;
}
void encodeMessage(Writer& w,const StoredMessage& m){w.string(m.id);w.string(m.sender);w.string(m.recipient);w.text(m.text);w.number(m.utcMilliseconds);w.number(static_cast<unsigned char>(m.delivery));w.number(m.logicalOrder);}
void validateTargets(const DeliveryTargetManifest& manifest){
    require(identifier(manifest.messageId,32)&&identifier(manifest.canonicalRecipient)&&
        manifest.senderAccount>0&&manifest.recipientAccount>0&&manifest.senderAccount!=manifest.recipientAccount&&
        manifest.targets.size()>0&&manifest.targets.size()<=32,"Invalid delivery target manifest");
    if(manifest.selectionUnresolved){
        require(manifest.snapshotId.empty()&&manifest.snapshotSequence==0&&manifest.issuedUtcMs==0&&manifest.freshUntilUtcMs==0&&
            manifest.targets.size()==1&&manifest.targets.front().device==manifest.canonicalRecipient&&
            manifest.targets.front().bindingKeyId.empty(),
            "Unresolved delivery target must be canonical only");
    }else require(identifier(manifest.snapshotId,32)&&manifest.recipientAccount>0&&manifest.snapshotSequence>0&&
        manifest.issuedUtcMs>0&&manifest.freshUntilUtcMs>manifest.issuedUtcMs,"Invalid authenticated target snapshot metadata");
    std::string previous;
    for(const auto& target:manifest.targets){
        require(identifier(target.device)&&target.device>previous&&static_cast<unsigned char>(target.status)<=2&&
            (target.bindingKeyId.empty()?target.bindingGeneration==0:identifier(target.bindingKeyId,32)&&target.bindingGeneration>0),
            "Invalid or duplicate delivery target");
        previous=target.device;
    }
}
Blob encodeTargets(const DeliveryTargetManifest& manifest){
    validateTargets(manifest);Writer writer;writer.string("PeerChat/DeliveryTargets/v1");writer.string(manifest.messageId);
    writer.string(manifest.canonicalRecipient);writer.string(manifest.snapshotId);writer.number(manifest.senderAccount);
    writer.number(manifest.recipientAccount);
    writer.number(manifest.snapshotSequence);writer.number(manifest.issuedUtcMs);writer.number(manifest.freshUntilUtcMs);
    writer.number(manifest.selectionUnresolved?1:0);writer.number(manifest.targets.size());
    for(const auto& target:manifest.targets){writer.string(target.device);writer.string(target.bindingKeyId);
        writer.number(target.bindingGeneration);writer.number(static_cast<unsigned char>(target.status));}
    return writer.data;
}
DeliveryTargetManifest decodeTargets(const Blob& bytes){
    Reader reader(bytes);require(reader.string()=="PeerChat/DeliveryTargets/v1","Unsupported delivery target manifest");
    DeliveryTargetManifest manifest;manifest.messageId=reader.string();manifest.canonicalRecipient=reader.string();
    manifest.snapshotId=reader.string();manifest.senderAccount=reader.number();manifest.recipientAccount=reader.number();
    manifest.snapshotSequence=reader.number();
    manifest.issuedUtcMs=reader.number();manifest.freshUntilUtcMs=reader.number();
    const auto unresolved=reader.number(),count=reader.number();require(unresolved<=1&&count>0&&count<=32,"Invalid delivery target count");
    manifest.selectionUnresolved=unresolved!=0;
    for(std::uint64_t i=0;i<count;++i){DeliveryTargetState target;target.device=reader.string();
        target.bindingKeyId=reader.string();target.bindingGeneration=reader.number();
        auto status=reader.number();require(status<=2,"Invalid delivery target state");target.status=static_cast<DeliveryTargetStatus>(status);
        manifest.targets.push_back(std::move(target));}
    reader.end();validateTargets(manifest);return manifest;
}
bool validTargetRetryReason(const std::string& reason){
    constexpr std::array<const char*,8> allowed{{"not_authorized","recipient_key_retired",
        "recipient_key_unavailable","quota_exceeded","expired_or_invalid_timestamp",
        "conflicting_message","rejected_or_storage_unavailable","store_rejected"}};
    return std::any_of(allowed.begin(),allowed.end(),[&](const char* value){return reason==value;});
}
Blob encodeTargetRetry(const std::string& id,const std::string& device,const DeliveryTargetRetry& retry){
    require(identifier(id,32)&&identifier(device)&&validTargetRetryReason(retry.reason)&&retry.retryUtcMs>0,
        "Invalid target retry marker");
    Writer writer;writer.string("PeerChat/DeliveryTargetRetry/v1");writer.string(id);writer.string(device);
    writer.string(retry.reason);writer.number(retry.retryUtcMs);return writer.data;
}
DeliveryTargetRetry decodeTargetRetry(const Blob& bytes,const std::string& id,const std::string& device){
    Reader reader(bytes);require(reader.string()=="PeerChat/DeliveryTargetRetry/v1"&&reader.string()==id&&
        reader.string()==device,"Target retry marker identity mismatch");
    DeliveryTargetRetry retry;retry.reason=reader.string();retry.retryUtcMs=reader.number();reader.end();
    require(validTargetRetryReason(retry.reason)&&retry.retryUtcMs>0,"Invalid target retry marker");return retry;
}
Contact decodeContactRecord(const std::string& key,const Blob& plain){
    Reader r(plain);Contact c;
    c.peerId=r.string();c.name=r.text();c.publicKey=r.bytes();auto trust=r.number(),count=r.number();
    require(trust<=1&&count<=128&&key=="contact/"+c.peerId&&(c.publicKey.empty()||Storage::publicKeyId(c.publicKey)==c.peerId),"Invalid contact record");c.verified=trust==1;
    for(std::uint64_t i=0;i<count;i++){Endpoint e;e.host=r.text();auto port=r.number();require(port>0&&port<=65535,"Invalid contact port");e.port=static_cast<std::uint16_t>(port);c.endpoints.push_back(std::move(e));}
    r.end();return c;
}
}

using namespace storage_detail;
struct Storage::Impl {
    struct HistoryKey {
        std::uint64_t time=0;
        std::string sender,id,group;
        std::int64_t sequence=0;
        bool operator<(const HistoryKey& other) const {
            return std::tie(time,sender,id)<std::tie(other.time,other.sender,other.id);
        }
    };
    struct HistoryCache {std::string conversation;std::int64_t watermark=0,count=0;std::vector<HistoryKey> keys;};
    // Ordering keys live only in memory: never persist decrypted timestamps.
    // Cache at most 128 conversations / 32768 keys. The Android publisher reads
    // up to 128 contacts per pass; a smaller conversation count would thrash
    // even small histories on every publication. Key memory remains bounded.
    // Larger conversations use
    // a bounded top-page scan. Payloads are always read from protected storage.
    std::vector<HistoryCache> historyCaches;
    std::vector<HistoryKey> historyPageKeys(const std::string& conversation,std::int64_t watermark,std::int64_t count,
        int limit,const std::optional<HistoryKey>& before,
        const std::function<void(const std::function<void(HistoryKey)>&)>& scan){
        auto cached=std::find_if(historyCaches.begin(),historyCaches.end(),[&](const HistoryCache& c){return c.conversation==conversation;});
        std::vector<HistoryKey> keys;
        if(cached!=historyCaches.end()&&cached->watermark==watermark&&cached->count==count){
            keys=std::move(cached->keys);historyCaches.erase(cached);
        }else{
            if(cached!=historyCaches.end())historyCaches.erase(cached);
            const bool cacheable=count<=32768;
            scan([&](HistoryKey key){
                if(cacheable)keys.push_back(std::move(key));
                else if(!before||key<*before){
                    auto position=std::lower_bound(keys.begin(),keys.end(),key);keys.insert(position,std::move(key));
                    if(keys.size()>static_cast<size_t>(limit))keys.erase(keys.begin());
                }
            });
            std::sort(keys.begin(),keys.end());
            if(!cacheable)return keys;
        }
        auto end=before?std::lower_bound(keys.begin(),keys.end(),*before):keys.end();
        const auto available=static_cast<size_t>(end-keys.begin());
        std::vector<HistoryKey> page(end-std::min(available,static_cast<size_t>(limit)),end);
        size_t total=keys.size();for(const auto& c:historyCaches)total+=c.keys.size();
        while(!historyCaches.empty()&&(historyCaches.size()>=128||total>32768)){
            total-=historyCaches.front().keys.size();historyCaches.erase(historyCaches.begin());
        }
        historyCaches.push_back({conversation,watermark,count,std::move(keys)});return page;
    }
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
            execute("CREATE TABLE IF NOT EXISTS group_message_order_v1(sequence INTEGER PRIMARY KEY,logical_order INTEGER NOT NULL,sender TEXT NOT NULL);"
                    "CREATE INDEX IF NOT EXISTS group_message_logical_v1 ON group_message_order_v1(logical_order,sender,sequence);"
                    "CREATE TABLE IF NOT EXISTS group_order_state_v1(id INTEGER PRIMARY KEY,revision INTEGER NOT NULL);"
                    "INSERT OR IGNORE INTO group_order_state_v1 VALUES(1,0);");
            // Additive index only; names, paths, digests and provenance remain
            // in the existing platform-protected object backend.
            execute("CREATE TABLE IF NOT EXISTS attachment_history_index(sequence INTEGER PRIMARY KEY AUTOINCREMENT,attachment_id TEXT UNIQUE NOT NULL);");
            execute("CREATE TABLE IF NOT EXISTS group_reference_index_v1(sequence INTEGER PRIMARY KEY AUTOINCREMENT,server_group TEXT NOT NULL,local_group TEXT NOT NULL,message_id TEXT NOT NULL,UNIQUE(server_group,message_id));");
            // Separate outbox index contains identifiers only, never message text.
            execute("CREATE TABLE IF NOT EXISTS outbox(id TEXT PRIMARY KEY,peer TEXT NOT NULL);"
                    "CREATE TABLE IF NOT EXISTS delivery_target_outbox(id TEXT NOT NULL,device TEXT NOT NULL,PRIMARY KEY(id,device));"
                    "CREATE INDEX IF NOT EXISTS delivery_target_device ON delivery_target_outbox(device,id);"
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
        std::string key=s.text(0);Secret plain{impl_->protect(s.bytes(1),"PeerChat.v1/"+key,false)};
        result.push_back(decodeContactRecord(key,plain.data));
    }
    return result;
}
std::optional<Contact> Storage::contact(const std::string& peerId){
    require(identifier(peerId),"Invalid contact identity");
    const auto key="contact/"+peerId;
    Statement s(impl_->sql,"SELECT b FROM objects WHERE k=?");s.text(1,key);
    if(!s.row())return {};
    Secret plain{impl_->protect(s.bytes(0),"PeerChat.v1/"+key,false)};
    return decodeContactRecord(key,plain.data);
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
std::optional<DeliveryTargetManifest> Storage::deliveryTargets(const std::string& id){
    require(identifier(id,32),"Invalid delivery target message identity");
    auto bytes=impl_->get("delivery-targets/v1/"+id);if(bytes.empty())return {};
    auto manifest=decodeTargets(bytes);require(manifest.messageId==id,"Delivery target manifest identity mismatch");return manifest;
}
std::optional<DeliveryTargetRetry> Storage::targetRetry(const std::string& id,const std::string& device){
    require(identifier(id,32)&&identifier(device),"Invalid target retry identity");
    auto bytes=impl_->get("delivery-target-retry/v1/"+id+"/"+device);if(bytes.empty())return {};
    return decodeTargetRetry(bytes,id,device);
}
bool Storage::appendWithTargets(const std::string& conversation,const StoredMessage& m,const DeliveryTargetManifest& manifest){
    validateTargets(manifest);
    require(identifier(conversation)&&identifier(m.id,32)&&identifier(m.sender)&&identifier(m.recipient)&&
        conversation==m.recipient&&m.recipient==manifest.canonicalRecipient&&m.id==manifest.messageId&&
        m.delivery==Delivery::Queued&&!m.text.empty()&&unicode::utf16Units(m.text)<=16384,
        "Invalid targeted outgoing message");
    Transaction transaction(impl_->sql);
    Statement existing(impl_->sql,"SELECT peer,b FROM messages WHERE id=?");existing.text(1,m.id);
    if(existing.row()){
        require(existing.text(0)==conversation,"Targeted message ID reused in another conversation");
        Secret plain{impl_->protect(existing.bytes(1),"PeerChat.v1/message/"+m.id+"/"+conversation,false)};
        const auto old=decodeMessage(plain.data);auto frozen=deliveryTargets(m.id);
        require(frozen&&encodeTargets(*frozen)==encodeTargets(manifest)&&old.id==m.id&&old.sender==m.sender&&
            old.recipient==m.recipient&&old.text==m.text&&old.utcMilliseconds==m.utcMilliseconds&&
            old.logicalOrder==m.logicalOrder,"Conflicting targeted message ID");
        transaction.commit();return false;
    }
    Writer plain;encodeMessage(plain,m);auto encrypted=impl_->protect(plain.data,"PeerChat.v1/message/"+m.id+"/"+conversation,true);
    Statement insert(impl_->sql,"INSERT INTO messages(id,peer,b) VALUES(?,?,?)");insert.text(1,m.id);insert.text(2,conversation);insert.blob(3,encrypted);insert.row();
    Statement pending(impl_->sql,"INSERT INTO outbox(id,peer) VALUES(?,?)");pending.text(1,m.id);pending.text(2,conversation);pending.row();
    impl_->put("delivery-targets/v1/"+m.id,encodeTargets(manifest));
    for(const auto& target:manifest.targets){
        if(target.status!=DeliveryTargetStatus::Pending)continue;
        Statement route(impl_->sql,"INSERT INTO delivery_target_outbox(id,device) VALUES(?,?)");route.text(1,m.id);route.text(2,target.device);route.row();
    }
    transaction.commit();return true;
}
std::optional<StoredMessage> Storage::message(const std::string& id){
    require(identifier(id,32),"Invalid message identity");Statement query(impl_->sql,"SELECT peer,b FROM messages WHERE id=?");query.text(1,id);
    if(!query.row())return std::nullopt;
    auto peer=query.text(0);Secret plain{impl_->protect(query.bytes(1),"PeerChat.v1/message/"+id+"/"+peer,false)};
    auto message=decodeMessage(plain.data);require(message.id==id,"Stored message ID mismatch");return message;
}
std::optional<StoredMessage> Storage::historyMessage(const std::string& id){
    auto result=message(id);if(!result)return {};
    Statement cursor(impl_->sql,"SELECT rowid FROM messages WHERE id=?");cursor.text(1,id);
    require(cursor.row(),"History message disappeared");result->sequence=cursor.number(0);
    require(result->sequence>0,"Invalid history message cursor");return result;
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
std::uint64_t Storage::archiveHistoryCount(const std::string& conversation,std::uint64_t after,std::uint64_t through){
    require(identifier(conversation)&&after<=through&&through<=static_cast<std::uint64_t>(archiveHistoryWatermark()),"Invalid archive count window");
    Statement query(impl_->sql,"SELECT COUNT(*) FROM message_archive_index i JOIN messages m ON m.id=i.message_id WHERE m.peer=? AND i.sequence>? AND i.sequence<=?");
    query.text(1,conversation);query.number(2,after);query.number(3,through);require(query.row(),"Archive count unavailable");return static_cast<std::uint64_t>(query.number(0));
}
Blob Storage::transferJournal(const std::string& device){require(identifier(device),"Invalid transfer device");return impl_->get("transfer-journal/v1/"+device);}
void Storage::saveTransferJournal(const std::string& device,const Blob& expected,const Blob& next){
    require(identifier(device)&&!next.empty()&&next.size()<=262144,"Invalid transfer journal");
    Transaction transaction(impl_->sql);require(transferJournal(device)==expected,"Transfer journal changed concurrently");
    impl_->put("transfer-journal/v1/"+device,next);transaction.commit();
}
Blob Storage::transferTuning(const std::string& device){
    require(identifier(device),"Invalid transfer tuning device");return impl_->get("transfer-tuning/v1/"+device);
}
void Storage::saveTransferTuning(const std::string& device,const Blob& expected,const Blob& next){
    require(identifier(device)&&!next.empty()&&next.size()<=8192,"Invalid transfer tuning");
    Transaction transaction(impl_->sql);Secret previous{transferTuning(device)};
    require(previous.data==expected,"Transfer tuning changed concurrently");
    if(previous.data!=next)impl_->put("transfer-tuning/v1/"+device,next);
    transaction.commit();
}
Blob Storage::attachmentMetadata(const std::string& id){
    require(identifier(id,32),"Invalid attachment identity");return impl_->get("attachment-history/v1/"+id);
}
Blob Storage::groupAttachmentStaging(const std::string& device){
    require(identifier(device),"Invalid staging device");return impl_->get("group-attachment-staging/v1/"+device);
}
Blob Storage::directAttachmentState(const std::string& device){require(identifier(device),"Invalid DM cache device");return impl_->get("dm-attachment-state/v1/"+device);}
Blob Storage::directAttachmentReadContext(const std::string& device){require(identifier(device),"Invalid DM cache device");return impl_->get("dm-attachment-read-context/v1/"+device);}
void Storage::saveDirectAttachmentReadContext(const std::string& device,const Blob& expected,const Blob& next){
    require(identifier(device)&&!next.empty()&&next.size()<=4096,"Invalid DM attachment read context");Transaction transaction(impl_->sql);Secret previous{directAttachmentReadContext(device)};
    require(previous.data==expected,"DM attachment read context changed concurrently");if(previous.data!=next)impl_->put("dm-attachment-read-context/v1/"+device,next);transaction.commit();
}
void Storage::saveDirectAttachmentState(const std::string& device,const Blob& expected,const Blob& next){
    require(identifier(device)&&!next.empty()&&next.size()<=1024*1024,"Invalid DM attachment state");Transaction transaction(impl_->sql);Secret previous{directAttachmentState(device)};
    require(previous.data==expected,"DM attachment state changed concurrently");if(previous.data!=next)impl_->put("dm-attachment-state/v1/"+device,next);transaction.commit();
}
Blob Storage::directAttachmentTransfers(const std::string& device){require(identifier(device),"Invalid DM transfer device");return impl_->get("dm-attachment-transfers/v1/"+device);}
Blob Storage::directAttachmentJob(const std::string& device,const std::string& id){require(identifier(device)&&identifier(id,32),"Invalid DM attachment job identity");return impl_->get("dm-attachment-job/v1/"+device+"/"+id);}
Blob Storage::directAttachmentIndexPage(const std::string& device,const std::string& page){
    require(identifier(device)&&identifier(page),"Invalid DM attachment index page");auto bytes=impl_->get("dm-attachment-index-page/v1/"+device+"/"+page);
    require(bytes.empty()||(bytes.size()<=65536&&publicKeyId(bytes)==page),"DM attachment index page integrity failed");return bytes;
}
void Storage::saveDirectAttachmentIndexPage(const std::string& device,const std::string& page,const Blob& bytes){
    require(identifier(device)&&identifier(page)&&!bytes.empty()&&bytes.size()<=65536&&publicKeyId(bytes)==page,"Invalid DM attachment index page bounds or digest");
    Transaction transaction(impl_->sql);Secret previous{directAttachmentIndexPage(device,page)};
    require(previous.data.empty()||previous.data==bytes,"DM attachment index page is immutable");
    if(previous.data.empty())impl_->put("dm-attachment-index-page/v1/"+device+"/"+page,bytes);
    transaction.commit();
}
void Storage::saveDirectAttachmentJob(const std::string& device,const std::string& id,const Blob& expectedJob,const Blob& nextJob,const Blob& expectedIndex,const Blob& nextIndex){
    require(identifier(device)&&identifier(id,32)&&!nextJob.empty()&&nextJob.size()<=900*1024&&!nextIndex.empty()&&nextIndex.size()<=65536,"Invalid DM attachment job bounds");
    Transaction transaction(impl_->sql);Secret previousJob{directAttachmentJob(device,id)},previousIndex{directAttachmentState(device)};
    require(previousJob.data==expectedJob&&previousIndex.data==expectedIndex,"DM attachment job or index changed concurrently");
    if(previousJob.data!=nextJob)impl_->put("dm-attachment-job/v1/"+device+"/"+id,nextJob);
    if(previousIndex.data!=nextIndex)impl_->put("dm-attachment-state/v1/"+device,nextIndex);
    transaction.commit();
}
void Storage::saveDirectAttachmentTransfers(const std::string& device,const Blob& expected,const Blob& next){
    require(identifier(device)&&!next.empty()&&next.size()<=262144,"Invalid DM attachment transfers");Transaction transaction(impl_->sql);Secret previous{directAttachmentTransfers(device)};
    require(previous.data==expected,"DM attachment transfers changed concurrently");if(previous.data!=next)impl_->put("dm-attachment-transfers/v1/"+device,next);transaction.commit();
}
void Storage::saveGroupAttachmentStaging(const std::string& device,const Blob& expected,const Blob& next){
    require(identifier(device)&&!next.empty()&&next.size()<=262144,"Invalid group attachment staging");
    Transaction transaction(impl_->sql);Secret previous{groupAttachmentStaging(device)};
    require(previous.data==expected,"Group attachment staging changed concurrently");
    if(previous.data!=next)impl_->put("group-attachment-staging/v1/"+device,next);
    transaction.commit();
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
    require(delivery!=Delivery::Delivered||!deliveryTargets(id),"Manifested delivery requires authenticated target settlement");
    Statement s(impl_->sql,"SELECT peer,b FROM messages WHERE id=?");s.text(1,id);if(!s.row()){transaction.commit();return;}
    std::string peer=s.text(0);Secret plain{impl_->protect(s.bytes(1),"PeerChat.v1/message/"+id+"/"+peer,false)};auto m=decodeMessage(plain.data);
    require(m.id==id,"Stored message ID mismatch");if(deliveryRank(delivery)<=deliveryRank(m.delivery)){transaction.commit();return;}m.delivery=delivery;
    Writer writer;encodeMessage(writer,m);Blob encrypted=impl_->protect(writer.data,"PeerChat.v1/message/"+id+"/"+peer,true);
    Statement update(impl_->sql,"UPDATE messages SET b=? WHERE id=?");update.blob(1,encrypted);update.text(2,id);update.row();
    if(delivery==Delivery::Delivered){Statement remove(impl_->sql,"DELETE FROM outbox WHERE id=?");remove.text(1,id);remove.row();}
    transaction.commit();
}
bool Storage::settleTarget(const std::string& id,const std::string& device){
    require(identifier(id,32)&&identifier(device),"Invalid target acknowledgement identity");
    Transaction transaction(impl_->sql);auto manifest=deliveryTargets(id);
    require(manifest.has_value(),"Target acknowledgement requires frozen manifest");
    auto target=std::find_if(manifest->targets.begin(),manifest->targets.end(),[&](const auto& item){return item.device==device;});
    require(target!=manifest->targets.end(),"Target acknowledgement is not in frozen manifest");
    if(target->status==DeliveryTargetStatus::Acknowledged){transaction.commit();return false;}
    // An exact authenticated ACK may arrive after a route became unavailable.
    // It proves delivery; unavailable alone never did.
    Statement row(impl_->sql,"SELECT peer,b FROM messages WHERE id=?");row.text(1,id);
    require(row.row()&&row.text(0)==manifest->canonicalRecipient,"Target acknowledgement lost canonical message");
    Secret plain{impl_->protect(row.bytes(1),"PeerChat.v1/message/"+id+"/"+manifest->canonicalRecipient,false)};
    auto message=decodeMessage(plain.data);require(message.id==id&&message.recipient==manifest->canonicalRecipient,
        "Target acknowledgement message identity mismatch");
    target->status=DeliveryTargetStatus::Acknowledged;
    impl_->put("delivery-targets/v1/"+id,encodeTargets(*manifest));
    Statement removeTarget(impl_->sql,"DELETE FROM delivery_target_outbox WHERE id=? AND device=?");removeTarget.text(1,id);removeTarget.text(2,device);removeTarget.row();
    const bool all= !manifest->selectionUnresolved&&std::all_of(manifest->targets.begin(),manifest->targets.end(),
        [](const auto& item){return item.status==DeliveryTargetStatus::Acknowledged;});
    if(all||deliveryRank(message.delivery)<deliveryRank(Delivery::Sent)){
        message.delivery=all?Delivery::Delivered:Delivery::Sent;Writer writer;encodeMessage(writer,message);
        auto encrypted=impl_->protect(writer.data,"PeerChat.v1/message/"+id+"/"+manifest->canonicalRecipient,true);
        Statement update(impl_->sql,"UPDATE messages SET b=? WHERE id=?");update.blob(1,encrypted);update.text(2,id);update.row();
        if(all){Statement removeOutbox(impl_->sql,"DELETE FROM outbox WHERE id=?");removeOutbox.text(1,id);removeOutbox.row();}
    }
    transaction.commit();return true;
}
bool Storage::unavailableTarget(const std::string& id,const std::string& device){
    require(identifier(id,32)&&identifier(device),"Invalid unavailable target identity");
    Transaction transaction(impl_->sql);auto manifest=deliveryTargets(id);
    require(manifest.has_value(),"Unavailable target requires frozen manifest");
    auto target=std::find_if(manifest->targets.begin(),manifest->targets.end(),[&](const auto& item){return item.device==device;});
    require(target!=manifest->targets.end(),"Unavailable target is not in frozen manifest");
    if(target->status==DeliveryTargetStatus::Unavailable){transaction.commit();return false;}
    require(target->status==DeliveryTargetStatus::Pending,"Acknowledged target cannot become unavailable");
    target->status=DeliveryTargetStatus::Unavailable;impl_->put("delivery-targets/v1/"+id,encodeTargets(*manifest));
    Statement removeTarget(impl_->sql,"DELETE FROM delivery_target_outbox WHERE id=? AND device=?");removeTarget.text(1,id);removeTarget.text(2,device);removeTarget.row();
    transaction.commit();return true;
}
Blob Storage::mailboxEnvelope(const std::string& id,const std::string& device){
    require(identifier(id,32)&&identifier(device),"Invalid mailbox submission identity");
    return impl_->get("mailbox-envelope/"+id+"/"+device);
}
bool Storage::saveTargetRetry(const std::string& id,const std::string& device,const Blob& exactEnvelope,
                              const DeliveryTargetRetry& retry){
    require(!exactEnvelope.empty()&&mailboxEnvelope(id,device)==exactEnvelope,"Target retry has no exact durable envelope");
    const auto header=mailbox::parse(exactEnvelope).header;
    require(header.messageId==id&&header.recipientDevice==device,"Target retry envelope identity mismatch");
    Transaction transaction(impl_->sql);auto manifest=deliveryTargets(id);
    require(manifest&&manifest->senderAccount==header.senderAccount&&
        manifest->recipientAccount==header.recipientAccount,"Target retry account mismatch");
    auto target=std::find_if(manifest->targets.begin(),manifest->targets.end(),[&](const auto& value){return value.device==device;});
    require(target!=manifest->targets.end()&&target->status==DeliveryTargetStatus::Pending,
        "Target retry is not pending in frozen manifest");
    if(mailboxUploadAccepted(id,device)){transaction.commit();return false;}
    const auto key="delivery-target-retry/v1/"+id+"/"+device;
    const auto bytes=encodeTargetRetry(id,device,retry);
    const bool changed=impl_->get(key)!=bytes;if(changed)impl_->put(key,bytes);
    transaction.commit();return changed;
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
    if(auto manifest=deliveryTargets(id))require(manifest->senderAccount==envelope.header.senderAccount&&
        manifest->recipientAccount==envelope.header.recipientAccount&&
        std::any_of(manifest->targets.begin(),manifest->targets.end(),[&](const auto& target){
            return target.device==device&&target.status==DeliveryTargetStatus::Pending;}),
        "Mailbox envelope is outside frozen target/account scope");
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
    impl_->erase("delivery-target-retry/v1/"+id+"/"+device);
}
std::vector<StoredMessage> Storage::pendingMailboxUploads(int limit){
    require(limit>0&&limit<=64,"Invalid mailbox upload page");
    Statement query(impl_->sql,"SELECT m.id,m.peer,m.b FROM messages m WHERE EXISTS(SELECT 1 FROM objects e WHERE e.k>='mailbox-envelope/'||m.id||'/' AND e.k<'mailbox-envelope/'||m.id||'0' AND NOT EXISTS(SELECT 1 FROM objects a WHERE a.k='mailbox-uploaded/'||substr(e.k,18))) ORDER BY m.rowid LIMIT ?");query.number(1,limit);
    std::vector<StoredMessage> result;
    while(query.row()){const auto id=query.text(0),peer=query.text(1);Secret plain{impl_->protect(query.bytes(2),"PeerChat.v1/message/"+id+"/"+peer,false)};auto m=decodeMessage(plain.data);require(m.id==id&&m.recipient==peer,"Mailbox upload identity mismatch");result.push_back(std::move(m));}
    return result;
}
bool Storage::mailboxUploadAccepted(const std::string& id,const std::string& device){
    require(identifier(id,32)&&identifier(device),"Invalid mailbox upload identity");auto accepted=impl_->get("mailbox-uploaded/"+id+"/"+device);
    if(accepted.empty())return false;
    require(accepted==mailboxEnvelope(id,device),"Mailbox upload acceptance mismatch");return true;
}
std::vector<StoredMessage> Storage::mailboxCandidates(std::int64_t afterSequence,int limit){
    require(afterSequence>=0&&limit>0&&limit<=64,"Invalid mailbox candidate page");
    // Exact identifier prefixes use the binary primary-key index. A correlated
    // LIKE expression scans every object for every retained message.
    Statement query(impl_->sql,"SELECT m.rowid,m.id,m.peer,m.b FROM messages m WHERE m.rowid>? AND ((EXISTS(SELECT 1 FROM outbox o WHERE o.id=m.id) AND NOT EXISTS(SELECT 1 FROM objects t WHERE t.k='delivery-targets/v1/'||m.id) AND NOT EXISTS(SELECT 1 FROM objects a WHERE a.k>='mailbox-uploaded/'||m.id||'/' AND a.k<'mailbox-uploaded/'||m.id||'0')) OR EXISTS(SELECT 1 FROM delivery_target_outbox t WHERE t.id=m.id) OR EXISTS(SELECT 1 FROM objects e WHERE e.k>='mailbox-envelope/'||m.id||'/' AND e.k<'mailbox-envelope/'||m.id||'0' AND NOT EXISTS(SELECT 1 FROM objects a WHERE a.k='mailbox-uploaded/'||substr(e.k,18)))) ORDER BY m.rowid LIMIT ?");
    query.number(1,afterSequence);query.number(2,limit);std::vector<StoredMessage> result;
    while(query.row()){const auto sequence=query.number(0);auto id=query.text(1),peer=query.text(2);Secret plain{impl_->protect(query.bytes(3),"PeerChat.v1/message/"+id+"/"+peer,false)};auto m=decodeMessage(plain.data);require(m.id==id&&m.recipient==peer,"Mailbox candidate identity mismatch");m.sequence=sequence;result.push_back(std::move(m));}
    return result;
}
bool Storage::mailboxHasPendingEnvelope(const std::string& id){
    require(identifier(id,32),"Invalid pending envelope identity");Statement query(impl_->sql,"SELECT 1 FROM objects e WHERE e.k>=? AND e.k<? AND NOT EXISTS(SELECT 1 FROM objects a WHERE a.k='mailbox-uploaded/'||substr(e.k,18)) LIMIT 1");query.text(1,"mailbox-envelope/"+id+"/");query.text(2,"mailbox-envelope/"+id+"0");return query.row();
}
void Storage::mailboxIssue(const std::string& id,const std::string& device,const std::string& reason){
    require(identifier(id,32)&&identifier(device)&&reason.size()<=128&&std::all_of(reason.begin(),reason.end(),[](char c){return (c>='a'&&c<='z')||c=='_';}),"Invalid mailbox issue");
    auto key="mailbox-issue/"+id+"/"+device;Blob value(reason.begin(),reason.end());if(impl_->get(key)!=value)impl_->put(key,value);
}
std::string Storage::mailboxIssue(const std::string& id,const std::string& device){
    require(identifier(id,32)&&identifier(device),"Invalid mailbox issue identity");auto value=impl_->get("mailbox-issue/"+id+"/"+device);return std::string(value.begin(),value.end());
}
std::string Storage::mailboxDiagnostics(){
    std::string result;Statement query(impl_->sql,"SELECT k FROM objects WHERE k LIKE 'mailbox-issue/%' ORDER BY k LIMIT 32");
    while(query.row()){
        const auto key=query.text(0);auto saved=impl_->get(key);std::string reason(saved.begin(),saved.end());
        require(reason.size()<=128&&std::all_of(reason.begin(),reason.end(),[](char c){return(c>='a'&&c<='z')||c=='_';}),"Invalid retained mailbox issue");
        result+="mailbox_action_required message="+key.substr(14,32)+" device="+key.substr(47)+" reason="+reason+" action=preserve_queued_message_and_request_verified_attempt_reconciliation\n";
    }
    Statement targets(impl_->sql,"SELECT k FROM objects WHERE k LIKE 'delivery-targets/v1/%' ORDER BY k LIMIT 32");
    while(targets.row()){
        const auto key=targets.text(0),id=key.substr(std::string("delivery-targets/v1/").size());
        auto manifest=deliveryTargets(id);require(manifest.has_value(),"Missing delivery target manifest");
        unsigned acknowledged=0,pending=0,unavailable=0;
        for(const auto& target:manifest->targets)switch(target.status){
            case DeliveryTargetStatus::Acknowledged:++acknowledged;break;
            case DeliveryTargetStatus::Pending:++pending;break;
            case DeliveryTargetStatus::Unavailable:++unavailable;break;
        }
        const bool all= !manifest->selectionUnresolved&&acknowledged==manifest->targets.size();
        result+="delivery_targets message="+id+" selection="+(manifest->selectionUnresolved?"unresolved":"resolved")+
            " total="+std::to_string(manifest->targets.size())+" acknowledged="+std::to_string(acknowledged)+
            " pending="+std::to_string(pending)+" unavailable="+std::to_string(unavailable)+
            " result="+(all?"all_verified_acks":"partial")+"\n";
        result+="delivery_target_scope message_id="+id+" canonical_device="+manifest->canonicalRecipient+
            " sender_account="+std::to_string(manifest->senderAccount)+" recipient_account="+std::to_string(manifest->recipientAccount)+"\n";
        for(const auto& target:manifest->targets)
            result+="delivery_target_device message_id="+id+" device="+target.device+
                " status="+(target.status==DeliveryTargetStatus::Acknowledged?"acknowledged":target.status==DeliveryTargetStatus::Pending?"pending":"unavailable")+"\n";
    }
    Statement retries(impl_->sql,"SELECT k FROM objects WHERE k LIKE 'delivery-target-retry/v1/%' ORDER BY k LIMIT 32");
    while(retries.row()){
        const auto key=retries.text(0),prefix=std::string("delivery-target-retry/v1/");
        require(key.size()==prefix.size()+32+1+64&&key[prefix.size()+32]=='/',"Invalid target retry key");
        const auto id=key.substr(prefix.size(),32),device=key.substr(prefix.size()+33);
        auto retry=targetRetry(id,device);require(retry.has_value(),"Missing target retry marker");
        result+="delivery_target_retry message="+id+" device="+device+" reason="+retry->reason+
            " retry_utc_ms="+std::to_string(retry->retryUtcMs)+"\n";
    }
    return result.empty()?"mailbox_action_required=none\n":result;
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
void Storage::saveAuthenticatedMailboxReceipt(const StoredMessage& received,std::uint64_t localAccount,const Blob& wire){
    const auto receipt=mailbox::parseReceipt(wire);
    require(localAccount>0&&receipt.recipientAccount==localAccount&&receipt.messageId==received.id&&
        receipt.senderDevice==received.sender&&receipt.recipientDevice==received.recipient&&
        received.delivery==Delivery::Delivered,"Authenticated mailbox receipt context mismatch");
    auto durable=message(received.id);require(durable.has_value(),"Mailbox receipt requires committed message");
    if(durable->recipient==received.recipient){
        saveMailboxReceipt(received.id,received.sender,wire);return;
    }
    // An archive-first original can name another device on this account. Its
    // protected assertion was already verified during import, but neither the
    // import nor its alias is an ACK. Only the now-authenticated mailbox receive
    // may create this local device's receipt for identical original content.
    Transaction transaction(impl_->sql);
    durable=message(received.id);require(durable.has_value(),"Archived mailbox receipt lost durable message");
    auto proofBytes=archiveHistoryProof(received.id);
    require(!proofBytes.empty(),"Mailbox recipient alias requires verified archived provenance");
    const auto assertion=archive::decodeAssertion(proofBytes);
    const auto& original=assertion.original.message;
    require(assertion.account==localAccount&&assertion.original.recipientAccount==localAccount&&
        assertion.original.senderAccount!=localAccount&&original.id==received.id&&
        original.sender==received.sender&&original.recipient==durable->recipient&&
        durable->sender==received.sender&&durable->delivery==Delivery::Sent&&
        original.text==received.text&&durable->text==received.text&&
        original.utcMilliseconds==received.utcMilliseconds&&durable->utcMilliseconds==received.utcMilliseconds&&
        original.logicalOrder==received.logicalOrder&&durable->logicalOrder==received.logicalOrder,
        "Mailbox receipt does not match verified archived original");
    auto recipientAccount=historicalDeviceAccount(durable->recipient);
    require(recipientAccount&&*recipientAccount==localAccount,"Archived recipient account binding mismatch");
    const auto prefix="mailbox-receipt/"+received.id+"/"+received.sender+"/";
    const auto key=prefix+hex(receipt.envelopeDigest);
    const auto previous=impl_->get(key);
    if(!previous.empty())require(mailbox::receiptPayload(mailbox::parseReceipt(previous))==mailbox::receiptPayload(receipt),
        "Conflicting archived mailbox receipt");
    else {
        Statement count(impl_->sql,"SELECT COUNT(*) FROM objects WHERE k LIKE ?");count.text(1,prefix+"%");
        require(count.row()&&count.number(0)<32,"Mailbox receipt retention limit reached");impl_->put(key,wire);
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
    require(identifier(conversation)&&limit>0&&limit<=1000&&beforeSequence>0,"Invalid history page request");
    std::optional<Impl::HistoryKey> before;
    // The ABI token remains a local rowid, but names an authenticated ordering
    // key, not an arrival boundary. Account conversations can span device peers.
    if(beforeSequence!=INT64_MAX){
        Statement anchor(impl_->sql,"SELECT id,peer,b FROM messages WHERE rowid=?");anchor.number(1,beforeSequence);
        require(anchor.row(),"Unknown history page cursor");auto id=anchor.text(0),peer=anchor.text(1);
        Secret plain{impl_->protect(anchor.bytes(2),"PeerChat.v1/message/"+id+"/"+peer,false)};auto m=decodeMessage(plain.data);
        require(m.id==id,"Stored history cursor identity mismatch");before=Impl::HistoryKey{m.utcMilliseconds,m.sender,m.id,{},beforeSequence};
    }
    Statement revision(impl_->sql,"SELECT COALESCE(MAX(a.sequence),0),COUNT(*) FROM messages m JOIN message_archive_index a ON a.message_id=m.id WHERE m.peer=?");
    revision.text(1,conversation);require(revision.row(),"History revision unavailable");
    // An append-only index lets a warm DM ordering cache authenticate only new
    // rows. A deletion/replacement makes the delta count disagree and falls back
    // to the full protected scan below. Never persist plaintext ordering keys.
    auto cached=std::find_if(impl_->historyCaches.begin(),impl_->historyCaches.end(),[&](const auto& c){return c.conversation=="dm/"+conversation;});
    const auto watermark=revision.number(0),count=revision.number(1);
    if(cached!=impl_->historyCaches.end()&&watermark>cached->watermark&&count>cached->count&&count<=32768){
        std::vector<Impl::HistoryKey> added;
        Statement delta(impl_->sql,"SELECT m.rowid,m.id,m.b FROM messages m JOIN message_archive_index a ON a.message_id=m.id WHERE m.peer=? AND a.sequence>? AND a.sequence<=?");
        delta.text(1,conversation);delta.number(2,cached->watermark);delta.number(3,watermark);
        while(delta.row()){
            auto id=delta.text(1);Secret plain{impl_->protect(delta.bytes(2),"PeerChat.v1/message/"+id+"/"+conversation,false)};auto m=decodeMessage(plain.data);
            require(m.id==id,"Stored message ID mismatch");added.push_back({m.utcMilliseconds,m.sender,m.id,{},delta.number(0)});
        }
        if(static_cast<std::int64_t>(added.size())==count-cached->count){
            cached->keys.insert(cached->keys.end(),added.begin(),added.end());std::sort(cached->keys.begin(),cached->keys.end());
            cached->watermark=watermark;cached->count=count;
        }
    }
    auto keys=impl_->historyPageKeys("dm/"+conversation,revision.number(0),revision.number(1),limit,before,[&](const auto& accept){
        Statement rows(impl_->sql,"SELECT rowid,id,b FROM messages WHERE peer=?");rows.text(1,conversation);
        while(rows.row()){
            auto id=rows.text(1);Secret plain{impl_->protect(rows.bytes(2),"PeerChat.v1/message/"+id+"/"+conversation,false)};auto m=decodeMessage(plain.data);
            require(m.id==id,"Stored message ID mismatch");accept(Impl::HistoryKey{m.utcMilliseconds,m.sender,m.id,{},rows.number(0)});
        }
    });
    std::vector<StoredMessage> result;
    for(const auto& key:keys){auto m=message(key.id);require(m.has_value(),"History message disappeared");m->sequence=key.sequence;result.push_back(std::move(*m));}
    return result;
}
void Storage::eraseHistory(const std::string& conversation) {
    require(identifier(conversation),"Invalid conversation identity");
    Transaction transaction(impl_->sql);
    Statement mailboxRecords(impl_->sql,"DELETE FROM objects WHERE EXISTS(SELECT 1 FROM messages m WHERE m.peer=? AND (objects.k LIKE 'mailbox-envelope/'||m.id||'/%' OR objects.k LIKE 'mailbox-uploaded/'||m.id||'/%' OR objects.k LIKE 'mailbox-receipt/'||m.id||'/%' OR objects.k LIKE 'delivery-target-retry/v1/'||m.id||'/%' OR objects.k='archive-history-proof/'||m.id OR objects.k='delivery-targets/v1/'||m.id))");
    mailboxRecords.text(1,conversation);mailboxRecords.row();
    Statement removeTargetRoutes(impl_->sql,"DELETE FROM delivery_target_outbox WHERE id IN (SELECT id FROM messages WHERE peer=?)");removeTargetRoutes.text(1,conversation);removeTargetRoutes.row();
    Statement removeOutbox(impl_->sql,"DELETE FROM outbox WHERE peer=?");removeOutbox.text(1,conversation);removeOutbox.row();
    Statement removeMessages(impl_->sql,"DELETE FROM messages WHERE peer=?");removeMessages.text(1,conversation);removeMessages.row();
    transaction.commit();
}
std::vector<StoredMessage> Storage::pending(const std::string& conversation,int limit,std::int64_t afterSequence,bool newest,const std::vector<std::string>& excluded) {
    require(identifier(conversation)&&limit>0&&limit<=1000&&afterSequence>=0&&excluded.size()<=16,"Invalid outbox page request");
    std::string sql="SELECT m.id,m.peer,m.b,m.rowid FROM outbox o JOIN messages m ON o.id=m.id WHERE ((o.peer=? AND NOT EXISTS(SELECT 1 FROM objects t WHERE t.k='delivery-targets/v1/'||m.id)) OR EXISTS(SELECT 1 FROM delivery_target_outbox t WHERE t.id=m.id AND t.device=?)) AND m.rowid>?";
    if(!excluded.empty()){sql+=" AND m.id NOT IN (";for(size_t i=0;i<excluded.size();++i){require(identifier(excluded[i],32),"Invalid excluded outbox identity");sql+=i?",?":"?";}sql+=")";}
    sql+=newest?" ORDER BY m.rowid DESC LIMIT ?":" ORDER BY m.rowid LIMIT ?";
    Statement s(impl_->sql,sql);s.text(1,conversation);s.text(2,conversation);s.number(3,afterSequence);int parameter=4;
    for(const auto& id:excluded)s.text(parameter++,id);
    s.number(parameter,limit);
    std::vector<StoredMessage> result;
    while(s.row()){
        auto id=s.text(0),canonical=s.text(1);Secret plain{impl_->protect(s.bytes(2),"PeerChat.v1/message/"+id+"/"+canonical,false)};
        auto m=decodeMessage(plain.data);require(m.id==id&&m.recipient==canonical,"Outbox identity mismatch");
        if(auto manifest=deliveryTargets(id)){
            auto target=std::find_if(manifest->targets.begin(),manifest->targets.end(),[&](const auto& value){return value.device==conversation;});
            require(target!=manifest->targets.end()&&target->status==DeliveryTargetStatus::Pending,"Target outbox index mismatch");
            m.delivery=mailboxUploadAccepted(id,conversation)?Delivery::Stored:Delivery::Queued;
        }
        m.sequence=s.number(3);result.push_back(std::move(m));
    }
    return result;
}
bool Storage::pendingRouteDemand(const std::string& device){
    require(identifier(device),"Invalid route demand device");
    // Only durable indexes and exact per-device mailbox acceptance markers are
    // consulted. An uncertain target may cause an extra route; send admission
    // still reads and authenticates the protected message and target manifest.
    Statement query(impl_->sql,
        "SELECT EXISTS(SELECT 1 FROM outbox o WHERE o.peer=? "
        "AND NOT EXISTS(SELECT 1 FROM objects m WHERE m.k='delivery-targets/v1/'||o.id) "
        "AND NOT EXISTS(SELECT 1 FROM objects a WHERE a.k='mailbox-uploaded/'||o.id||'/'||?) LIMIT 1) "
        "OR EXISTS(SELECT 1 FROM delivery_target_outbox t WHERE t.device=? "
        "AND NOT EXISTS(SELECT 1 FROM objects a WHERE a.k='mailbox-uploaded/'||t.id||'/'||?) LIMIT 1)");
    query.text(1,device);query.text(2,device);query.text(3,device);query.text(4,device);
    require(query.row(),"Route demand index unavailable");return query.number(0)!=0;
}
std::vector<std::string> Storage::pendingTargetDevices(int limit){
    require(limit>0&&limit<=256,"Invalid pending target device page");
    Statement query(impl_->sql,"SELECT DISTINCT device FROM delivery_target_outbox ORDER BY device LIMIT ?");query.number(1,limit);
    std::vector<std::string> result;while(query.row()){auto device=query.text(0);require(identifier(device),"Invalid target outbox device");result.push_back(std::move(device));}
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
Blob Storage::groupReplicaReceipt(const std::string& group,const std::string& id){
    require(identifier(group,32)&&identifier(id),"Invalid group replica receipt identity");
    return impl_->get("group-replica/"+group+"/"+id);
}
void Storage::commitGroupReplicaReceipt(const GroupEvent& event,const Blob& receipt){
    commitGroupReplicaReceipts({{event,receipt}});
}
void Storage::commitGroupReplicaReceipts(const std::vector<std::pair<GroupEvent,Blob>>& receipts,const std::vector<GroupEvent>& bootstrapMetadata){
    require((!receipts.empty()||!bootstrapMetadata.empty())&&receipts.size()<=32&&bootstrapMetadata.size()<=256,"Invalid group replica recovery page");
    Transaction transaction(impl_->sql);
    const auto& group=receipts.empty()?bootstrapMetadata.front().groupId:receipts.front().first.groupId;
    std::set<std::string> ids;
    for(const auto& event:bootstrapMetadata){
        require(identifier(group,32)&&event.groupId==group&&identifier(event.eventId)&&event.logicalOrder>0&&event.logicalOrder<=INT64_MAX&&
            event.kind>=GroupEventKind::Create&&event.kind<=GroupEventKind::SetRole&&event.message.empty()&&ids.insert(event.eventId).second,
            "Invalid group replica bootstrap metadata");
        auto plain=encodeGroupEvent(event);require(plain.size()<=4096,"Group bootstrap metadata exceeds bound");
        Statement existing(impl_->sql,"SELECT group_id,b FROM group_events WHERE event_id=?");existing.text(1,event.eventId);
        if(existing.row()){
            require(existing.text(0)==group,"Group bootstrap event identity conflict");
            Secret saved{impl_->protect(existing.bytes(1),"PeerChat.v1/group-event/"+event.eventId+"/"+group,false)};
            require(saved.data==plain,"Conflicting group bootstrap event");
        }else{
            auto encrypted=impl_->protect(plain,"PeerChat.v1/group-event/"+event.eventId+"/"+group,true);
            Statement insert(impl_->sql,"INSERT INTO group_events(event_id,group_id,logical_order,b) VALUES(?,?,?,?)");
            insert.text(1,event.eventId);insert.text(2,group);insert.number(3,event.logicalOrder);insert.blob(4,encrypted);insert.row();
        }
    }
    for(const auto& row:receipts){
    const auto& event=row.first;const auto& receipt=row.second;
    require(identifier(event.groupId,32)&&identifier(event.eventId)&&event.logicalOrder>0&&event.logicalOrder<=INT64_MAX&&
        (event.kind==GroupEventKind::AuthorizeDevice||event.kind==GroupEventKind::RevokeDevice)&&
        !receipt.empty()&&receipt.size()<=65536,"Invalid group replica receipt");
    require(event.groupId==group&&ids.insert(event.eventId).second,"Invalid group replica recovery identity");
    auto key="group-replica/"+event.groupId+"/"+event.eventId;
    Secret old{impl_->get(key)};require(old.data.empty()||old.data==receipt,"Conflicting group replica receipt");
    auto plain=encodeGroupEvent(event);Statement existing(impl_->sql,"SELECT group_id,b FROM group_events WHERE event_id=?");existing.text(1,event.eventId);
    if(existing.row()){
        require(existing.text(0)==event.groupId,"Group replica event identity conflict");
        Secret saved{impl_->protect(existing.bytes(1),"PeerChat.v1/group-event/"+event.eventId+"/"+event.groupId,false)};
        require(saved.data==plain,"Conflicting group replica event");
    }else{
        auto encrypted=impl_->protect(plain,"PeerChat.v1/group-event/"+event.eventId+"/"+event.groupId,true);
        Statement insert(impl_->sql,"INSERT INTO group_events(event_id,group_id,logical_order,b) VALUES(?,?,?,?)");
        insert.text(1,event.eventId);insert.text(2,event.groupId);insert.number(3,event.logicalOrder);insert.blob(4,encrypted);insert.row();
    }
    if(old.data.empty())impl_->put(key,receipt);
    }
    transaction.commit();
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
    require(impl_->get("group-history-reference/v1/"+group+"/"+id).empty(),"Conflicting group history record type");
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
        Statement position(impl_->sql,"SELECT sequence FROM group_message_index WHERE server_group=? AND message_id=?");position.text(1,group);position.text(2,id);require(position.row(),"Group history index commit failed");
        indexGroupOrder(impl_->sql,position.number(0),plaintext,envelope);impl_->execute("UPDATE group_order_state_v1 SET revision=revision+1 WHERE id=1");
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
    auto keyFor=[&](const std::string& group,const std::string& id,std::int64_t sequence){
        auto row=authenticatedGroupMessage(group,id);Impl::HistoryKey key;key.id=id;key.group=group;key.sequence=sequence;
        // These are already authenticated protected bodies. References/unknown
        // body types have no timestamp; never invent one from local arrival.
        try{protocol::Reader reader(row.plaintext);auto kind=reader.type();auto type=reader.string();
            if((kind==protocol::Text&&type=="PeerChat.GroupText.v1")||
                (kind==protocol::FileTransfer&&type=="PeerChat.GroupAttachment.v1"))key.time=reader.number();}
        catch(const std::exception&){}
        constexpr size_t senderOffset=4+2+32+8+8+32+8;
        if(row.envelope.size()>=senderOffset+64)key.sender.assign(row.envelope.begin()+senderOffset,row.envelope.begin()+senderOffset+64);
        std::fill(row.plaintext.begin(),row.plaintext.end(),0);return key;
    };
    std::optional<Impl::HistoryKey> anchor;
    if(before!=INT64_MAX){Statement cursor(impl_->sql,"SELECT server_group,message_id FROM group_message_index WHERE local_group=? AND sequence=?");
        cursor.text(1,local);cursor.number(2,before);require(cursor.row(),"Unknown group history page cursor");anchor=keyFor(cursor.text(0),cursor.text(1),before);}
    Statement revision(impl_->sql,"SELECT COALESCE(MAX(sequence),0),COUNT(*) FROM group_message_index WHERE local_group=?");revision.text(1,local);require(revision.row(),"Group history revision unavailable");
    auto keys=impl_->historyPageKeys("group/"+local,revision.number(0),revision.number(1),limit,anchor,[&](const auto& accept){
        Statement rows(impl_->sql,"SELECT server_group,message_id,sequence FROM group_message_index WHERE local_group=?");rows.text(1,local);
        while(rows.row())accept(keyFor(rows.text(0),rows.text(1),rows.number(2)));
    });
    std::vector<AuthenticatedGroupMessage> page;for(const auto& key:keys){auto item=authenticatedGroupMessage(key.group,key.id);item.sequence=key.sequence;page.push_back(std::move(item));}return page;
}
bool Storage::groupHistoryOrderingReady(const std::string& local,int batch){
    require(identifier(local,32)&&batch>0&&batch<=64,"Invalid group history indexing batch");
    Transaction transaction(impl_->sql);
    Statement q(impl_->sql,"SELECT i.server_group,i.message_id,i.sequence FROM group_message_index i LEFT JOIN group_message_order_v1 o ON o.sequence=i.sequence WHERE i.local_group=? AND o.sequence IS NULL ORDER BY i.sequence LIMIT ?");q.text(1,local);q.number(2,batch);
    bool changed=false;
    while(q.row()){
        auto row=authenticatedGroupMessage(q.text(0),q.text(1));indexGroupOrder(impl_->sql,q.number(2),row.plaintext,row.envelope);
        std::fill(row.plaintext.begin(),row.plaintext.end(),0);
        changed=true;
    }
    if(changed)impl_->execute("UPDATE group_order_state_v1 SET revision=revision+1 WHERE id=1");
    Statement more(impl_->sql,"SELECT 1 FROM group_message_index i LEFT JOIN group_message_order_v1 o ON o.sequence=i.sequence WHERE i.local_group=? AND o.sequence IS NULL LIMIT 1");more.text(1,local);bool ready=!more.row();transaction.commit();return ready;
}
std::uint64_t Storage::groupHistoryOrderRevision(){Statement q(impl_->sql,"SELECT revision FROM group_order_state_v1 WHERE id=1");require(q.row()&&q.number(0)>=0,"Group history revision unavailable");return static_cast<std::uint64_t>(q.number(0));}
bool Storage::setGroupMessageStatus(const std::string& group,const std::string& id,const std::string& status,std::uint32_t stage){
    require(identifier(group,32)&&identifier(id,32)&&status.size()<=160&&stage<=2,"Invalid group delivery status");
    Secret saved{impl_->get("authenticated-group-message/"+group+"/"+id)};require(!saved.data.empty(),"Unknown group message status");
    const auto key="group-message-status/"+group+"/"+id;Secret previous{impl_->get(key)};
    if(!previous.data.empty()){Reader reader(previous.data);reader.string();auto prior=reader.remaining()?reader.number():0;reader.end();if(prior>stage)return false;}
    // 0 local/uncertain, 1 server admission, 2 recipient validation report.
    // A late admission/disconnect cannot overwrite a known recipient receipt.
    Writer writer;writer.string(status);writer.number(stage);impl_->put(key,writer.data);return true;
}
std::int64_t Storage::groupArchiveWatermark(const std::string& group){
    if(!group.empty()){
        require(identifier(group,32),"Invalid group archive scope");Statement q(impl_->sql,"SELECT COALESCE(MAX(sequence),0) FROM group_message_index WHERE server_group=?");q.text(1,group);require(q.row(),"Group archive watermark unavailable");return q.number(0);
    }
    Statement q(impl_->sql,"SELECT seq FROM sqlite_sequence WHERE name='group_message_index'");
    if(!q.row())return 0;
    auto value=q.number(0);require(value>=0,"Invalid group archive watermark");return value;
}
GroupArchivePage Storage::groupArchivePage(std::int64_t after,std::int64_t through,int limit,const std::string& group){
    require(after>=0&&through>=after&&through<=groupArchiveWatermark(group)&&limit>0&&limit<=8,"Invalid group archive page");
    GroupArchivePage page;page.next=after;size_t bytes=512;
    Statement q(impl_->sql,group.empty()?"SELECT server_group,message_id,sequence FROM group_message_index WHERE sequence>? AND sequence<=? ORDER BY sequence LIMIT ?":"SELECT server_group,message_id,sequence FROM group_message_index WHERE sequence>? AND sequence<=? AND server_group=? ORDER BY sequence LIMIT ?");
    q.number(1,after);q.number(2,through);if(group.empty())q.number(3,limit);else{q.text(3,group);q.number(4,limit);}
    while(q.row()){
        auto item=authenticatedGroupMessage(q.text(0),q.text(1));
        // Plaintext never enters archive serialization or the relay.
        std::fill(item.plaintext.begin(),item.plaintext.end(),0);item.plaintext.clear();item.sequence=q.number(2);
        if(!page.records.empty()&&bytes+item.envelope.size()+32>69000)break;
        require(item.envelope.size()+544<=70000,"Group archive envelope exceeds wire bound");
        bytes+=item.envelope.size()+32;page.next=item.sequence;page.records.push_back(std::move(item));
    }
    Statement more(impl_->sql,group.empty()?"SELECT 1 FROM group_message_index WHERE sequence>? AND sequence<=? LIMIT 1":"SELECT 1 FROM group_message_index WHERE sequence>? AND sequence<=? AND server_group=? LIMIT 1");more.number(1,page.next);more.number(2,through);if(!group.empty())more.text(3,group);page.complete=!more.row();
    if(page.complete)page.next=through;
    return page;
}
bool Storage::appendGroupHistoryReference(const std::string& group,const std::string& local,const std::string& id,const Blob& descriptor){
    require(identifier(group,32)&&identifier(local,32)&&identifier(id,32)&&!descriptor.empty()&&descriptor.size()<=2048,"Invalid group reference");
    Transaction tx(impl_->sql);require(impl_->get("authenticated-group-message/"+group+"/"+id).empty(),"Conflicting group history record type");const auto key="group-history-reference/v1/"+group+"/"+id;auto old=impl_->get(key);
    if(!old.empty()){require(old==descriptor,"Conflicting group reference identity");tx.commit();return false;}
    impl_->put(key,descriptor);Statement q(impl_->sql,"INSERT INTO group_reference_index_v1(server_group,local_group,message_id) VALUES(?,?,?)");q.text(1,group);q.text(2,local);q.text(3,id);q.row();tx.commit();return true;
}
Blob Storage::groupHistoryReference(const std::string& group,const std::string& id){
    require(identifier(group,32)&&identifier(id,32),"Invalid group reference identity");auto value=impl_->get("group-history-reference/v1/"+group+"/"+id);require(value.size()<=2048,"Invalid group reference record");return value;
}
std::int64_t Storage::groupReferenceWatermark(const std::string& group){
    require(group.empty()||identifier(group,32),"Invalid group reference scope");Statement q(impl_->sql,group.empty()?"SELECT COALESCE(MAX(sequence),0) FROM group_reference_index_v1":"SELECT COALESCE(MAX(sequence),0) FROM group_reference_index_v1 WHERE server_group=?");if(!group.empty())q.text(1,group);require(q.row(),"Missing reference watermark");return q.number(0);
}
GroupArchivePage Storage::groupReferencePage(std::int64_t after,std::int64_t through,int limit,const std::string& group){
    require(after>=0&&through>=after&&through<=groupReferenceWatermark(group)&&limit>0&&limit<=8,"Invalid reference page");
    Statement q(impl_->sql,group.empty()?"SELECT server_group,message_id,sequence FROM group_reference_index_v1 WHERE sequence>? AND sequence<=? ORDER BY sequence LIMIT ?":"SELECT server_group,message_id,sequence FROM group_reference_index_v1 WHERE sequence>? AND sequence<=? AND server_group=? ORDER BY sequence LIMIT ?");q.number(1,after);q.number(2,through);if(group.empty())q.number(3,limit);else{q.text(3,group);q.number(4,limit);}
    GroupArchivePage page;page.next=after;while(q.row()){AuthenticatedGroupMessage row;row.envelope=groupHistoryReference(q.text(0),q.text(1));require(!row.envelope.empty(),"Missing protected reference");row.sequence=q.number(2);page.next=row.sequence;page.records.push_back(std::move(row));}
    Statement more(impl_->sql,group.empty()?"SELECT 1 FROM group_reference_index_v1 WHERE sequence>? AND sequence<=? LIMIT 1":"SELECT 1 FROM group_reference_index_v1 WHERE sequence>? AND sequence<=? AND server_group=? LIMIT 1");more.number(1,page.next);more.number(2,through);if(!group.empty())more.text(3,group);page.complete=!more.row();if(page.complete)page.next=through;return page;
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
    return pendingGroupKeyHandoffs(recipientPeerId,"",0,limit);
}
std::vector<GroupKeyHandoff> Storage::pendingGroupKeyHandoffs(const std::string& recipientPeerId,const std::string& group,std::uint64_t generation,int limit){
    require(identifier(recipientPeerId)&&limit>0&&limit<=256,"Invalid group key outbox request");
    require((group.empty()&&!generation)||(identifier(group,32)&&generation>0&&generation<=INT64_MAX),"Invalid scoped group key outbox request");
    Statement statement(impl_->sql,group.empty()?"SELECT id,b FROM group_key_outbox WHERE recipient_peer=? ORDER BY rowid LIMIT ?":"SELECT id,b FROM group_key_outbox WHERE recipient_peer=? AND group_id=? AND key_generation=? ORDER BY rowid LIMIT ?");
    statement.text(1,recipientPeerId);if(group.empty())statement.number(2,limit);else{statement.text(2,group);statement.number(3,generation);statement.number(4,limit);}
    std::vector<GroupKeyHandoff> pending;
    while(statement.row()){std::string id=statement.text(0);Secret plain{impl_->protect(statement.bytes(1),"PeerChat.v1/group-key-handoff/"+id+"/"+recipientPeerId,false)};Reader reader(plain.data);GroupKeyHandoff offer;offer.id=reader.string();offer.groupId=reader.string();offer.serverGroupId=reader.string();offer.distributorPeerId=reader.string();offer.recipientPeerId=reader.string();offer.rosterGeneration=reader.number();offer.keyGeneration=reader.number();offer.contentKey=reader.bytes();reader.end();require(offer.id==id&&offer.recipientPeerId==recipientPeerId&&identifier(offer.groupId,32)&&identifier(offer.serverGroupId,32)&&identifier(offer.distributorPeerId)&&offer.contentKey.size()==32,"Stored group key handoff identity mismatch");pending.push_back(std::move(offer));}
    for(const auto& offer:pending)require(group.empty()||(offer.groupId==group&&offer.keyGeneration==generation),"Stored group key handoff scope mismatch");
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
