#pragma once
#include "transfer_manager.hpp"
#include "platform/group_crypto.hpp"
namespace peerchat::group_attachments {
// Application E2EE object codec only. No socket, grant, retry or transfer engine.
// The manifest is for an authenticated encrypted group body, NEVER a Pi request.
constexpr std::uint64_t HeaderBytes=20,TagBytes=16,BlockBytes=32768;
constexpr std::uint64_t MaxCipherBytes=transfer::MaxFileBytes+((transfer::MaxFileBytes+BlockBytes-1)/BlockBytes)*TagBytes+HeaderBytes;
struct Binding {std::string attachment,group,sender;std::uint64_t account=0,epoch=0;};
struct Manifest {
 Binding binding;std::string name;std::uint64_t size=0;
 Blob key,prefix;transfer::Digest plainDigest{},cipherDigest{};
 Manifest()=default;Manifest(const Manifest&)=delete;Manifest& operator=(const Manifest&)=delete;
 Manifest(Manifest&&)=default;Manifest& operator=(Manifest&&)=delete;
 ~Manifest(){platform::group_crypto::wipe(key.data(),key.size());}
 std::uint64_t blocks()const{return std::max<std::uint64_t>(1,(size+BlockBytes-1)/BlockBytes);}
 std::uint64_t cipherSize()const{return HeaderBytes+size+blocks()*TagBytes;}
 void validate()const{protocol::check(protocol::id(binding.attachment,32)&&protocol::id(binding.group,32)&&protocol::id(binding.sender)&&binding.account&&binding.account<=INT64_MAX&&binding.epoch&&binding.epoch<=INT64_MAX&&transfer::safeName(name)&&size<=transfer::MaxFileBytes&&key.size()==32&&prefix.size()==4);}
};
inline Blob header(const Manifest& m){Blob bytes{'P','C','G','A',1,0,0,0};for(unsigned n=0;n<8;++n)bytes.push_back(static_cast<unsigned char>(m.size>>(n*8)));bytes.insert(bytes.end(),m.prefix.begin(),m.prefix.end());return bytes;}
inline Blob nonce(const Manifest& m,std::uint64_t index){auto n=m.prefix;for(int bit=56;bit>=0;bit-=8)n.push_back(static_cast<unsigned char>(index>>bit));return n;}
inline Blob aad(const Manifest& m,std::uint64_t index,size_t count){protocol::Writer w(protocol::GroupEvent);w.string("PeerChat/GroupAttachmentBlob/v1");w.string(m.binding.attachment);w.string(m.binding.group);w.string(m.binding.sender);w.number(m.binding.account);w.number(m.binding.epoch);w.string(m.name);w.number(m.size);w.number(index);w.number(count);return w.data;}
inline void hash(transfer::Hash& h,const Blob& bytes){for(size_t off=0;off<bytes.size();off+=transfer::ChunkBytes)h.update(bytes.data()+off,std::min(transfer::ChunkBytes,bytes.size()-off));}
inline Blob read(transfer::Source& source,std::uint64_t offset,size_t count){Blob result;while(result.size()<count){auto part=source.read(offset+result.size(),std::min<size_t>(transfer::ChunkBytes,count-result.size()));protocol::check(!part.empty()&&part.size()<=count-result.size());result.insert(result.end(),part.begin(),part.end());}return result;}
inline void append(transfer::Sink& sink,const Blob& bytes){for(size_t off=0;off<bytes.size();off+=transfer::ChunkBytes)sink.append(Blob(bytes.begin()+off,bytes.begin()+std::min(bytes.size(),off+transfer::ChunkBytes)));}
struct PlainBuffer {Blob bytes;~PlainBuffer(){platform::group_crypto::wipe(bytes.data(),bytes.size());}};
inline Manifest seal(transfer::Source& source,transfer::Sink& ciphertext,Binding binding,transfer::Hash& plainHash,transfer::Hash& cipherHash){
 Manifest m;
 try{m.binding=std::move(binding);m.name=source.name();m.size=source.size();m.key=platform::group_crypto::random(32);m.prefix=platform::group_crypto::random(4);m.validate();auto h=header(m);hash(cipherHash,h);append(ciphertext,h);
  for(std::uint64_t index=0;index<m.blocks();++index){auto offset=index*BlockBytes;auto count=static_cast<size_t>(std::min<std::uint64_t>(BlockBytes,m.size-offset));PlainBuffer plain{read(source,offset,count)};hash(plainHash,plain.bytes);Blob encrypted,tag(16);
   bool ok=platform::group_crypto::gcm(true,m.key,aad(m,index,count),nonce(m,index),plain.bytes,encrypted,tag);protocol::check(ok);encrypted.insert(encrypted.end(),tag.begin(),tag.end());hash(cipherHash,encrypted);append(ciphertext,encrypted);
  }m.plainDigest=plainHash.finish();m.cipherDigest=cipherHash.finish();ciphertext.flush();ciphertext.commit();return m;
 }catch(...){ciphertext.discard();throw;}
}
inline void open(transfer::Source& ciphertext,transfer::Sink& plaintext,const Manifest& m,transfer::Hash& plainHash,transfer::Hash& cipherHash){
 try{m.validate();protocol::check(ciphertext.size()==m.cipherSize());auto h=read(ciphertext,0,HeaderBytes);protocol::check(h==header(m));hash(cipherHash,h);std::uint64_t position=HeaderBytes;
  for(std::uint64_t index=0;index<m.blocks();++index){auto count=static_cast<size_t>(std::min<std::uint64_t>(BlockBytes,m.size-index*BlockBytes));auto block=read(ciphertext,position,count+TagBytes);position+=block.size();hash(cipherHash,block);Blob tag(block.end()-TagBytes,block.end());block.resize(count);PlainBuffer plain;
   protocol::check(platform::group_crypto::gcm(false,m.key,aad(m,index,count),nonce(m,index),block,plain.bytes,tag));hash(plainHash,plain.bytes);append(plaintext,plain.bytes);
  }protocol::check(position==ciphertext.size()&&plainHash.finish()==m.plainDigest&&cipherHash.finish()==m.cipherDigest);plaintext.flush();plaintext.commit();
 }catch(...){plaintext.discard();throw;}
}
}
