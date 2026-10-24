#pragma once
#include "../transfer_manager.hpp"
#include "attachment_file_scope.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <sys/syscall.h>
#include <cerrno>
#include <mbedtls/sha256.h>

namespace peerchat::transfer::posix {
inline void check(bool ok){if(!ok)throw std::runtime_error("Attachment cache I/O failed");}
struct File {
    int fd=-1;explicit File(int value):fd(value){check(fd>=0);}~File(){if(fd>=0)::close(fd);}
    File(const File&)=delete;File& operator=(const File&)=delete;
};
inline struct stat identity(int fd,Purpose purpose=Purpose::PeerFile){struct stat s{};check(fstat(fd,&s)==0&&S_ISREG(s.st_mode)&&s.st_size>=0&&static_cast<std::uint64_t>(s.st_size)<=maximum(purpose));return s;}
inline void identityFields(protocol::Writer& w,int fd,bool source,Purpose purpose=Purpose::PeerFile){auto s=identity(fd,purpose);w.number(s.st_dev);w.number(s.st_ino);if(source){w.number(s.st_mtim.tv_sec);w.number(s.st_mtim.tv_nsec);}}
inline void verifyIdentity(protocol::Reader& r,int fd,bool source,Purpose purpose=Purpose::PeerFile){protocol::Writer w(protocol::FileTransfer);identityFields(w,fd,source,purpose);protocol::Reader expected(w.data);for(unsigned n=0;n<(source?4u:2u);++n)check(r.number()==expected.number());}
inline Blob read(int fd,std::uint64_t offset,size_t limit,Purpose purpose=Purpose::PeerFile){check(limit<=ChunkBytes&&offset<=maximum(purpose)&&limit<=maximum(purpose)-offset);Blob bytes(limit);size_t done=0;
    while(done<limit){auto n=pread(fd,bytes.data()+done,limit-done,static_cast<off_t>(offset+done));if(n<0&&errno==EINTR)continue;check(n>0);done+=static_cast<size_t>(n);}return bytes;}
class Sha256 final:public Hash {
    mbedtls_sha256_context value_;bool finished_=false;
public:
    Sha256(){mbedtls_sha256_init(&value_);check(mbedtls_sha256_starts(&value_,0)==0);}
    ~Sha256(){mbedtls_sha256_free(&value_);}
    void update(const unsigned char* bytes,size_t count)override{check(!finished_&&count<=ChunkBytes&&mbedtls_sha256_update(&value_,bytes,count)==0);}
    Digest finish()override{Digest result{};check(!finished_&&mbedtls_sha256_finish(&value_,result.data())==0);finished_=true;return result;}
};
// App-private no-backup directory, no automatic eviction. Quota refuses new
// copies instead of deleting any potentially sole recoverable attachment.
class Cache {
    std::shared_ptr<File> directory_;
public:
    explicit Cache(const std::string& path){check(!path.empty()&&path.front()=='/'&&path.find('\0')==std::string::npos);directory_=std::make_shared<File>(open(path.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));}
    int fd()const{return directory_->fd;}
    // OFD locks conflict with Java FileChannel's process locks, including in
    // this same process. Hold admission through creation of a sized partial.
    class Admission {
        File lock_;
    public:
        explicit Admission(int directory):lock_(openat(directory,".capacity.lock",O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600)){
            struct flock request{};request.l_type=F_WRLCK;request.l_whence=SEEK_SET;
            int result;do{result=fcntl(lock_.fd,F_OFD_SETLKW,&request);}while(result<0&&errno==EINTR);check(result==0);
        }
    };
    void reserve(std::uint64_t size){
        auto directory=fdopendir(dup(fd()));check(directory!=nullptr);std::uint64_t total=0,allocated=0;size_t count=0;bool valid=true;
        rewinddir(directory);
        while(auto e=readdir(directory)){if(e->d_name[0]=='.')continue;struct stat s{};if(fstatat(fd(),e->d_name,&s,AT_SYMLINK_NOFOLLOW)!=0||!S_ISREG(s.st_mode)||s.st_size<0){valid=false;break;}
            allocated+=std::min(static_cast<std::uint64_t>(s.st_size),static_cast<std::uint64_t>(s.st_blocks)*512);total+=static_cast<std::uint64_t>(s.st_size);
            if(++count>256||total>10ull*1024*1024*1024){valid=false;break;}}
        closedir(directory);check(valid&&count<256&&size<=10ull*1024*1024*1024-total);
        struct statvfs space{};check(fstatvfs(fd(),&space)==0);const auto available=static_cast<std::uint64_t>(space.f_bavail)*space.f_frsize;check(available>=total-allocated+size+64ull*1024*1024);
    }
};
inline void discardCiphertext(std::shared_ptr<Cache> cache,const Blob& token,const Metadata& metadata,Purpose purpose){
    check((purpose==Purpose::GroupCacheCiphertext||purpose==Purpose::DirectCacheCiphertext)&&protocol::id(metadata.id,32)&&metadata.name=="ciphertext.peerchat"&&metadata.size<=MaxCacheCipherBytes);
    protocol::Reader r(token);check(r.type()==protocol::FileTransfer);auto domain=r.string();std::vector<std::string> paths;bool source=false;
    if(domain=="PeerChat/CacheSink/v1"){check(r.string()==metadata.id&&r.string()==metadata.name);paths={metadata.id+".part",metadata.id+"-"+metadata.name};}
    else{check(domain=="PeerChat/CacheSource/v1");source=true;auto path=r.string();check(path==metadata.id+"-"+metadata.name&&r.string()==metadata.name);paths={path};}
    auto identityAt=r.cursor;for(unsigned i=0;i<(source?4u:2u);++i)r.number();tokenPurpose(r,purpose);
    Cache::Admission admission(cache->fd());
    for(const auto& path:paths){int opened=openat(cache->fd(),path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
        if(opened<0){check(errno==ENOENT);continue;}File file(opened);protocol::Reader expected(token);expected.cursor=identityAt;verifyIdentity(expected,file.fd,source,purpose);
        auto owned=identity(file.fd,purpose);struct stat actual{};check(owned.st_nlink==1&&fstatat(cache->fd(),path.c_str(),&actual,AT_SYMLINK_NOFOLLOW)==0&&actual.st_dev==owned.st_dev&&actual.st_ino==owned.st_ino);
        check(unlinkat(cache->fd(),path.c_str(),0)==0);
    }check(fsync(cache->fd())==0);
}
class FileSource final:public Source {
    std::shared_ptr<Cache> cache_;std::unique_ptr<File> file_;std::string name_,path_;Blob token_;std::uint64_t size_=0;Purpose purpose_=Purpose::PeerFile;
public:
    FileSource(std::shared_ptr<Cache> cache,std::string path,std::string name,Purpose purpose=Purpose::PeerFile):cache_(std::move(cache)),name_(std::move(name)),path_(std::move(path)),purpose_(purpose){
        check(path_.size()>33&&path_.size()<=253&&protocol::id(path_.substr(0,32),32)&&path_[32]=='-'&&safeName(path_.substr(33))&&safeName(name_));file_=std::make_unique<File>(openat(cache_->fd(),path_.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW));size_=identity(file_->fd,purpose_).st_size;
        protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/CacheSource/v1");w.string(path_);w.string(name_);identityFields(w,file_->fd,true,purpose_);tokenPurpose(w,purpose_);token_=std::move(w.data);
    }
    std::uint64_t size()const override{return size_;}std::string name()const override{return name_;}Blob resumeToken()const override{return token_;}
    const std::string& cacheName()const{return path_;}
    static std::unique_ptr<FileSource> create(std::shared_ptr<Cache> cache,const std::string& path,const std::string& name,const Blob& bytes){
        check(bytes.size()<=1024*1024&&path.size()>33&&path.size()<=253&&protocol::id(path.substr(0,32),32)&&path[32]=='-'&&safeName(path.substr(33)));Cache::Admission admission(cache->fd());cache->reserve(bytes.size());
        File file(openat(cache->fd(),path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600));size_t done=0;
        while(done<bytes.size()){auto n=write(file.fd,bytes.data()+done,std::min<size_t>(ChunkBytes,bytes.size()-done));if(n<0&&errno==EINTR)continue;check(n>0);done+=n;}
        check(fsync(file.fd)==0&&fsync(cache->fd())==0);return std::make_unique<FileSource>(std::move(cache),path,name);
    }
    Blob read(std::uint64_t offset,size_t count)override{check(offset<=size_&&count<=size_-offset);return posix::read(file_->fd,offset,count,purpose_);}
    static std::unique_ptr<FileSource> reopen(std::shared_ptr<Cache> cache,const DurableState& d){protocol::Reader r(d.location);check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/CacheSource/v1");auto path=r.string(),name=r.string();auto result=std::make_unique<FileSource>(std::move(cache),path,name,d.purpose);verifyIdentity(r,result->file_->fd,true,d.purpose);tokenPurpose(r,d.purpose);check(result->size()==d.metadata.size&&result->name()==d.metadata.name);return result;}
};
class FileSink final:public Sink {
    std::shared_ptr<Cache> cache_;std::unique_ptr<File> file_;Metadata metadata_;std::string partial_,final_;Blob token_;std::uint64_t offset_=0;bool committed_=false;Purpose purpose_=Purpose::PeerFile;
    FileSink(std::shared_ptr<Cache> cache,const Metadata& m,bool restore,Purpose purpose):cache_(std::move(cache)),metadata_(m),purpose_(purpose){
        check(protocol::id(m.id,32)&&safeName(m.name)&&m.size<=maximum(purpose_));partial_=m.id+".part";final_=m.id+"-"+m.name;
        // Logical names are bounded separately; ID prefix may exceed safeName's
        // UI basename bound but is still within NAME_MAX (32+1+<=220 bytes).
        check(final_.size()<=253);
        Cache::Admission admission(cache_->fd());
        if(!restore)cache_->reserve(m.size);
        int fd=openat(cache_->fd(),partial_.c_str(),O_RDWR|O_CLOEXEC|O_NOFOLLOW|(restore?0:O_CREAT|O_EXCL),0600);
        if(fd<0&&restore&&errno==ENOENT){fd=openat(cache_->fd(),final_.c_str(),O_RDWR|O_CLOEXEC|O_NOFOLLOW);committed_=fd>=0;}
        file_=std::make_unique<File>(fd);auto current=identity(file_->fd,purpose_);
        try{
        // Logical length reserves future blocks across crashes; offset_ remains
        // the only durable progress authority. No unwritten suffix is exposed.
        if(!committed_&&static_cast<std::uint64_t>(current.st_size)<m.size){
            if(restore)cache_->reserve(m.size-static_cast<std::uint64_t>(current.st_size));
            check(ftruncate(fd,static_cast<off_t>(m.size))==0&&fsync(fd)==0);
        }
        protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/CacheSink/v1");w.string(m.id);w.string(m.name);identityFields(w,file_->fd,false,purpose_);tokenPurpose(w,purpose_);token_=std::move(w.data);
        }catch(...){if(!restore){struct stat owned{};if(fstatat(cache_->fd(),partial_.c_str(),&owned,AT_SYMLINK_NOFOLLOW)==0&&owned.st_dev==current.st_dev&&owned.st_ino==current.st_ino){unlinkat(cache_->fd(),partial_.c_str(),0);fsync(cache_->fd());}}throw;}
    }
public:
    FileSink(std::shared_ptr<Cache> cache,const Metadata& m,Purpose purpose=Purpose::PeerFile):FileSink(cache,m,false,purpose){check(fsync(cache_->fd())==0);}
    Blob resumeToken()const override{return token_;}
    void append(const Blob& bytes)override{check(!committed_&&bytes.size()<=ChunkBytes&&offset_<=metadata_.size&&bytes.size()<=metadata_.size-offset_);size_t done=0;
        while(done<bytes.size()){auto n=pwrite(file_->fd,bytes.data()+done,bytes.size()-done,static_cast<off_t>(offset_+done));if(n<0&&errno==EINTR)continue;check(n>0);done+=static_cast<size_t>(n);}offset_+=bytes.size();}
    void flush()override{check(fsync(file_->fd)==0);}
    Blob read(std::uint64_t offset,size_t count)override{check(offset<=offset_&&count<=offset_-offset);return posix::read(file_->fd,offset,count,purpose_);}
    void commit()override{
        check(offset_==metadata_.size);
        if(fsync(file_->fd)!=0)throw CompletionError(CompletionError::Stage::FileSync,errno);
        if(!committed_){
            // Android SELinux forbids app hard links. renameat2 NOREPLACE is a
            // single atomic same-directory publication with no overwrite. No
            // unsafe existence-check + ordinary rename fallback is permitted.
            if(syscall(__NR_renameat2,cache_->fd(),partial_.c_str(),cache_->fd(),final_.c_str(),1 /* RENAME_NOREPLACE */)!=0){
                const int error=errno;struct stat s{};
                if(error!=EEXIST)throw CompletionError(CompletionError::Stage::Rename,error);
                const auto owned=identity(file_->fd,purpose_);
                if(fstatat(cache_->fd(),final_.c_str(),&s,AT_SYMLINK_NOFOLLOW)!=0||!S_ISREG(s.st_mode)||s.st_dev!=owned.st_dev||s.st_ino!=owned.st_ino)
                    throw CompletionError(CompletionError::Stage::DestinationConflict,error);
                // Recover a pre-fix interrupted hard-link publication only when
                // both names refer to this exact recorded owned inode.
                if(unlinkat(cache_->fd(),partial_.c_str(),0)!=0)throw CompletionError(CompletionError::Stage::PartialCleanup,errno);
            }
            committed_=true;
        }
        // Repeat after reopen too: a crash/failure after rename must not skip
        // durable directory publication. Complete/ACK follows this success.
        if(fsync(cache_->fd())!=0)throw CompletionError(CompletionError::Stage::DirectorySync,errno);
    }
    void discard()override{if(committed_)return;struct stat s{};check(fstatat(cache_->fd(),partial_.c_str(),&s,AT_SYMLINK_NOFOLLOW)==0&&s.st_dev==identity(file_->fd,purpose_).st_dev&&s.st_ino==identity(file_->fd,purpose_).st_ino);check(unlinkat(cache_->fd(),partial_.c_str(),0)==0&&fsync(cache_->fd())==0);}
    Blob sourceToken()const{check(committed_);FileSource source(cache_,final_,metadata_.name,purpose_);return source.resumeToken();}
    bool committed()const{return committed_;}
    // Decryption restarts its hash from byte zero after a crash. Truncate only
    // the exact recorded owned partial, never a finalized internal copy.
    static std::unique_ptr<FileSink> restart(std::shared_ptr<Cache> cache,const DurableState& d){
        protocol::Reader r(d.location);check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/CacheSink/v1"&&r.string()==d.metadata.id&&r.string()==d.metadata.name);
        auto result=std::unique_ptr<FileSink>(new FileSink(std::move(cache),d.metadata,true,d.purpose));verifyIdentity(r,result->file_->fd,false,d.purpose);tokenPurpose(r,d.purpose);
        Cache::Admission admission(result->cache_->fd());
        if(result->committed_){check(static_cast<std::uint64_t>(identity(result->file_->fd,d.purpose).st_size)==d.metadata.size);result->offset_=d.metadata.size;}
        else check(ftruncate(result->file_->fd,0)==0&&ftruncate(result->file_->fd,static_cast<off_t>(d.metadata.size))==0&&fsync(result->file_->fd)==0);
        return result;
    }
    static std::unique_ptr<FileSink> reopen(std::shared_ptr<Cache> cache,const DurableState& d){
        protocol::Reader r(d.location);check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/CacheSink/v1"&&r.string()==d.metadata.id&&r.string()==d.metadata.name);
        auto result=std::unique_ptr<FileSink>(new FileSink(std::move(cache),d.metadata,true,d.purpose));verifyIdentity(r,result->file_->fd,false,d.purpose);tokenPurpose(r,d.purpose);
        Cache::Admission admission(result->cache_->fd());
        check(static_cast<std::uint64_t>(identity(result->file_->fd,d.purpose).st_size)>=d.done&&d.done<=d.metadata.size);result->offset_=d.done;
        if(!result->committed_)check(ftruncate(result->file_->fd,static_cast<off_t>(d.done))==0&&ftruncate(result->file_->fd,static_cast<off_t>(d.metadata.size))==0&&fsync(result->file_->fd)==0);
        else check(d.done==d.metadata.size&&static_cast<std::uint64_t>(identity(result->file_->fd,d.purpose).st_size)==d.done);
        return result;
    }
};
// A crash after plaintext rename but before protected job commit must reverify
// the completed internal copy without overwriting or discarding it.
class ReplaySink final:public Sink {
    std::unique_ptr<FileSink> sink_;std::unique_ptr<FileSource> source_;std::uint64_t offset_=0;
public:
    ReplaySink(std::unique_ptr<FileSink> sink,std::unique_ptr<FileSource> source):sink_(std::move(sink)),source_(std::move(source)){}
    void append(const Blob& bytes)override{check(offset_<=source_->size()&&bytes.size()<=source_->size()-offset_&&source_->read(offset_,bytes.size())==bytes);offset_+=bytes.size();}
    void flush()override{sink_->flush();}
    void commit()override{check(offset_==source_->size());sink_->commit();}
    void discard()override{} // The finalized internal original is retained.
    Blob resumeToken()const override{return sink_->resumeToken();}
};
inline std::unique_ptr<FileSink> restartOwnedSink(std::shared_ptr<Cache> cache,const DurableState& d){
    check(protocol::id(d.metadata.id,32)&&safeName(d.metadata.name)&&d.metadata.size<=maximum(d.purpose)&&d.metadata.id.size()+1+d.metadata.name.size()<=253);
    protocol::Reader r(d.location);check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/CacheSink/v1"&&r.string()==d.metadata.id&&r.string()==d.metadata.name);
    r.number();r.number();tokenPurpose(r,d.purpose);
    try{return FileSink::restart(cache,d);}catch(...){
        struct stat entry{};const auto partial=d.metadata.id+".part",final=d.metadata.id+"-"+d.metadata.name;
        check(fstatat(cache->fd(),partial.c_str(),&entry,AT_SYMLINK_NOFOLLOW)!=0&&errno==ENOENT);
        check(fstatat(cache->fd(),final.c_str(),&entry,AT_SYMLINK_NOFOLLOW)!=0&&errno==ENOENT);
        cache->reserve(d.metadata.size);return std::make_unique<FileSink>(cache,d.metadata,d.purpose);
    }
}
inline std::unique_ptr<Sink> replaySink(std::shared_ptr<Cache> cache,const DurableState& d){
    auto sink=restartOwnedSink(cache,d);if(!sink->committed())return sink;
    auto sourceState=d;sourceState.location=sink->sourceToken();auto source=FileSource::reopen(cache,sourceState);
    return std::make_unique<ReplaySink>(std::move(sink),std::move(source));
}
inline std::unique_ptr<Sink> resumeStage(std::shared_ptr<Cache> cache,const Metadata& metadata,const Blob& token){
    // Validate the protected token's complete structure before considering the
    // absent-partial case. An orderly sealer cancellation may have removed its
    // own partial; recovery can create a fresh exclusive file, never overwrite.
    protocol::Reader r(token);check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/CacheSink/v1"&&r.string()==metadata.id&&r.string()==metadata.name);
    r.number();r.number();tokenPurpose(r,Purpose::DirectCacheCiphertext);
    DurableState d;d.metadata=metadata;d.location=token;d.purpose=Purpose::DirectCacheCiphertext;
    auto sink=restartOwnedSink(cache,d);
    check(!sink->committed());return sink;
}
}
