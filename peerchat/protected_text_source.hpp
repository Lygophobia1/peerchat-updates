#pragma once
#include "transfer_manager.hpp"
#include "platform/record_protection.hpp"

namespace peerchat::transfer {
// Portable source adapter only. RecordProtector owns platform encryption;
// TransferManager still hashes, journals, chunks and acknowledges exact bytes.
class ProtectedTextSource final:public Source {
    std::unique_ptr<Source> record_;std::shared_ptr<platform::RecordProtector> protector_;Blob plain_,token_;
public:
    static constexpr size_t MaxBytes=512*1024;
    static constexpr const char* Domain="PeerChat/ProtectedTypedText/v1";
    static constexpr const char* Name="message.peerchat-text";
    static Blob seal(const Blob& plain,const std::string& id,platform::RecordProtector& protector){
        protocol::check(protocol::id(id,32)&&!plain.empty()&&plain.size()<=MaxBytes);
        unicode::decode(std::string(plain.begin(),plain.end()));
        return protector.protect(plain,std::string(Domain)+"/"+id);
    }
    ProtectedTextSource(std::unique_ptr<Source> record,std::string id,std::shared_ptr<platform::RecordProtector> protector)
        :record_(std::move(record)),protector_(std::move(protector)){
        protocol::check(record_&&protector_&&protocol::id(id,32)&&record_->size()<=1024*1024&&!record_->resumeToken().empty());
        Blob sealed;for(std::uint64_t offset=0;offset<record_->size();offset+=ChunkBytes){auto count=std::min<std::uint64_t>(ChunkBytes,record_->size()-offset);auto part=record_->read(offset,count);protocol::check(part.size()==count);sealed.insert(sealed.end(),part.begin(),part.end());}
        plain_=protector_->unprotect(sealed,std::string(Domain)+"/"+id);protocol::check(!plain_.empty()&&plain_.size()<=MaxBytes);unicode::decode(std::string(plain_.begin(),plain_.end()));
        protocol::Writer w(protocol::FileTransfer);w.string(Domain);w.string(id);w.number(record_->size());w.bytes(record_->resumeToken());token_=std::move(w.data);
    }
    ~ProtectedTextSource(){volatile unsigned char* p=plain_.data();for(size_t n=0;n<plain_.size();++n)p[n]=0;}
    std::uint64_t size()const override{return plain_.size();}std::string name()const override{return Name;}
    Blob resumeToken()const override{return token_;}
    Blob read(std::uint64_t offset,size_t count)override{protocol::check(count<=ChunkBytes&&offset<=plain_.size()&&count<=plain_.size()-offset);return {plain_.begin()+offset,plain_.begin()+offset+count};}
    static bool token(const Blob& bytes){try{protocol::Reader r(bytes);return r.type()==protocol::FileTransfer&&r.string()==Domain;}catch(...){return false;}}
    static std::unique_ptr<ProtectedTextSource> reopen(const DurableState& d,std::shared_ptr<platform::RecordProtector> protector,
        const std::function<std::unique_ptr<Source>(const DurableState&)>& open){
        protocol::Reader r(d.location);protocol::check(r.type()==protocol::FileTransfer&&r.string()==Domain);auto id=r.string();protocol::check(id==d.metadata.id);auto count=r.number();auto inner=r.bytes();r.end();
        auto record=d;record.metadata.name=Name;record.metadata.size=count;record.location=std::move(inner);
        auto source=std::make_unique<ProtectedTextSource>(open(record),id,std::move(protector));
        protocol::check(source->size()==d.metadata.size&&source->name()==d.metadata.name&&source->resumeToken()==d.location);return source;
    }
};
}
