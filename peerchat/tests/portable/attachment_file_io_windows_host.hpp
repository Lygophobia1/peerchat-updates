#pragma once
// HOST ONLY: real Windows files/CNG/SQLite/DPAPI under production Android JNI.
// These token-shaped adapters do not execute Android POSIX or Keystore.
#include "windows_transfer_io.hpp"
#include <fstream>
namespace peerchat::transfer::posix {
class Cache {
public:
 std::filesystem::path directory;
 explicit Cache(const std::string& path):directory(std::filesystem::absolute(unicode::decode(path))){std::filesystem::create_directories(directory);}
 void reserve(std::uint64_t bytes){protocol::check(bytes<=MaxCacheCipherBytes);}
};
using Sha256=windows::Sha256;
class FileSource final:public Source {
 std::unique_ptr<windows::FileSource> source_;std::string path_,name_;Blob token_;Purpose purpose_;
 void token(){protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/CacheSource/v1");w.string(path_);w.string(name_);w.bytes(source_->resumeToken());w.number(static_cast<unsigned>(purpose_));token_=std::move(w.data);}
 FileSource(std::unique_ptr<windows::FileSource> source,std::string path,std::string name,Purpose purpose):source_(std::move(source)),path_(std::move(path)),name_(std::move(name)),purpose_(purpose){token();}
public:
 FileSource(std::shared_ptr<Cache> cache,std::string path,std::string name,Purpose purpose=Purpose::PeerFile):path_(std::move(path)),name_(std::move(name)),purpose_(purpose){
  protocol::check(path_.size()>33&&path_.size()<=253&&protocol::id(path_.substr(0,32),32)&&path_[32]=='-'&&safeName(path_.substr(33))&&safeName(name_));source_=std::make_unique<windows::FileSource>((cache->directory/unicode::decode(path_)).wstring(),name_,purpose_);token();
 }
 std::uint64_t size()const override{return source_->size();}std::string name()const override{return name_;}
 Blob read(std::uint64_t offset,size_t count)override{return source_->read(offset,count);}Blob resumeToken()const override{return token_;}const std::string& cacheName()const{return path_;}
 static std::unique_ptr<FileSource> create(std::shared_ptr<Cache> cache,const std::string& path,const std::string& name,const Blob& bytes){auto file=cache->directory/unicode::decode(path);protocol::check(!std::filesystem::exists(file));std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());out.close();protocol::check(bool(out));return std::make_unique<FileSource>(std::move(cache),path,name);}
 static std::unique_ptr<FileSource> reopen(std::shared_ptr<Cache> cache,const DurableState& d){
  protocol::Reader r(d.location);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/CacheSource/v1");auto path=r.string(),name=r.string();auto inner=r.bytes();auto purpose=r.number();r.end();protocol::check(purpose==static_cast<unsigned>(d.purpose)&&name==d.metadata.name);
  protocol::Reader raw(inner);protocol::check(raw.type()==protocol::FileTransfer&&std::filesystem::path(raw.text())==cache->directory/unicode::decode(path));auto state=d;state.location=inner;auto source=windows::FileSource::reopen(state);protocol::check(source->size()==d.metadata.size);return std::unique_ptr<FileSource>(new FileSource(std::move(source),std::move(path),std::move(name),d.purpose));
 }
};
class FileSink final:public Sink {
 mutable std::unique_ptr<windows::FileSink> sink_;std::shared_ptr<Cache> cache_;Metadata metadata_;Purpose purpose_;Blob token_;
 void token(){protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/CacheSink/v1");w.string(metadata_.id);w.string(metadata_.name);w.bytes(sink_->resumeToken());w.number(static_cast<unsigned>(purpose_));token_=std::move(w.data);}
 FileSink(std::shared_ptr<Cache> cache,const DurableState& d,bool immutable):cache_(std::move(cache)),metadata_(d.metadata),purpose_(d.purpose){
  protocol::Reader r(d.location);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/CacheSink/v1"&&r.string()==metadata_.id&&r.string()==metadata_.name);auto inner=r.bytes();protocol::check(r.number()==static_cast<unsigned>(purpose_));r.end();protocol::Reader raw(inner);protocol::check(raw.type()==protocol::FileTransfer);auto partial=std::filesystem::path(raw.text()),final=std::filesystem::path(raw.text());protocol::check(partial.parent_path()==cache_->directory&&final.parent_path()==cache_->directory);auto state=d;state.location=inner;sink_=windows::FileSink::reopen(state,immutable);token();
 }
public:
 FileSink(std::shared_ptr<Cache> cache,const Metadata& m,Purpose purpose=Purpose::PeerFile):cache_(std::move(cache)),metadata_(m),purpose_(purpose){sink_=std::make_unique<windows::FileSink>(cache_->directory.wstring(),m,nullptr,true);token();}
 void append(const Blob& bytes)override{sink_->append(bytes);}void flush()override{sink_->flush();}Blob read(std::uint64_t offset,size_t count)override{return sink_->read(offset,count);}void commit()override{sink_->commit();}void discard()override{sink_->discard();}Blob resumeToken()const override{return token_;}
 bool finalized()const{return std::filesystem::exists(cache_->directory/unicode::decode(metadata_.id+"-"+metadata_.name));}
 Blob sourceToken()const{auto final=sink_->path();sink_.reset();FileSource source(cache_,unicode::encode(std::filesystem::path(final).filename().wstring()),metadata_.name,purpose_);return source.resumeToken();}
 static std::unique_ptr<FileSink> reopen(std::shared_ptr<Cache> cache,const DurableState& d,bool immutable=false){return std::unique_ptr<FileSink>(new FileSink(std::move(cache),d,immutable));}
};
class ReplaySink final:public Sink {
 std::unique_ptr<FileSink> sink_;std::uint64_t size_,offset_=0;
public:
 ReplaySink(std::unique_ptr<FileSink> sink,std::uint64_t size):sink_(std::move(sink)),size_(size){}
 void append(const Blob& bytes)override{protocol::check(offset_<=size_&&bytes.size()<=size_-offset_&&sink_->read(offset_,bytes.size())==bytes);offset_+=bytes.size();}
 void flush()override{sink_->flush();}void commit()override{protocol::check(offset_==size_);sink_->commit();}void discard()override{}Blob resumeToken()const override{return sink_->resumeToken();}
};
inline std::unique_ptr<Sink> replaySink(std::shared_ptr<Cache> cache,const DurableState& d){
 protocol::Reader r(d.location);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/CacheSink/v1"&&r.string()==d.metadata.id&&r.string()==d.metadata.name);auto inner=r.bytes();protocol::check(r.number()==static_cast<unsigned>(d.purpose));r.end();
 protocol::Reader raw(inner);protocol::check(raw.type()==protocol::FileTransfer);auto partial=std::filesystem::path(raw.text()),final=std::filesystem::path(raw.text());protocol::check(partial==cache->directory/unicode::decode(d.metadata.id+".peerchat-part")&&final==cache->directory/unicode::decode(d.metadata.id+"-"+d.metadata.name));
 if(!std::filesystem::exists(partial)&&!std::filesystem::exists(final))return std::make_unique<FileSink>(cache,d.metadata,d.purpose);
 auto state=d;state.done=0;try{auto sink=FileSink::reopen(cache,state);if(!sink->finalized())return sink;}catch(...){}state.done=state.metadata.size;return std::make_unique<ReplaySink>(FileSink::reopen(cache,state,true),state.metadata.size);
}
inline std::unique_ptr<Sink> resumeStage(std::shared_ptr<Cache> cache,const Metadata& m,const Blob& token){DurableState d;d.metadata=m;d.location=token;d.purpose=Purpose::DirectCacheCiphertext;try{return FileSink::reopen(cache,d);}catch(...){protocol::Reader r(token);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/CacheSink/v1"&&r.string()==m.id&&r.string()==m.name);r.bytes();protocol::check(r.number()==static_cast<unsigned>(d.purpose));r.end();protocol::check(!std::filesystem::exists(cache->directory/unicode::decode(m.id+".peerchat-part"))&&!std::filesystem::exists(cache->directory/unicode::decode(m.id+"-"+m.name)));return std::make_unique<FileSink>(cache,m,d.purpose);}}
}
