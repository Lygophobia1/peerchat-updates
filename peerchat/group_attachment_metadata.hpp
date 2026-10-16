#pragma once
#include "group_attachment_blob.hpp"

namespace peerchat::group_attachments {
// Sensitive metadata: only protected local storage or an encrypted group body.
// Never use this serializer for an upload descriptor or diagnostic export.
struct SecretRecord {
 Blob bytes;
 explicit SecretRecord(Blob value={}):bytes(std::move(value)){}
 SecretRecord(const SecretRecord&)=delete;SecretRecord& operator=(const SecretRecord&)=delete;
 SecretRecord(SecretRecord&& other)noexcept:bytes(std::move(other.bytes)){}
 ~SecretRecord(){platform::group_crypto::wipe(bytes.data(),bytes.size());}
};
inline void writePrivateManifest(protocol::Writer& w,const Manifest& m){
 m.validate();protocol::check(!m.binding.direct);w.string(m.binding.attachment);w.string(m.binding.group);w.string(m.binding.sender);w.number(m.binding.account);w.number(m.binding.epoch);
 w.string(m.name);w.number(m.size);w.bytes(m.key);w.bytes(m.prefix);w.bytes(Blob(m.plainDigest.begin(),m.plainDigest.end()));w.bytes(Blob(m.cipherDigest.begin(),m.cipherDigest.end()));
}
inline Manifest readPrivateManifest(protocol::Reader& r){
 Manifest m;m.binding.attachment=r.string();m.binding.group=r.string();m.binding.sender=r.string();m.binding.account=r.number();m.binding.epoch=r.number();m.name=r.string();m.size=r.number();m.key=r.bytes();m.prefix=r.bytes();
 auto plain=r.bytes(),cipher=r.bytes();protocol::check(plain.size()==32&&cipher.size()==32);std::copy(plain.begin(),plain.end(),m.plainDigest.begin());std::copy(cipher.begin(),cipher.end(),m.cipherDigest.begin());m.validate();return m;
}
}
