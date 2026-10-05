#pragma once
#include "platform/hpke_provider.hpp"
#include <algorithm>
#include <functional>
#include <limits>
#include <string>

namespace peerchat::mailbox {
using Bytes=platform::HpkeProvider::Bytes;
using Verify=std::function<bool(const Bytes&,const Bytes&,const Bytes&)>;
using Fingerprint=std::function<std::string(const Bytes&)>;
constexpr std::size_t MaxBody=65536,MaxWire=70000;
constexpr std::uint64_t MaxTtlMilliseconds=7ULL*24*60*60*1000;
inline void require(bool ok){if(!ok)throw std::runtime_error("Invalid mailbox protocol object");}
inline bool id(const std::string& s,std::size_t count){return s.size()==count&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
inline bool account(std::uint64_t value){return value>0&&value<=std::uint64_t(std::numeric_limits<std::int64_t>::max());}
struct Writer {
    Bytes bytes;
    explicit Writer(const char* domain){while(*domain)bytes.push_back(static_cast<unsigned char>(*domain++));bytes.push_back(0);}
    void number(std::uint64_t n,unsigned width){for(int i=int(width)-1;i>=0;--i)bytes.push_back(static_cast<unsigned char>(n>>(8*i)));}
    void field(const Bytes& value){require(value.size()<=MaxWire);number(value.size(),4);bytes.insert(bytes.end(),value.begin(),value.end());}
    void text(const std::string& value){field(Bytes(value.begin(),value.end()));}
};
struct Reader {
    const Bytes& bytes;std::size_t cursor=0;
    Reader(const Bytes& b,const char* domain):bytes(b){Writer expected(domain);require(b.size()<=MaxWire&&b.size()>=expected.bytes.size()&&std::equal(expected.bytes.begin(),expected.bytes.end(),b.begin()));cursor=expected.bytes.size();}
    std::uint64_t number(unsigned width){require(width<=8&&cursor<=bytes.size()&&width<=bytes.size()-cursor);std::uint64_t n=0;while(width--)n=(n<<8)|bytes[cursor++];return n;}
    Bytes field(std::size_t max){auto size=number(4);require(size<=max&&size<=bytes.size()-cursor);Bytes out(bytes.begin()+cursor,bytes.begin()+cursor+std::size_t(size));cursor+=std::size_t(size);return out;}
    std::string text(std::size_t max){auto data=field(max);return {data.begin(),data.end()};}
    void end(){require(cursor==bytes.size());}
};
struct RecipientBinding {
    std::uint64_t accountId=0,generation=0,createdUtc=0;
    std::string device,keyId,predecessorId;
    Bytes publicKey,signature;
    bool storageAllowed=false;
};
inline void validate(const RecipientBinding& value){
    require(account(value.accountId)&&id(value.device,64)&&id(value.keyId,32)&&account(value.generation)&&account(value.createdUtc)&&
        (value.predecessorId.empty()||id(value.predecessorId,32))&&value.publicKey.size()==65&&value.publicKey[0]==4&&
        (value.signature.empty()||value.signature.size()==64));
}
inline Bytes bindingPayload(const RecipientBinding& value){
    validate(value);Writer w("PeerChat/device-recipient-binding/v1");
    w.number(1,2);w.number(platform::HpkeSuite::kem,2);w.number(platform::HpkeSuite::kdf,2);w.number(platform::HpkeSuite::aead,2);
    w.number(value.accountId,8);w.text(value.device);w.text(value.keyId);w.number(value.generation,8);w.number(value.createdUtc,8);
    w.text(value.predecessorId);w.field(value.publicKey);w.number(value.storageAllowed?1:0,1);return w.bytes;
}
inline bool verifyBinding(const RecipientBinding& value,std::uint64_t authorizedAccount,const std::string& authorizedDevice,const Bytes& authorizedIdentity,const Verify& verify,const Fingerprint& fingerprint){
    try{return value.signature.size()==64&&value.accountId==authorizedAccount&&value.device==authorizedDevice&&
        fingerprint(authorizedIdentity)==authorizedDevice&&verify(bindingPayload(value),value.signature,authorizedIdentity);}catch(...){return false;}
}
inline Bytes serializeBinding(const RecipientBinding& value){
    require(value.signature.size()==64);auto bytes=bindingPayload(value);Writer signature("");signature.bytes.clear();signature.field(value.signature);
    bytes.insert(bytes.end(),signature.bytes.begin(),signature.bytes.end());return bytes;
}
inline RecipientBinding parseBinding(const Bytes& bytes){
    Reader r(bytes,"PeerChat/device-recipient-binding/v1");
    require(r.number(2)==1&&r.number(2)==platform::HpkeSuite::kem&&r.number(2)==platform::HpkeSuite::kdf&&r.number(2)==platform::HpkeSuite::aead);
    RecipientBinding value;value.accountId=r.number(8);value.device=r.text(64);value.keyId=r.text(32);value.generation=r.number(8);value.createdUtc=r.number(8);
    value.predecessorId=r.text(32);value.publicKey=r.field(65);auto consent=r.number(1);require(consent<=1);value.storageAllowed=consent==1;
    value.signature=r.field(64);r.end();require(serializeBinding(value)==bytes);return value;
}
inline Bytes bindingBatch(const std::vector<Bytes>& bindings){
    require(bindings.size()<=32);Writer writer("PeerChat/mailbox-recipient-batch/v1");writer.number(bindings.size(),2);
    for(const auto& bytes:bindings){parseBinding(bytes);writer.field(bytes);}require(writer.bytes.size()<=MaxWire);return writer.bytes;
}
inline std::vector<RecipientBinding> parseBindingBatch(const Bytes& bytes){
    Reader reader(bytes,"PeerChat/mailbox-recipient-batch/v1");auto count=reader.number(2);require(count<=32);std::vector<RecipientBinding> result;
    while(count--)result.push_back(parseBinding(reader.field(4096)));
    reader.end();return result;
}
struct Revocation {std::uint64_t accountId=0,generation=0;std::string device,keyId;Bytes signature;};
inline Bytes revocationPayload(const Revocation& value){
    require(account(value.accountId)&&account(value.generation)&&id(value.device,64)&&id(value.keyId,32));
    Writer w("PeerChat/device-recipient-revoke/v1");w.number(1,2);w.number(value.accountId,8);w.text(value.device);w.text(value.keyId);w.number(value.generation,8);return w.bytes;
}
inline Bytes serializeRevocation(const Revocation& value){
    require(value.signature.size()==64);auto bytes=revocationPayload(value);Writer signature("");signature.bytes.clear();signature.field(value.signature);
    bytes.insert(bytes.end(),signature.bytes.begin(),signature.bytes.end());return bytes;
}
inline Revocation parseRevocation(const Bytes& bytes){
    Reader r(bytes,"PeerChat/device-recipient-revoke/v1");require(r.number(2)==1);Revocation value;
    value.accountId=r.number(8);value.device=r.text(64);value.keyId=r.text(32);value.generation=r.number(8);value.signature=r.field(64);r.end();require(serializeRevocation(value)==bytes);return value;
}
struct Header {
    std::uint64_t senderAccount=0,recipientAccount=0,keyGeneration=0,createdUtc=0,expiresUtc=0,logicalOrder=0;
    std::string senderDevice,recipientDevice,keyId,messageId;
    bool storageAllowed=false;
};
struct Envelope {Header header;platform::HpkeEnvelope encrypted;Bytes signature;};
inline void validate(const Header& h){
    require(account(h.senderAccount)&&account(h.recipientAccount)&&h.senderAccount!=h.recipientAccount&&
        id(h.senderDevice,64)&&id(h.recipientDevice,64)&&h.senderDevice!=h.recipientDevice&&id(h.keyId,32)&&id(h.messageId,32)&&
        account(h.keyGeneration)&&account(h.createdUtc)&&account(h.expiresUtc)&&h.expiresUtc>h.createdUtc&&h.expiresUtc-h.createdUtc<=MaxTtlMilliseconds&&
        h.logicalOrder>0&&h.logicalOrder<=std::uint64_t(std::numeric_limits<std::int64_t>::max())&&h.storageAllowed);
}
inline Bytes headerPayload(const Header& h){
    validate(h);Writer w("PeerChat/mailbox-envelope/v1");
    w.number(1,2);w.number(platform::HpkeSuite::kem,2);w.number(platform::HpkeSuite::kdf,2);w.number(platform::HpkeSuite::aead,2);
    w.number(h.senderAccount,8);w.text(h.senderDevice);w.number(h.recipientAccount,8);w.text(h.recipientDevice);
    w.text(h.keyId);w.number(h.keyGeneration,8);w.text(h.messageId);w.number(h.createdUtc,8);w.number(h.expiresUtc,8);w.number(h.logicalOrder,8);w.number(1,1);
    return w.bytes;
}
inline Bytes info(){Writer w("PeerChat/mailbox-HPKE/Base/v1");return w.bytes;}
inline Bytes textBody(const Bytes& utf8,std::uint64_t messageUtc){
    require(!utf8.empty()&&utf8.size()<=MaxBody-128&&account(messageUtc));Writer w("PeerChat/mailbox-text/v1");w.number(1,2);w.number(messageUtc,8);w.field(utf8);return w.bytes;
}
struct TextBody {Bytes utf8;std::uint64_t messageUtc=0;};
inline TextBody parseTextBody(const Bytes& bytes){
    Reader r(bytes,"PeerChat/mailbox-text/v1");require(r.number(2)==1);TextBody body;body.messageUtc=r.number(8);body.utf8=r.field(MaxBody-128);r.end();
    require(textBody(body.utf8,body.messageUtc)==bytes);return body;
}
inline Bytes signingPayload(const Envelope& value){
    Writer w("PeerChat/mailbox-device-signature/v1");w.field(headerPayload(value.header));
    require(value.encrypted.enc.size()==65&&value.encrypted.enc[0]==4&&value.encrypted.ciphertext.size()>=16&&value.encrypted.ciphertext.size()<=MaxBody+16);
    w.field(value.encrypted.enc);w.field(value.encrypted.ciphertext);return w.bytes;
}
inline Bytes serialize(const Envelope& value){
    require(value.signature.size()==64);auto out=signingPayload(value);Writer tail("");tail.bytes.clear();tail.field(value.signature);
    out.insert(out.end(),tail.bytes.begin(),tail.bytes.end());require(out.size()<=MaxWire);return out;
}
inline Envelope parse(const Bytes& bytes){
    Reader r(bytes,"PeerChat/mailbox-device-signature/v1");auto header=r.field(4096);
    Reader h(header,"PeerChat/mailbox-envelope/v1");
    require(h.number(2)==1&&h.number(2)==platform::HpkeSuite::kem&&h.number(2)==platform::HpkeSuite::kdf&&h.number(2)==platform::HpkeSuite::aead);
    Envelope value;auto& x=value.header;
    x.senderAccount=h.number(8);x.senderDevice=h.text(64);x.recipientAccount=h.number(8);x.recipientDevice=h.text(64);
    x.keyId=h.text(32);x.keyGeneration=h.number(8);x.messageId=h.text(32);x.createdUtc=h.number(8);x.expiresUtc=h.number(8);x.logicalOrder=h.number(8);
    x.storageAllowed=h.number(1)==1;h.end();validate(x);
    value.encrypted.enc=r.field(65);value.encrypted.ciphertext=r.field(MaxBody+16);value.signature=r.field(64);r.end();
    require(serialize(value)==bytes);return value;
}
inline Envelope seal(const Header& header,const Bytes& plaintext,const RecipientBinding& recipient,const Bytes& trustedIdentity,
                     const Verify& verify,const Fingerprint& fingerprint,platform::IdentityProvider& sender,platform::HpkeProvider& hpke){
    require(plaintext.size()>0&&plaintext.size()<=MaxBody&&verifyBinding(recipient,header.recipientAccount,header.recipientDevice,trustedIdentity,verify,fingerprint)&&
        recipient.storageAllowed&&recipient.keyId==header.keyId&&recipient.generation==header.keyGeneration&&fingerprint(sender.publicKey())==header.senderDevice);
    Envelope result;result.header=header;result.encrypted=hpke.seal(recipient.publicKey,info(),headerPayload(header),plaintext);
    result.signature=sender.sign(signingPayload(result));require(result.signature.size()==64);return result;
}
inline Bytes open(const Envelope& envelope,const RecipientBinding& recipient,std::uint64_t authorizedSenderAccount,const Bytes& trustedSenderIdentity,
                  const Verify& verify,const Fingerprint& fingerprint,const std::shared_ptr<platform::RecipientKeyHandle>& handle,
                  platform::HpkeProvider& hpke,std::uint64_t now){
    auto& h=envelope.header;validate(h);
    require(h.senderAccount==authorizedSenderAccount&&h.recipientAccount==recipient.accountId&&h.recipientDevice==recipient.device&&h.keyId==recipient.keyId&&h.keyGeneration==recipient.generation&&
        recipient.storageAllowed&&h.createdUtc<=now&&h.expiresUtc>now&&fingerprint(trustedSenderIdentity)==h.senderDevice&&
        envelope.signature.size()==64&&verify(signingPayload(envelope),envelope.signature,trustedSenderIdentity));
    // Caller holds a current KeyManager operation lease and authoritative friend
    // account/device authorization. This function does not establish trust.
    return hpke.open(handle,recipient.publicKey,info(),headerPayload(h),envelope.encrypted);
}
struct Receipt {std::uint64_t recipientAccount=0;std::string recipientDevice,senderDevice,messageId;Bytes envelopeDigest,signature;};
inline Bytes receiptPayload(const Receipt& receipt){
    require(account(receipt.recipientAccount)&&id(receipt.recipientDevice,64)&&id(receipt.senderDevice,64)&&id(receipt.messageId,32)&&receipt.envelopeDigest.size()==32);
    Writer w("PeerChat/mailbox-recipient-ACK/v1");w.number(receipt.recipientAccount,8);w.text(receipt.recipientDevice);w.text(receipt.senderDevice);w.text(receipt.messageId);w.field(receipt.envelopeDigest);return w.bytes;
}
inline bool verifyReceipt(const Receipt& receipt,std::uint64_t expectedAccount,const std::string& expectedDevice,
                          const std::string& localDevice,const std::string& expectedMessage,const Bytes& expectedDigest,
                          const Bytes& trustedIdentity,const Verify& verify,const Fingerprint& fingerprint){
    try{return receipt.recipientAccount==expectedAccount&&receipt.recipientDevice==expectedDevice&&receipt.senderDevice==localDevice&&
        receipt.messageId==expectedMessage&&receipt.envelopeDigest==expectedDigest&&receipt.signature.size()==64&&
        fingerprint(trustedIdentity)==expectedDevice&&verify(receiptPayload(receipt),receipt.signature,trustedIdentity);}catch(...){return false;}
}
inline Bytes serializeReceipt(const Receipt& receipt){
    require(receipt.signature.size()==64);auto bytes=receiptPayload(receipt);Writer signature("");signature.bytes.clear();signature.field(receipt.signature);
    bytes.insert(bytes.end(),signature.bytes.begin(),signature.bytes.end());return bytes;
}
inline Receipt parseReceipt(const Bytes& bytes){
    Reader r(bytes,"PeerChat/mailbox-recipient-ACK/v1");Receipt receipt;
    receipt.recipientAccount=r.number(8);receipt.recipientDevice=r.text(64);receipt.senderDevice=r.text(64);receipt.messageId=r.text(32);
    receipt.envelopeDigest=r.field(32);receipt.signature=r.field(64);r.end();require(serializeReceipt(receipt)==bytes);return receipt;
}
}
