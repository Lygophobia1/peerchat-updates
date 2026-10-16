#pragma once
// HOST TEST ONLY. Executes the production Android JNI/transfer/journal owners
// with Windows real file/CNG primitives. Does not claim Android POSIX or Keystore.
#include "windows_transfer_io.hpp"
#include <fstream>
namespace peerchat::transfer::posix {
class Cache {
public:
    std::filesystem::path directory;
    explicit Cache(const std::string& path):directory(unicode::decode(path)){std::filesystem::create_directories(directory);}
    void reserve(std::uint64_t bytes){protocol::check(bytes<=MaxFileBytes);}
};
using Sha256=windows::Sha256;
class FileSource final:public Source {
    std::unique_ptr<windows::FileSource> source_;std::string path_;
    FileSource(std::unique_ptr<windows::FileSource> source,std::string path):source_(std::move(source)),path_(std::move(path)){}
public:
    FileSource(std::shared_ptr<Cache> cache,std::string path,std::string name):path_(std::move(path)){
        protocol::check(path_.size()>33&&path_.size()<=253&&protocol::id(path_.substr(0,32),32)&&path_[32]=='-'&&safeName(path_.substr(33))&&safeName(name));
        source_=std::make_unique<windows::FileSource>((cache->directory/unicode::decode(path_)).wstring(),name);
    }
    std::uint64_t size()const override{return source_->size();}
    std::string name()const override{return source_->name();}
    Blob read(std::uint64_t offset,size_t count)override{return source_->read(offset,count);}
    Blob resumeToken()const override{return source_->resumeToken();}
    const std::string& cacheName()const{return path_;}
    static std::unique_ptr<FileSource> create(std::shared_ptr<Cache> cache,const std::string& path,const std::string& name,const Blob& bytes){
        auto file=cache->directory/unicode::decode(path);protocol::check(!std::filesystem::exists(file));std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());out.close();protocol::check(bool(out));return std::make_unique<FileSource>(std::move(cache),path,name);
    }
    static std::unique_ptr<FileSource> reopen(std::shared_ptr<Cache>,const DurableState& d){
        auto source=windows::FileSource::reopen(d);protocol::Reader r(d.location);auto path=unicode::encode(std::filesystem::path(r.text()).filename().wstring());
        return std::unique_ptr<FileSource>(new FileSource(std::move(source),std::move(path)));
    }
};
class FileSink final:public Sink {
    mutable std::unique_ptr<windows::FileSink> sink_;std::string name_;
    explicit FileSink(std::unique_ptr<windows::FileSink> sink,std::string name):sink_(std::move(sink)),name_(std::move(name)){}
public:
    FileSink(std::shared_ptr<Cache> cache,const Metadata& m):sink_(std::make_unique<windows::FileSink>(cache->directory.wstring(),m,nullptr,true)),name_(m.name){}
    void append(const Blob& bytes)override{sink_->append(bytes);}
    void flush()override{sink_->flush();}
    Blob read(std::uint64_t offset,size_t count)override{return sink_->read(offset,count);}
    void commit()override{sink_->commit();}
    void discard()override{sink_->discard();}
    Blob resumeToken()const override{return sink_->resumeToken();}
    Blob sourceToken()const{auto final=sink_->path();sink_.reset();windows::FileSource source(final,name_);return source.resumeToken();}
    static std::unique_ptr<FileSink> reopen(std::shared_ptr<Cache>,const DurableState& d){return std::unique_ptr<FileSink>(new FileSink(windows::FileSink::reopen(d),d.metadata.name));}
};
}
