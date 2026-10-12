#pragma once
#include "../transfer_manager.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <mbedtls/sha256.h>

namespace peerchat::transfer::posix {
inline void check(bool ok){if(!ok)throw std::runtime_error("Attachment cache I/O failed");}
struct File {
    int fd=-1;explicit File(int value):fd(value){check(fd>=0);}~File(){if(fd>=0)::close(fd);}
    File(const File&)=delete;File& operator=(const File&)=delete;
};
inline struct stat identity(int fd){struct stat s{};check(fstat(fd,&s)==0&&S_ISREG(s.st_mode)&&s.st_size>=0&&static_cast<std::uint64_t>(s.st_size)<=MaxFileBytes);return s;}
inline void identityFields(protocol::Writer& w,int fd,bool source){auto s=identity(fd);w.number(s.st_dev);w.number(s.st_ino);if(source){w.number(s.st_mtim.tv_sec);w.number(s.st_mtim.tv_nsec);}}
inline void verifyIdentity(protocol::Reader& r,int fd,bool source){protocol::Writer w(protocol::FileTransfer);identityFields(w,fd,source);protocol::Reader expected(w.data);for(unsigned n=0;n<(source?4u:2u);++n)check(r.number()==expected.number());}
inline Blob read(int fd,std::uint64_t offset,size_t limit){check(limit<=ChunkBytes&&offset<=MaxFileBytes);Blob bytes(limit);size_t done=0;
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
    void reserve(std::uint64_t size){
        auto directory=fdopendir(dup(fd()));check(directory!=nullptr);std::uint64_t total=0;size_t count=0;bool valid=true;
        rewinddir(directory);
        while(auto e=readdir(directory)){if(e->d_name[0]=='.')continue;struct stat s{};if(fstatat(fd(),e->d_name,&s,AT_SYMLINK_NOFOLLOW)!=0||!S_ISREG(s.st_mode)||s.st_size<0){valid=false;break;}
            std::string name=e->d_name;total+=name.size()==37&&name.substr(32)==".part"?MaxFileBytes:static_cast<std::uint64_t>(s.st_size);
            if(++count>256||total>1024ull*1024*1024){valid=false;break;}}
        closedir(directory);check(valid&&count<256&&size<=1024ull*1024*1024-total);
    }
};
class FileSource final:public Source {
    std::shared_ptr<Cache> cache_;std::unique_ptr<File> file_;std::string name_,path_;Blob token_;std::uint64_t size_=0;
public:
    FileSource(std::shared_ptr<Cache> cache,std::string path,std::string name):cache_(std::move(cache)),name_(std::move(name)),path_(std::move(path)){
        check(path_.size()>33&&path_.size()<=253&&protocol::id(path_.substr(0,32),32)&&path_[32]=='-'&&safeName(path_.substr(33))&&safeName(name_));file_=std::make_unique<File>(openat(cache_->fd(),path_.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW));size_=identity(file_->fd).st_size;
        protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/CacheSource/v1");w.string(path_);w.string(name_);identityFields(w,file_->fd,true);token_=std::move(w.data);
    }
    std::uint64_t size()const override{return size_;}std::string name()const override{return name_;}Blob resumeToken()const override{return token_;}
    const std::string& cacheName()const{return path_;}
    Blob read(std::uint64_t offset,size_t count)override{check(offset<=size_&&count<=size_-offset);return posix::read(file_->fd,offset,count);}
    static std::unique_ptr<FileSource> reopen(std::shared_ptr<Cache> cache,const DurableState& d){protocol::Reader r(d.location);check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/CacheSource/v1");auto path=r.string(),name=r.string();auto result=std::make_unique<FileSource>(std::move(cache),path,name);verifyIdentity(r,result->file_->fd,true);r.end();check(result->size()==d.metadata.size&&result->name()==d.metadata.name);return result;}
};
class FileSink final:public Sink {
    std::shared_ptr<Cache> cache_;std::unique_ptr<File> file_;Metadata metadata_;std::string partial_,final_;Blob token_;std::uint64_t offset_=0;bool committed_=false;
    FileSink(std::shared_ptr<Cache> cache,const Metadata& m,bool restore):cache_(std::move(cache)),metadata_(m){
        check(protocol::id(m.id,32)&&safeName(m.name)&&m.size<=MaxFileBytes);partial_=m.id+".part";final_=m.id+"-"+m.name;
        // Logical names are bounded separately; ID prefix may exceed safeName's
        // UI basename bound but is still within NAME_MAX (32+1+<=220 bytes).
        check(final_.size()<=253);
        int fd=openat(cache_->fd(),partial_.c_str(),O_RDWR|O_CLOEXEC|O_NOFOLLOW|(restore?0:O_CREAT|O_EXCL),0600);
        if(fd<0&&restore&&errno==ENOENT){fd=openat(cache_->fd(),final_.c_str(),O_RDWR|O_CLOEXEC|O_NOFOLLOW);committed_=fd>=0;}
        file_=std::make_unique<File>(fd);identity(file_->fd);
        protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/CacheSink/v1");w.string(m.id);w.string(m.name);identityFields(w,file_->fd,false);token_=std::move(w.data);
    }
public:
    FileSink(std::shared_ptr<Cache> cache,const Metadata& m):FileSink(cache,m,false){check(fsync(cache_->fd())==0);}
    Blob resumeToken()const override{return token_;}
    void append(const Blob& bytes)override{check(!committed_&&bytes.size()<=ChunkBytes&&offset_<=metadata_.size&&bytes.size()<=metadata_.size-offset_);size_t done=0;
        while(done<bytes.size()){auto n=pwrite(file_->fd,bytes.data()+done,bytes.size()-done,static_cast<off_t>(offset_+done));if(n<0&&errno==EINTR)continue;check(n>0);done+=static_cast<size_t>(n);}offset_+=bytes.size();}
    void flush()override{check(fsync(file_->fd)==0);}
    Blob read(std::uint64_t offset,size_t count)override{check(offset<=offset_&&count<=offset_-offset);return posix::read(file_->fd,offset,count);}
    void commit()override{
        check(offset_==metadata_.size);flush();
        if(!committed_){
            if(linkat(cache_->fd(),partial_.c_str(),cache_->fd(),final_.c_str(),0)!=0){struct stat s{};check(errno==EEXIST&&fstatat(cache_->fd(),final_.c_str(),&s,AT_SYMLINK_NOFOLLOW)==0&&s.st_dev==identity(file_->fd).st_dev&&s.st_ino==identity(file_->fd).st_ino);}
            check(fsync(cache_->fd())==0);check(unlinkat(cache_->fd(),partial_.c_str(),0)==0);check(fsync(cache_->fd())==0);committed_=true;
        }
    }
    void discard()override{if(committed_)return;struct stat s{};check(fstatat(cache_->fd(),partial_.c_str(),&s,AT_SYMLINK_NOFOLLOW)==0&&s.st_dev==identity(file_->fd).st_dev&&s.st_ino==identity(file_->fd).st_ino);check(unlinkat(cache_->fd(),partial_.c_str(),0)==0&&fsync(cache_->fd())==0);}
    Blob sourceToken()const{check(committed_);FileSource source(cache_,final_,metadata_.name);return source.resumeToken();}
    static std::unique_ptr<FileSink> reopen(std::shared_ptr<Cache> cache,const DurableState& d){
        protocol::Reader r(d.location);check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/CacheSink/v1"&&r.string()==d.metadata.id&&r.string()==d.metadata.name);
        auto result=std::unique_ptr<FileSink>(new FileSink(std::move(cache),d.metadata,true));verifyIdentity(r,result->file_->fd,false);r.end();
        check(static_cast<std::uint64_t>(identity(result->file_->fd).st_size)>=d.done&&d.done<=d.metadata.size);result->offset_=d.done;
        if(!result->committed_)check(ftruncate(result->file_->fd,static_cast<off_t>(d.done))==0&&fsync(result->file_->fd)==0);
        else check(d.done==d.metadata.size&&static_cast<std::uint64_t>(identity(result->file_->fd).st_size)==d.done);
        return result;
    }
};
}
