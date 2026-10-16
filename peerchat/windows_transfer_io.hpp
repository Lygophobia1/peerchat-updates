#pragma once
#include "transfer_manager.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <chrono>
#include <set>
#include "platform/windows_record_protection.hpp"

namespace peerchat::transfer::windows {
inline void require(bool ok){if(!ok)throw std::runtime_error("File I/O failed (Win32 "+std::to_string(GetLastError())+")");}
inline void fileIdentity(protocol::Writer& w,HANDLE file,bool source){
    BY_HANDLE_FILE_INFORMATION i{};require(GetFileInformationByHandle(file,&i)!=0);
    require(!(i.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)));
    w.number(i.dwVolumeSerialNumber);w.number(i.nFileIndexHigh);w.number(i.nFileIndexLow);
    if(source){w.number(i.ftLastWriteTime.dwHighDateTime);w.number(i.ftLastWriteTime.dwLowDateTime);}
}
inline void checkIdentity(protocol::Reader& r,HANDLE file,bool source){
    protocol::Writer w(protocol::FileTransfer);fileIdentity(w,file,source);protocol::Reader expected(w.data);
    for(unsigned n=0;n<(source?5u:3u);++n)require(r.number()==expected.number());
}
// Protected cleanup journal, not an outbox or retry owner. Abrupt process death
// may leave a partial; next profile open removes only its authenticated exact
// partial pathname. It never resumes or claims a completed file after restart.
class CleanupJournal {
    std::filesystem::path root_;
    static constexpr const char* Context="PeerChat/FileTransfer/PartialCleanup/v1";
public:
    explicit CleanupJournal(const std::wstring& profile):root_(std::filesystem::path(profile)/L"transfer-cleanup"){
        std::filesystem::create_directory(root_);
        auto attributes=GetFileAttributesW(root_.c_str());
        require(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY)&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT));
    }
    std::wstring record(const std::string& id,const std::wstring& partial,HANDLE ownedFile){
        require(protocol::id(id,32));
        BY_HANDLE_FILE_INFORMATION identity{};require(GetFileInformationByHandle(ownedFile,&identity)!=0);
        auto plain=protocol::Writer(protocol::FileTransfer);plain.string(id);plain.text(partial);
        plain.number(identity.dwVolumeSerialNumber);plain.number(identity.nFileIndexHigh);plain.number(identity.nFileIndexLow);
        platform::WindowsRecordProtector protector;auto bytes=protector.protect(plain.data,Context);
        auto path=(root_/(id+".cleanup")).wstring();
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        require(file!=INVALID_HANDLE_VALUE);DWORD written=0;
        bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file);
        CloseHandle(file);if(!ok){DeleteFileW(path.c_str());throw std::runtime_error("Cannot journal partial file");}
        return path;
    }
    // Only IDs from successfully loaded protected staging may be retained.
    // Leave their authenticated records intact until the staged file is adopted.
    // The default preserves ordinary transfer cleanup behavior.
    void recover(const std::set<std::string>& retained={}){
        require(retained.size()<=MaxRecords);
        for(const auto& id:retained)require(protocol::id(id,32));
        size_t count=0;
        for(auto& item:std::filesystem::directory_iterator(root_)){
            require(++count<=MaxRecords);
            auto name=item.path().filename().string();
            if(name.size()!=40||name.substr(32)!=".cleanup"||!protocol::id(name.substr(0,32),32))continue;
            HANDLE file=CreateFileW(item.path().c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            require(file!=INVALID_HANDLE_VALUE);BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
            if(!GetFileInformationByHandle(file,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||!GetFileSizeEx(file,&size)||size.QuadPart<1||size.QuadPart>16384){CloseHandle(file);throw std::runtime_error("Invalid file cleanup record");}
            Blob bytes(static_cast<size_t>(size.QuadPart));DWORD read=0;bool ok=ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)&&read==bytes.size();CloseHandle(file);require(ok);
            platform::WindowsRecordProtector protector;auto plain=protector.unprotect(bytes,Context);protocol::Reader record(plain);
            auto id=record.string();auto partial=record.text();
            auto volume=record.number(),indexHigh=record.number(),indexLow=record.number();record.end();
            auto path=std::filesystem::path(partial);
            require(record.type()==protocol::FileTransfer&&id==name.substr(0,32)&&path.is_absolute()&&path.filename()==id+".peerchat-part");
            if(retained.count(id))continue;
            HANDLE owned=CreateFileW(path.c_str(),DELETE|FILE_READ_ATTRIBUTES,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            if(owned!=INVALID_HANDLE_VALUE){
                BY_HANDLE_FILE_INFORMATION identity{};FILE_DISPOSITION_INFO disposition{TRUE};
                bool matches=GetFileInformationByHandle(owned,&identity)&&!(identity.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))
                    &&identity.dwVolumeSerialNumber==volume&&identity.nFileIndexHigh==indexHigh&&identity.nFileIndexLow==indexLow;
                bool removed=matches&&SetFileInformationByHandle(owned,FileDispositionInfo,&disposition,sizeof(disposition));
                CloseHandle(owned);require(removed);
            }else require(GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND);
            require(DeleteFileW(item.path().c_str())!=0);
        }
    }
};
class Sha256 final:public Hash {
    BCRYPT_ALG_HANDLE algorithm_=nullptr;
    BCRYPT_HASH_HANDLE hash_=nullptr;
public:
    Sha256(){
        require(BCryptOpenAlgorithmProvider(&algorithm_,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0);
        if(BCryptCreateHash(algorithm_,&hash_,nullptr,0,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(algorithm_,0);algorithm_=nullptr;throw std::runtime_error("SHA-256 unavailable");}
    }
    ~Sha256(){if(hash_)BCryptDestroyHash(hash_);if(algorithm_)BCryptCloseAlgorithmProvider(algorithm_,0);}
    void update(const unsigned char* bytes,size_t size)override{require(hash_&&size<=ChunkBytes&&BCryptHashData(hash_,const_cast<PUCHAR>(bytes),static_cast<ULONG>(size),0)>=0);}
    Digest finish()override{Digest result{};require(hash_&&BCryptFinishHash(hash_,result.data(),32,0)>=0);BCryptDestroyHash(hash_);hash_=nullptr;return result;}
};
class FileSource final:public Source {
    HANDLE handle_=INVALID_HANDLE_VALUE;
    std::uint64_t size_=0;
    std::string name_;
    Blob token_;
public:
    explicit FileSource(const std::wstring& path,const std::string& logicalName={},Purpose purpose=Purpose::PeerFile){
        require(purpose==Purpose::PeerFile||purpose==Purpose::GroupCacheCiphertext);
        name_=logicalName.empty()?unicode::encode(std::filesystem::path(path).filename().wstring()):logicalName;require(safeName(name_));
        handle_=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(handle_==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open file");
        BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
        if(!GetFileInformationByHandle(handle_,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||!GetFileSizeEx(handle_,&size)||size.QuadPart<0||static_cast<std::uint64_t>(size.QuadPart)>(purpose==Purpose::PeerFile?MaxFileBytes:MaxCacheCipherBytes)){
            CloseHandle(handle_);handle_=INVALID_HANDLE_VALUE;throw std::runtime_error("Unsupported source file");
        }
        size_=static_cast<std::uint64_t>(size.QuadPart);
        try{protocol::Writer token(protocol::FileTransfer);token.text(std::filesystem::absolute(path).wstring());fileIdentity(token,handle_,true);
            if(!logicalName.empty()){token.string("PeerChat/LogicalFileName/v1");token.string(logicalName);}token_=std::move(token.data);}
        catch(...){CloseHandle(handle_);handle_=INVALID_HANDLE_VALUE;throw;}
    }
    ~FileSource(){if(handle_!=INVALID_HANDLE_VALUE)CloseHandle(handle_);}
    std::uint64_t size()const override{return size_;}
    std::string name()const override{return name_;}
    Blob resumeToken()const override{return token_;}
    static std::unique_ptr<FileSource> reopen(const DurableState& d){
        protocol::Reader r(d.location);require(r.type()==protocol::FileTransfer);auto path=r.text();
        require(std::filesystem::path(path).is_absolute());auto source=std::make_unique<FileSource>(path,std::string{},d.purpose);
        checkIdentity(r,source->handle_,true);
        if(r.remaining()){require(r.string()=="PeerChat/LogicalFileName/v1");source->name_=r.string();require(safeName(source->name_));source->token_=d.location;}
        r.end();return source;
    }
    Blob read(std::uint64_t offset,size_t limit)override{
        require(offset<=size_&&limit<=ChunkBytes&&limit<=size_-offset);LARGE_INTEGER position{};position.QuadPart=offset;
        require(SetFilePointerEx(handle_,position,nullptr,FILE_BEGIN)!=0);Blob result(limit);DWORD got=0;
        require(ReadFile(handle_,result.data(),static_cast<DWORD>(limit),&got,nullptr)!=0);result.resize(got);return result;
    }
};
// Generated chat text is not a user-owned plaintext input file. Keep its one
// resumable source DPAPI-protected; decrypt only inside the existing Source.
class ProtectedSnippetSource final:public Source {
    std::unique_ptr<FileSource> record_;Blob plain_,token_;std::string name_;
public:
    static constexpr const char* Domain="PeerChat/ProtectedSnippetSource/v1";
    static constexpr size_t MaxBytes=512*1024;
    static constexpr const char* MessageName="message.peerchat-text";
    explicit ProtectedSnippetSource(const std::wstring& path,std::string name="message.txt"):name_(std::move(name)){
        require(name_=="message.txt"||name_==MessageName);
        auto id=std::filesystem::path(path).stem().string();require(protocol::id(id,32));
        record_=std::make_unique<FileSource>(path);require(record_->size()<=1024*1024);
        Blob sealed;for(std::uint64_t offset=0;offset<record_->size();offset+=ChunkBytes){auto part=record_->read(offset,std::min<std::uint64_t>(ChunkBytes,record_->size()-offset));sealed.insert(sealed.end(),part.begin(),part.end());}
        platform::WindowsRecordProtector protector;plain_=protector.unprotect(sealed,std::string(Domain)+"/"+id);require(plain_.size()<=MaxBytes);
        protocol::Writer token(protocol::FileTransfer);token.data=record_->resumeToken();token.string(Domain);token.string(name_);token_=std::move(token.data);
    }
    ~ProtectedSnippetSource(){if(!plain_.empty())SecureZeroMemory(plain_.data(),plain_.size());}
    std::uint64_t size()const override{return plain_.size();}
    std::string name()const override{return name_;}
    Blob read(std::uint64_t offset,size_t count)override{require(offset<=plain_.size()&&count<=ChunkBytes&&count<=plain_.size()-offset);return Blob(plain_.begin()+offset,plain_.begin()+offset+count);}
    Blob resumeToken()const override{return token_;}
    static std::unique_ptr<Source> create(const std::wstring& path,const Blob& bytes){
        require(!bytes.empty()&&bytes.size()<=MaxBytes);auto id=std::filesystem::path(path).stem().string();require(protocol::id(id,32));
        platform::WindowsRecordProtector protector;auto sealed=protector.protect(bytes,std::string(Domain)+"/"+id);
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);require(file!=INVALID_HANDLE_VALUE);DWORD written=0;
        bool ok=WriteFile(file,sealed.data(),static_cast<DWORD>(sealed.size()),&written,nullptr)&&written==sealed.size()&&FlushFileBuffers(file);CloseHandle(file);
        if(!ok){DeleteFileW(path.c_str());throw std::runtime_error("Could not persist protected snippet");}
        try{return std::make_unique<ProtectedSnippetSource>(path,MessageName);}catch(...){DeleteFileW(path.c_str());throw;}
    }
    static std::unique_ptr<Source> reopen(const DurableState& state){
        protocol::Reader r(state.location);require(r.type()==protocol::FileTransfer);auto path=r.text();for(int n=0;n<5;n++)r.number();require(r.string()==Domain);auto name=r.string();r.end();auto source=std::make_unique<ProtectedSnippetSource>(path,name);require(source->resumeToken()==state.location);return source;
    }
};
inline bool protectedSnippetToken(const Blob& token){
    try{protocol::Reader r(token);require(r.type()==protocol::FileTransfer);r.text();for(int n=0;n<5;n++)r.number();if(!r.remaining())return false;auto domain=r.string();if(domain!=ProtectedSnippetSource::Domain)return false;auto name=r.string();require(name=="message.txt"||name==ProtectedSnippetSource::MessageName);r.end();return true;}catch(...){return false;}
}
inline std::unique_ptr<Source> reopenSource(const DurableState& state){return protectedSnippetToken(state.location)?ProtectedSnippetSource::reopen(state):FileSource::reopen(state);}
class FileSink final:public Sink {
    HANDLE handle_=INVALID_HANDLE_VALUE,directory_=INVALID_HANDLE_VALUE;
    std::wstring partial_,final_,journal_;
    bool committed_=false;
    bool durable_=false;
    Blob token_;
public:
    FileSink(const std::wstring& directory,const Metadata& metadata,CleanupJournal* journal=nullptr,bool durable=false,bool originalName=false,const std::string& selectedBasename={}):durable_(durable){
        require(protocol::id(metadata.id,32)&&safeName(metadata.name)&&(!originalName||selectedBasename.empty())&&(selectedBasename.empty()||safeName(selectedBasename)));
        if(!selectedBasename.empty()){
            auto selected=unicode::decode(selectedBasename),partialName=unicode::decode(metadata.id+".peerchat-part");
            require(CompareStringOrdinal(selected.c_str(),-1,partialName.c_str(),-1,TRUE)!=CSTR_EQUAL);
        }
        // Hold the destination directory without delete sharing. Reject junctions
        // and symlinks; never derive a destination directory from remote metadata.
        directory_=CreateFileW(directory.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(directory_==INVALID_HANDLE_VALUE)throw std::runtime_error("Destination unavailable");
        BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(directory_,&info)||!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){CloseHandle(directory_);directory_=INVALID_HANDLE_VALUE;throw std::runtime_error("Unsafe destination directory");}
        // Unique names prevent collisions with existing files and two equal
        // remote basenames. Never use MOVEFILE_REPLACE_EXISTING.
        auto base=std::filesystem::path(directory);
        if(originalName){
            // The transfer ID owns an exclusive child directory. Its file keeps
            // the sender's exact basename without colliding with another offer.
            base/=unicode::decode(metadata.id)+L".peerchat-received";
            if(!CreateDirectoryW(base.c_str(),nullptr)){CloseHandle(directory_);directory_=INVALID_HANDLE_VALUE;throw std::runtime_error("Receive directory already exists");}
            HANDLE child=CreateFileW(base.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            BY_HANDLE_FILE_INFORMATION childInfo{};
            if(child==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(child,&childInfo)||!(childInfo.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(childInfo.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){
                if(child!=INVALID_HANDLE_VALUE)CloseHandle(child);
                CloseHandle(directory_);directory_=INVALID_HANDLE_VALUE;
                RemoveDirectoryW(base.c_str());throw std::runtime_error("Unsafe receive directory");
            }
            CloseHandle(directory_);directory_=child;
        }
        final_=(base/(!selectedBasename.empty()?unicode::decode(selectedBasename):originalName?unicode::decode(metadata.name):unicode::decode(metadata.id)+L"-"+unicode::decode(metadata.name))).wstring();
        partial_=(base/(unicode::decode(metadata.id)+L".peerchat-part")).wstring();
        if(!selectedBasename.empty()){
            auto attributes=GetFileAttributesW(final_.c_str());
            if(attributes!=INVALID_FILE_ATTRIBUTES||GetLastError()!=ERROR_FILE_NOT_FOUND){CloseHandle(directory_);directory_=INVALID_HANDLE_VALUE;throw std::runtime_error("Selected attachment destination already exists or is unavailable");}
        }
        handle_=CreateFileW(partial_.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(handle_==INVALID_HANDLE_VALUE){if(!journal_.empty())DeleteFileW(journal_.c_str());CloseHandle(directory_);directory_=INVALID_HANDLE_VALUE;if(originalName)RemoveDirectoryW(base.c_str());throw std::runtime_error("Cannot create partial file");}
        try{if(journal)journal_=journal->record(metadata.id,partial_,handle_);
            protocol::Writer token(protocol::FileTransfer);token.text(std::filesystem::absolute(partial_).wstring());token.text(std::filesystem::absolute(final_).wstring());fileIdentity(token,handle_,false);
            if(!selectedBasename.empty()){token.string("PeerChat/SelectedFileName/v1");token.string(metadata.name);token.string(selectedBasename);}
            token_=std::move(token.data);
        }
        catch(...){CloseHandle(handle_);handle_=INVALID_HANDLE_VALUE;DeleteFileW(partial_.c_str());CloseHandle(directory_);directory_=INVALID_HANDLE_VALUE;if(originalName)RemoveDirectoryW(base.c_str());throw;}
    }
    ~FileSink(){
        if(handle_!=INVALID_HANDLE_VALUE)CloseHandle(handle_);
        bool removed=committed_||(!durable_&&(partial_.empty()||DeleteFileW(partial_.c_str())||GetLastError()==ERROR_FILE_NOT_FOUND));
        if(removed&&!journal_.empty())DeleteFileW(journal_.c_str());
        if(directory_!=INVALID_HANDLE_VALUE)CloseHandle(directory_);
    }
    void append(const Blob& bytes)override{DWORD wrote=0;require(handle_!=INVALID_HANDLE_VALUE&&bytes.size()<=ChunkBytes&&WriteFile(handle_,bytes.data(),static_cast<DWORD>(bytes.size()),&wrote,nullptr)&&wrote==bytes.size());}
    void flush()override{require(handle_!=INVALID_HANDLE_VALUE&&FlushFileBuffers(handle_));}
    Blob resumeToken()const override{return token_;}
    void discard()override{
        if(committed_)return;
        if(handle_!=INVALID_HANDLE_VALUE){FILE_DISPOSITION_INFO disposition{TRUE};
            require(SetFileInformationByHandle(handle_,FileDispositionInfo,&disposition,sizeof(disposition))!=0);
            CloseHandle(handle_);handle_=INVALID_HANDLE_VALUE;
        }
        durable_=false;
    }
    Blob read(std::uint64_t offset,size_t size)override{
        require(size<=ChunkBytes);LARGE_INTEGER pos{};pos.QuadPart=offset;require(SetFilePointerEx(handle_,pos,nullptr,FILE_BEGIN));
        Blob b(size);DWORD got=0;require(ReadFile(handle_,b.data(),static_cast<DWORD>(size),&got,nullptr)&&got==size);return b;
    }
    static std::unique_ptr<FileSink> reopen(const DurableState& d,bool immutableComplete=false){
        require(!immutableComplete||d.done==d.metadata.size);
        protocol::Reader r(d.location);require(r.type()==protocol::FileTransfer);auto partial=r.text(),final=r.text();
        auto p=std::filesystem::path(partial),f=std::filesystem::path(final);
        require(protocol::id(d.metadata.id,32)&&safeName(d.metadata.name));
        bool legacy=f.filename()==unicode::decode(d.metadata.id+"-"+d.metadata.name);
        bool original=p.parent_path().filename()==unicode::decode(d.metadata.id)+L".peerchat-received"&&f.filename()==unicode::decode(d.metadata.name);
        require(p.is_absolute()&&f.is_absolute()&&p.parent_path()==f.parent_path()&&p.filename()==d.metadata.id+".peerchat-part");
        auto sink=std::unique_ptr<FileSink>(new FileSink());
        sink->durable_=true;sink->partial_=partial;sink->final_=final;sink->token_=d.location;
        sink->directory_=CreateFileW(p.parent_path().c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        require(sink->directory_!=INVALID_HANDLE_VALUE);
        BY_HANDLE_FILE_INFORMATION dir{};require(GetFileInformationByHandle(sink->directory_,&dir)&&
            (dir.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!(dir.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT));
        sink->handle_=CreateFileW(partial.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(sink->handle_==INVALID_HANDLE_VALUE){
            require(GetLastError()==ERROR_FILE_NOT_FOUND&&d.done==d.metadata.size);
            sink->handle_=CreateFileW(final.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            require(sink->handle_!=INVALID_HANDLE_VALUE);sink->committed_=true;
        }
        checkIdentity(r,sink->handle_,false);
        if(r.remaining()){require(r.string()=="PeerChat/SelectedFileName/v1"&&r.string()==d.metadata.name);auto selected=r.string();auto name=unicode::decode(selected),part=unicode::decode(d.metadata.id+".peerchat-part");require(safeName(selected)&&f.filename()==name&&CompareStringOrdinal(name.c_str(),-1,part.c_str(),-1,TRUE)!=CSTR_EQUAL);}
        else require(legacy||original);
        r.end();LARGE_INTEGER size{};require(GetFileSizeEx(sink->handle_,&size)&&size.QuadPart>=0&&static_cast<std::uint64_t>(size.QuadPart)>=d.done);
        if(immutableComplete)require(static_cast<std::uint64_t>(size.QuadPart)==d.metadata.size);
        if(sink->committed_)require(static_cast<std::uint64_t>(size.QuadPart)==d.metadata.size);
        else{LARGE_INTEGER end{};end.QuadPart=d.done;require(SetFilePointerEx(sink->handle_,end,nullptr,FILE_BEGIN)&&SetEndOfFile(sink->handle_)&&FlushFileBuffers(sink->handle_));}
        return sink;
    }
    void commit()override{
        if(committed_)return; // Rename completed before a prior process died.
        require(handle_!=INVALID_HANDLE_VALUE&&FlushFileBuffers(handle_));require(CloseHandle(handle_)!=0);handle_=INVALID_HANDLE_VALUE;
        require(MoveFileExW(partial_.c_str(),final_.c_str(),MOVEFILE_WRITE_THROUGH)!=0);committed_=true;
    }
    const std::wstring& path()const{return final_;}
private:
    FileSink()=default;
};

// One bounded, profile/device-bound protected snapshot; no scheduler or outbox.
// Temp write + flush + atomic replace ensures old or new committed state.
class DurableJournal final:public StateStore {
    std::filesystem::path root_,path_;
    std::string context_;
    Blob previous_;
    std::vector<DurableState> committedRecords_;
    struct JournalCost {std::uint64_t commits=0,microseconds=0,protectUs=0,commitUs=0;};
    std::map<std::string,JournalCost> costs_;
    std::uint64_t deferredAcknowledgements_=0;
    DWORD lastCommitError_=0;
    static bool sameStoredRecord(const DurableState& a,const DurableState& b){return a.metadata==b.metadata&&a.peer==b.peer&&a.state==b.state&&a.sending==b.sending&&a.digestReady==b.digestReady&&a.done==b.done&&a.highWaterSent==b.highWaterSent&&a.created==b.created&&a.updated==b.updated&&a.expires==b.expires&&a.location==b.location&&a.error==b.error;}
    static std::uint64_t clockUs(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
    bool redundantSenderAcknowledgement(const std::vector<DurableState>& records)const{
        if(records.size()!=committedRecords_.size())return false;
        bool advanced=false;
        for(size_t i=0;i<records.size();++i){const auto& a=committedRecords_[i];const auto& b=records[i];
            if(!(a.metadata==b.metadata)||a.peer!=b.peer||a.state!=b.state||a.sending!=b.sending||a.digestReady!=b.digestReady||
                a.highWaterSent!=b.highWaterSent||a.created!=b.created||a.updated!=b.updated||a.expires!=b.expires||a.location!=b.location||a.error!=b.error)return false;
            if(a.done!=b.done){
                if(!b.sending||b.state!=State::Transferring||b.done<a.done||b.done>b.highWaterSent)return false;
                advanced=true;
            }
        }return advanced;
    }
    HANDLE lock_=INVALID_HANDLE_VALUE;
    std::uint64_t serial_=GetTickCount64();
    static Blob readFile(const std::filesystem::path& path){
        HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_FILE_NOT_FOUND)return {};throw std::runtime_error("Cannot open transfer journal");}
        BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
        bool valid=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&GetFileSizeEx(file,&size)&&size.QuadPart>0&&size.QuadPart<=1024*1024;
        if(!valid){CloseHandle(file);throw std::runtime_error("Invalid transfer journal");}
        Blob result(static_cast<size_t>(size.QuadPart));DWORD got=0;bool ok=ReadFile(file,result.data(),static_cast<DWORD>(result.size()),&got,nullptr)&&got==result.size();CloseHandle(file);require(ok);return result;
    }
public:
    std::string diagnostics(const std::string& id)const{
        auto i=costs_.find(id);auto c=i==costs_.end()?JournalCost{}:i->second;
        return " journal_selected_commits="+std::to_string(c.commits)+" journal_selected_us="+std::to_string(c.microseconds)+" journal_selected_protect_us="+std::to_string(c.protectUs)+" journal_selected_commit_us="+std::to_string(c.commitUs)+" journal_sender_ack_deferrals_all="+std::to_string(deferredAcknowledgements_)+" journal_last_win32_error="+std::to_string(lastCommitError_);
    }
    DurableJournal(const std::wstring& profile,const std::string& device):root_(std::filesystem::absolute(profile)/L"transfer-resume"),path_(root_/L"state.protected"){
        require(protocol::id(device));std::filesystem::create_directory(root_);
        auto attributes=GetFileAttributesW(root_.c_str());require(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY)&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT));
        context_="PeerChat/FileTransfer/Resume/v1/"+device+"/"+unicode::encode(std::filesystem::absolute(profile).wstring());
        auto lockPath=root_/L"owner.lock";
        lock_=CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        require(lock_!=INVALID_HANDLE_VALUE);BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(lock_,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))){CloseHandle(lock_);lock_=INVALID_HANDLE_VALUE;throw std::runtime_error("Unsafe transfer journal lock");}
    }
    ~DurableJournal(){if(lock_!=INVALID_HANDLE_VALUE)CloseHandle(lock_);}
    DurableJournal(const DurableJournal&)=delete;
    DurableJournal& operator=(const DurableJournal&)=delete;
    std::uint64_t wallTime()const override{return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());}
    std::optional<std::uint64_t> loadDownloadLimit()override{
        auto sealed=readFile(root_/L"download-limit.protected");if(sealed.empty())return {};
        platform::WindowsRecordProtector protector;auto plain=protector.unprotect(sealed,context_+"/DownloadLimit/v1");protocol::Reader r(plain);
        require(r.type()==protocol::FileTransfer&&r.number()==1);auto bits=r.number();r.end();require(bits<=10000000000ull);return bits;
    }
    void saveDownloadLimit(std::uint64_t bits)override{
        require(bits<=10000000000ull);auto prior=loadDownloadLimit();if(prior&&*prior==bits)return;
        protocol::Writer w(protocol::FileTransfer);w.number(1);w.number(bits);platform::WindowsRecordProtector protector;auto sealed=protector.protect(w.data,context_+"/DownloadLimit/v1");
        auto path=root_/L"download-limit.protected",temp=root_/(L"download-limit-"+std::to_wstring(++serial_)+L".next");
        HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);require(file!=INVALID_HANDLE_VALUE);DWORD wrote=0;
        bool ok=WriteFile(file,sealed.data(),static_cast<DWORD>(sealed.size()),&wrote,nullptr)&&wrote==sealed.size()&&FlushFileBuffers(file);CloseHandle(file);
        if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
        if(!ok){DeleteFileW(temp.c_str());throw std::runtime_error("Cannot commit protected download settings");}
    }
    std::map<std::string,Options> loadOptions()override{
        auto encrypted=readFile(root_/L"tuning.protected");if(encrypted.empty())return {};
        platform::WindowsRecordProtector protector;auto plain=protector.unprotect(encrypted,context_+"/Tuning/v1");protocol::Reader r(plain);
        require(r.type()==protocol::FileTransfer&&r.number()==1);auto count=r.number();require(count<=MaxRecords+1);std::map<std::string,Options> result;
        while(count--){auto id=r.string();Options value;value.chunkBytes=r.number();value.window=r.number();value.bitsPerSecond=r.number();require((id.empty()||protocol::id(id,32))&&value.valid()&&!result.count(id));result[id]=value;}r.end();return result;
    }
    void saveOptions(const std::map<std::string,Options>& rows)override{
        require(rows.size()<=MaxRecords+1);protocol::Writer w(protocol::FileTransfer);w.number(1);w.number(rows.size());for(const auto& row:rows){require((row.first.empty()||protocol::id(row.first,32))&&row.second.valid());w.string(row.first);w.number(row.second.chunkBytes);w.number(row.second.window);w.number(row.second.bitsPerSecond);}
        auto path=root_/L"tuning.protected";platform::WindowsRecordProtector protector;
        auto existing=readFile(path);if(!existing.empty()&&protector.unprotect(existing,context_+"/Tuning/v1")==w.data)return;
        auto sealed=protector.protect(w.data,context_+"/Tuning/v1");auto temp=root_/(L"tuning-"+std::to_wstring(++serial_)+L".next");
        HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);require(file!=INVALID_HANDLE_VALUE);DWORD wrote=0;
        bool ok=WriteFile(file,sealed.data(),static_cast<DWORD>(sealed.size()),&wrote,nullptr)&&wrote==sealed.size()&&FlushFileBuffers(file);CloseHandle(file);
        if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
        if(!ok){DeleteFileW(temp.c_str());throw std::runtime_error("Cannot commit protected transfer settings");}
    }
    std::vector<DurableState> load()override{
        auto encrypted=readFile(path_);if(encrypted.empty())return {};
        platform::WindowsRecordProtector protector;auto plain=protector.unprotect(encrypted,context_);protocol::Reader r(plain);
        require(r.type()==protocol::FileTransfer&&r.number()==1&&r.number()==ChunkBytes);auto count=r.number();require(count<=MaxRecords);
        std::vector<DurableState> result;
        for(std::uint64_t n=0;n<count;++n){
            DurableState d;d.metadata.id=r.string();d.peer=r.string();d.metadata.name=r.string();d.metadata.size=r.number();
            auto hash=r.bytes();require(hash.size()==32);std::copy(hash.begin(),hash.end(),d.metadata.digest.begin());
            auto state=r.number(),sending=r.number(),ready=r.number();require(state<=static_cast<unsigned>(State::Suspended)&&sending<=1&&ready<=1);
            d.state=static_cast<State>(state);d.sending=sending!=0;d.digestReady=ready!=0;
            d.done=r.number();d.highWaterSent=r.number();d.created=r.number();d.updated=r.number();d.expires=r.number();d.location=r.bytes();d.error=r.string();
            require(d.location.size()<=32768&&d.error.size()<=256);result.push_back(std::move(d));
        }
        r.end();previous_=plain;
        try{auto committed=result;committedRecords_.swap(committed);}catch(...){committedRecords_.clear();}
        return result;
    }
    void save(const std::vector<DurableState>& records)override{
        // A sender ACK confirms bytes already covered by its DURABLE high-water
        // bound. The next chunk persists that ACK together with the next bound
        // BEFORE transmission. On crash, the authenticated receiver's durable
        // Resume supplies the authoritative offset after full source revalidation.
        // Never defer receiver prefixes, rewinds below the durable cursor, state/identity/token changes,
        // cancellation, completion or a new high-water authorization.
#ifndef PEERCHAT_TEST_BASELINE_JOURNAL
        if(redundantSenderAcknowledgement(records)){++deferredAcknowledgements_;return;}
#endif
        const auto began=clockUs();const std::string* changed=nullptr;size_t changes=0;
        for(const auto& row:records){auto old=std::find_if(committedRecords_.begin(),committedRecords_.end(),[&](const auto& d){return d.metadata.id==row.metadata.id;});if(old==committedRecords_.end()||!sameStoredRecord(*old,row)){changed=&row.metadata.id;++changes;}}
        for(const auto& row:committedRecords_)if(std::none_of(records.begin(),records.end(),[&](const auto& d){return d.metadata.id==row.metadata.id;}))++changes;
        require(records.size()<=MaxRecords);protocol::Writer w(protocol::FileTransfer);w.number(1);w.number(ChunkBytes);w.number(records.size());
        for(auto& d:records){
            w.string(d.metadata.id);w.string(d.peer);w.string(d.metadata.name);w.number(d.metadata.size);w.bytes(Blob(d.metadata.digest.begin(),d.metadata.digest.end()));
            w.number(static_cast<unsigned>(d.state));w.number(d.sending);w.number(d.digestReady);w.number(d.done);w.number(d.highWaterSent);
            w.number(d.created);w.number(d.updated);w.number(d.expires);w.bytes(d.location);w.string(d.error);
        }
        if(w.data==previous_)return;
        require(w.data.size()<1024*1024);platform::WindowsRecordProtector protector;const auto protectBegan=clockUs();auto bytes=protector.protect(w.data,context_);const auto protectEnded=clockUs();
        auto temp=root_/(L"state-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++serial_)+L".next");
        HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        require(file!=INVALID_HANDLE_VALUE);DWORD wrote=0;bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&wrote,nullptr)&&wrote==bytes.size()&&FlushFileBuffers(file);CloseHandle(file);
        if(ok)ok=MoveFileExW(temp.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
        if(!ok){auto error=GetLastError();lastCommitError_=error;DeleteFileW(temp.c_str());throw std::runtime_error("Cannot commit transfer journal (Win32 "+std::to_string(error)+")");}previous_=std::move(w.data);
        const auto ended=clockUs();
        // A cache/telemetry allocation failure must not turn an already durable
        // commit into a transfer failure. Missing cache disables coalescing.
        try{auto committed=records;committedRecords_.swap(committed);}catch(...){committedRecords_.clear();}
        try{if(changes==1&&changed&&(costs_.count(*changed)||costs_.size()<MaxRecords)){auto& cost=costs_[*changed];++cost.commits;cost.microseconds+=ended-began;cost.protectUs+=protectEnded-protectBegan;cost.commitUs+=ended-protectEnded;}}catch(...){}
    }
    std::unique_ptr<Source> source(const DurableState& d)override{return reopenSource(d);}
    std::unique_ptr<Sink> sink(const DurableState& d)override{return FileSink::reopen(d);}
};
}
