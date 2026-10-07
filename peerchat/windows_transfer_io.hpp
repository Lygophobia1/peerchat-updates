#pragma once
#include "transfer_manager.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include "platform/windows_record_protection.hpp"

namespace peerchat::transfer::windows {
inline void require(bool ok){if(!ok)throw std::runtime_error("File I/O failed");}
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
    void recover(){
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
public:
    explicit FileSource(const std::wstring& path){
        name_=unicode::encode(std::filesystem::path(path).filename().wstring());require(safeName(name_));
        handle_=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(handle_==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open file");
        BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
        if(!GetFileInformationByHandle(handle_,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||!GetFileSizeEx(handle_,&size)||size.QuadPart<0||static_cast<std::uint64_t>(size.QuadPart)>MaxFileBytes){
            CloseHandle(handle_);handle_=INVALID_HANDLE_VALUE;throw std::runtime_error("Unsupported source file");
        }
        size_=static_cast<std::uint64_t>(size.QuadPart);
    }
    ~FileSource(){if(handle_!=INVALID_HANDLE_VALUE)CloseHandle(handle_);}
    std::uint64_t size()const override{return size_;}
    std::string name()const override{return name_;}
    Blob read(std::uint64_t offset,size_t limit)override{
        require(offset<=size_&&limit<=ChunkBytes&&limit<=size_-offset);LARGE_INTEGER position{};position.QuadPart=offset;
        require(SetFilePointerEx(handle_,position,nullptr,FILE_BEGIN)!=0);Blob result(limit);DWORD got=0;
        require(ReadFile(handle_,result.data(),static_cast<DWORD>(limit),&got,nullptr)!=0);result.resize(got);return result;
    }
};
class FileSink final:public Sink {
    HANDLE handle_=INVALID_HANDLE_VALUE,directory_=INVALID_HANDLE_VALUE;
    std::wstring partial_,final_,journal_;
    bool committed_=false;
public:
    FileSink(const std::wstring& directory,const Metadata& metadata,CleanupJournal* journal=nullptr){
        require(protocol::id(metadata.id,32)&&safeName(metadata.name));
        // Hold the destination directory without delete sharing. Reject junctions
        // and symlinks; never derive a destination directory from remote metadata.
        directory_=CreateFileW(directory.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(directory_==INVALID_HANDLE_VALUE)throw std::runtime_error("Destination unavailable");
        BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(directory_,&info)||!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){CloseHandle(directory_);directory_=INVALID_HANDLE_VALUE;throw std::runtime_error("Unsafe destination directory");}
        // Unique names prevent collisions with existing files and two equal
        // remote basenames. Never use MOVEFILE_REPLACE_EXISTING.
        auto base=std::filesystem::path(directory);
        final_=(base/(unicode::decode(metadata.id)+L"-"+unicode::decode(metadata.name))).wstring();
        partial_=(base/(unicode::decode(metadata.id)+L".peerchat-part")).wstring();
        handle_=CreateFileW(partial_.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(handle_==INVALID_HANDLE_VALUE){if(!journal_.empty())DeleteFileW(journal_.c_str());CloseHandle(directory_);directory_=INVALID_HANDLE_VALUE;throw std::runtime_error("Cannot create partial file");}
        try{if(journal)journal_=journal->record(metadata.id,partial_,handle_);}
        catch(...){CloseHandle(handle_);handle_=INVALID_HANDLE_VALUE;DeleteFileW(partial_.c_str());CloseHandle(directory_);directory_=INVALID_HANDLE_VALUE;throw;}
    }
    ~FileSink(){
        if(handle_!=INVALID_HANDLE_VALUE)CloseHandle(handle_);
        bool removed=committed_||partial_.empty()||DeleteFileW(partial_.c_str())||GetLastError()==ERROR_FILE_NOT_FOUND;
        if(removed&&!journal_.empty())DeleteFileW(journal_.c_str());
        if(directory_!=INVALID_HANDLE_VALUE)CloseHandle(directory_);
    }
    void append(const Blob& bytes)override{DWORD wrote=0;require(handle_!=INVALID_HANDLE_VALUE&&bytes.size()<=ChunkBytes&&WriteFile(handle_,bytes.data(),static_cast<DWORD>(bytes.size()),&wrote,nullptr)&&wrote==bytes.size());}
    void commit()override{
        require(handle_!=INVALID_HANDLE_VALUE&&FlushFileBuffers(handle_));require(CloseHandle(handle_)!=0);handle_=INVALID_HANDLE_VALUE;
        require(MoveFileExW(partial_.c_str(),final_.c_str(),MOVEFILE_WRITE_THROUGH)!=0);committed_=true;
    }
    const std::wstring& path()const{return final_;}
};
}
