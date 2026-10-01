#include "group_message_envelope.hpp"
#include <algorithm>
#include <array>
#include <cstring>

namespace peerchat::group_messages {
namespace {
struct Algorithm { BCRYPT_ALG_HANDLE handle{};~Algorithm(){if(handle)BCryptCloseAlgorithmProvider(handle,0);} };
struct Key { BCRYPT_KEY_HANDLE handle{};~Key(){if(handle)BCryptDestroyKey(handle);} };
struct Secret { Blob bytes;~Secret(){SecureZeroMemory(bytes.data(),bytes.size());} };
void check(bool value){if(!value)throw std::runtime_error("Invalid group envelope");}
void nt(NTSTATUS value){if(value<0)throw std::runtime_error("Group envelope crypto operation failed");}
void number(Blob& out,std::uint64_t value,std::size_t bytes){for(std::size_t i=bytes;i>0;--i)out.push_back(static_cast<unsigned char>(value>>(8*(i-1))));}
void fixed(Blob& out,const std::string& value,std::size_t bytes){check(protocol::id(value,bytes));out.insert(out.end(),value.begin(),value.end());}
void append(Blob& out,const Blob& value){out.insert(out.end(),value.begin(),value.end());}
bool valid(const Envelope& e){
    return e.version==EnvelopeVersion&&protocol::id(e.groupId,32)&&protocol::id(e.messageId,32)&&protocol::id(e.senderDevice)&&
        e.rosterGeneration>0&&e.rosterGeneration<=INT64_MAX&&e.keyGeneration>0&&e.keyGeneration<=INT64_MAX&&
        e.senderAccount>0&&e.senderAccount<=INT64_MAX&&e.nonce.size()==12&&e.ciphertext.size()<=MaxPlaintextBytes;
}
struct Reader {
    const Blob& wire;std::size_t offset=0;
    std::uint64_t number(std::size_t size){check(size<=wire.size()-offset);std::uint64_t value=0;while(size--)value=(value<<8)|wire[offset++];return value;}
    Blob bytes(std::size_t size){check(size<=wire.size()-offset);Blob value(wire.begin()+offset,wire.begin()+offset+size);offset+=size;return value;}
    std::string text(std::size_t size){auto value=bytes(size);return {value.begin(),value.end()};}
};
// Windows CNG AES-256-GCM. Caller owns/wipes key and decrypted scratch memory.
bool gcm(bool encrypt,const Blob& keyBytes,const Blob& aad,const Blob& nonce,const Blob& input,Blob& output,Blob& tag){
    check(keyBytes.size()==32&&nonce.size()==12&&tag.size()==16&&input.size()<=MaxPlaintextBytes);
    Algorithm algorithm;Key key;
    nt(BCryptOpenAlgorithmProvider(&algorithm.handle,BCRYPT_AES_ALGORITHM,nullptr,0));
    nt(BCryptSetProperty(algorithm.handle,BCRYPT_CHAINING_MODE,reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),sizeof(BCRYPT_CHAIN_MODE_GCM),0));
    nt(BCryptGenerateSymmetricKey(algorithm.handle,&key.handle,nullptr,0,const_cast<PUCHAR>(keyBytes.data()),32,0));
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce=const_cast<PUCHAR>(nonce.data());info.cbNonce=12;
    info.pbAuthData=const_cast<PUCHAR>(aad.data());info.cbAuthData=static_cast<ULONG>(aad.size());
    info.pbTag=tag.data();info.cbTag=16;
    output.resize(input.size());ULONG written=0;unsigned char empty=0;
    auto operation=encrypt?BCryptEncrypt:BCryptDecrypt;
    NTSTATUS status=operation(key.handle,input.empty()?&empty:const_cast<PUCHAR>(input.data()),static_cast<ULONG>(input.size()),
        &info,nullptr,0,output.empty()?&empty:output.data(),static_cast<ULONG>(output.size()),&written,0);
    if(status<0){SecureZeroMemory(output.data(),output.size());output.clear();return false;}
    check(written==input.size());return true;
}
}

const char* resultName(Result value)noexcept{
    switch(value){
    case Result::Accepted:return "accepted";
    case Result::UnsupportedVersion:return "unsupported_version";
    case Result::MalformedEnvelope:return "malformed_envelope";
    case Result::WrongGroup:return "wrong_group";
    case Result::RosterUnavailable:return "roster_unavailable";
    case Result::StaleRosterGeneration:return "stale_roster_generation";
    case Result::FutureRosterGeneration:return "future_roster_generation";
    case Result::StaleKeyGeneration:return "stale_key_generation";
    case Result::FutureKeyGeneration:return "future_key_generation";
    case Result::MissingGroupKey:return "missing_group_key";
    case Result::LocalNotMember:return "local_not_member";
    case Result::SenderNotMember:return "sender_not_member";
    case Result::UnauthorizedSenderDevice:return "unauthorized_sender_device";
    case Result::SignatureFailed:return "signature_failed";
    case Result::AeadAuthFailed:return "aead_auth_failed";
    case Result::DuplicateMessage:return "duplicate_message";
    case Result::ConflictingMessage:return "conflicting_message";
    case Result::CryptoFailed:return "crypto_failed";
    case Result::StorageFailed:return "storage_failed";
    }
    return "invalid_result";
}
Blob associatedData(const Envelope& e){
    check(valid(e));Blob out{'P','C','G','E'};number(out,e.version,2);
    fixed(out,e.groupId,32);number(out,e.rosterGeneration,8);number(out,e.keyGeneration,8);
    fixed(out,e.messageId,32);number(out,e.senderAccount,8);fixed(out,e.senderDevice,64);
    append(out,e.nonce);number(out,e.ciphertext.size(),4);check(out.size()==HeaderBytes);return out;
}
Blob signingPayload(const Envelope& e){check(e.tag.size()==16);auto out=associatedData(e);append(out,e.ciphertext);append(out,e.tag);return out;}
Blob serialize(const Envelope& e){check(e.signature.size()==64);auto out=signingPayload(e);append(out,e.signature);return out;}
Result parse(const Blob& wire,Envelope& out)noexcept{
    out={};
    try{
        if(wire.size()<6||wire.size()>MaxEnvelopeBytes)return Result::MalformedEnvelope;
        Reader reader{wire};if(reader.text(4)!="PCGE")return Result::MalformedEnvelope;
        auto version=reader.number(2);if(version!=EnvelopeVersion)return Result::UnsupportedVersion;
        if(wire.size()<HeaderBytes+80)return Result::MalformedEnvelope;
        Envelope e;e.version=static_cast<std::uint16_t>(version);e.groupId=reader.text(32);
        e.rosterGeneration=reader.number(8);e.keyGeneration=reader.number(8);e.messageId=reader.text(32);
        e.senderAccount=reader.number(8);e.senderDevice=reader.text(64);e.nonce=reader.bytes(12);
        auto size=reader.number(4);
        if(size>MaxPlaintextBytes||wire.size()!=HeaderBytes+size+80)return Result::MalformedEnvelope;
        e.ciphertext=reader.bytes(static_cast<std::size_t>(size));e.tag=reader.bytes(16);e.signature=reader.bytes(64);
        if(!valid(e))return Result::MalformedEnvelope;
        out=std::move(e);return Result::Accepted;
    }catch(...){return Result::MalformedEnvelope;}
}
Result MessageLayer::context(const std::string& local,const GroupState& ledger,const group_keys::RosterSnapshot*& roster)const{
    if(!protocol::id(local,32)||!ledger.created||ledger.groupId!=local)return Result::WrongGroup;
    roster=keys_.rosterForLegacy(local);
    if(!roster||!group_keys::GroupKeyManager::matchesSignedLedger(*roster,ledger))return Result::RosterUnavailable;
    if(!group_keys::GroupKeyManager::currentMember(*roster,account_,device_.peerId))return Result::LocalNotMember;
    auto member=ledger.members.find(account_);
    if(member==ledger.members.end()||member->second.publicKey!=device_.publicKey||Storage::publicKeyId(device_.publicKey)!=device_.peerId)
        return Result::UnauthorizedSenderDevice;
    return Result::Accepted;
}
Creation MessageLayer::create(const std::string& local,const GroupState& ledger,const Blob& plaintext){
    Creation result;
    try{
        const group_keys::RosterSnapshot* roster=nullptr;result.result=context(local,ledger,roster);
        if(result.result!=Result::Accepted)return result;
        if(plaintext.size()>MaxPlaintextBytes){result.result=Result::MalformedEnvelope;return result;}
        result.result=Result::StorageFailed;Secret key{store_.groupKey(local,roster->generation)};
        if(key.bytes.empty()){result.result=Result::MissingGroupKey;return result;}
        Envelope e;e.groupId=roster->serverGroupId;e.rosterGeneration=e.keyGeneration=roster->generation;
        e.messageId=Storage::newMessageId();e.senderAccount=account_;e.senderDevice=device_.peerId;e.nonce.resize(12);
        bool reserved=false;
        for(int attempt=0;attempt<32&&!reserved;++attempt){
            nt(BCryptGenRandom(nullptr,e.nonce.data(),12,BCRYPT_USE_SYSTEM_PREFERRED_RNG));
            reserved=store_.reserveGroupNonce(Storage::publicKeyId(key.bytes),e.nonce);
        }
        if(!reserved){result.result=Result::CryptoFailed;return result;}
        // Set length before computing AAD; encryption replaces this placeholder.
        e.ciphertext.resize(plaintext.size());e.tag.resize(16);auto aad=associatedData(e);
        result.result=Result::CryptoFailed;
        if(!gcm(true,key.bytes,aad,e.nonce,plaintext,e.ciphertext,e.tag))return result;
        e.signature=signDevicePayload(signingPayload(e),device_);auto wire=serialize(e);
        // Local authenticated history makes retry bytes durable without a second delivery owner.
        result.result=Result::StorageFailed;
        if(!store_.appendAuthenticatedGroupMessage(e.groupId,e.messageId,wire,plaintext)){result.result=Result::ConflictingMessage;return result;}
        result.envelope=std::move(e);result.wire=std::move(wire);result.result=Result::Accepted;
    }catch(...){/* Bounded status only. Never expose provider inputs or message contents. */}
    return result;
}
Reception MessageLayer::receive(const std::string& local,const GroupState& ledger,const Blob& wire){
    Reception result;result.result=parse(wire,result.envelope);if(result.result!=Result::Accepted)return result;
    try{
        const group_keys::RosterSnapshot* roster=nullptr;result.result=context(local,ledger,roster);
        if(result.result!=Result::Accepted)return result;
        const auto& e=result.envelope;
        if(e.groupId!=roster->serverGroupId){result.result=Result::WrongGroup;return result;}
        if(e.rosterGeneration!=roster->generation){result.result=e.rosterGeneration<roster->generation?Result::StaleRosterGeneration:Result::FutureRosterGeneration;return result;}
        if(e.keyGeneration!=roster->generation){result.result=e.keyGeneration<roster->generation?Result::StaleKeyGeneration:Result::FutureKeyGeneration;return result;}
        auto member=ledger.members.find(e.senderAccount);
        if(member==ledger.members.end()){result.result=Result::SenderNotMember;return result;}
        if(!group_keys::GroupKeyManager::currentMember(*roster,e.senderAccount,e.senderDevice)||member->second.peerId!=e.senderDevice||
            Storage::publicKeyId(member->second.publicKey)!=e.senderDevice){result.result=Result::UnauthorizedSenderDevice;return result;}
        result.result=Result::SignatureFailed;
        if(!verifyDevicePayload(signingPayload(e),e.signature,member->second.publicKey))return result;
        result.result=Result::StorageFailed;Secret key{store_.groupKey(local,e.keyGeneration)};
        if(key.bytes.empty()){result.result=Result::MissingGroupKey;return result;}
        Secret plaintext;auto tag=e.tag;result.result=Result::AeadAuthFailed;
        if(!gcm(false,key.bytes,associatedData(e),e.nonce,e.ciphertext,plaintext.bytes,tag))return result;
        // Dedupe happens after authentication. A failed packet cannot reserve an ID.
        result.result=Result::StorageFailed;
        try{
            if(!store_.appendAuthenticatedGroupMessage(e.groupId,e.messageId,wire,plaintext.bytes)){result.result=Result::DuplicateMessage;return result;}
        }catch(const std::runtime_error& error){
            if(std::string(error.what())=="Conflicting authenticated group message ID")result.result=Result::ConflictingMessage;
            return result;
        }
        result.plaintext=std::move(plaintext.bytes);result.result=Result::Accepted;
    }catch(...){if(result.result==Result::Accepted)result.result=Result::CryptoFailed;/* No plaintext/history/UI emission on any failure. */}
    return result;
}
}
