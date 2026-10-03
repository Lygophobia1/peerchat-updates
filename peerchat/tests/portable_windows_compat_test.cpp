#include "../storage.cpp"
#include "../delivery_manager.hpp"
#include "../portable/identity_format.hpp"
#include <iostream>

peerchat::Blob legacy(const peerchat::Blob& value,const std::string& context,bool encrypt){
    DATA_BLOB input{static_cast<DWORD>(value.size()),const_cast<BYTE*>(value.data())};
    DATA_BLOB entropy{static_cast<DWORD>(context.size()),reinterpret_cast<BYTE*>(const_cast<char*>(context.data()))},output{};
    BOOL ok=encrypt?CryptProtectData(&input,L"PeerChat record",&entropy,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output)
        :CryptUnprotectData(&input,nullptr,&entropy,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output);
    if(!ok)throw std::runtime_error("DPAPI control failed; Win32 error "+std::to_string(GetLastError()));
    peerchat::Blob result(output.pbData,output.pbData+output.cbData);SecureZeroMemory(output.pbData,output.cbData);LocalFree(output.pbData);return result;
}
peerchat::Blob hexBytes(const std::string& text){peerchat::Blob out;for(size_t i=0;i<text.size();i+=2)out.push_back(static_cast<unsigned char>(std::stoul(text.substr(i,2),nullptr,16)));return out;}
int main(){using namespace peerchat;try{
    const Blob plaintext{1,2,3,4,5};const std::string context="PeerChat.v1/portability-test";
    platform::WindowsRecordProtector provider;
    storage_detail::require(provider.unprotect(legacy(plaintext,context,true),context)==plaintext,"Legacy->provider record mismatch");
    storage_detail::require(legacy(provider.protect(plaintext,context),context,false)==plaintext,"Provider->legacy record mismatch");
    bool rejected=false;try{provider.unprotect(provider.protect(plaintext,context),context+"wrong");}catch(...){rejected=true;}
    storage_detail::require(rejected,"Wrong context accepted");
    const auto key=hexBytes("45435331200000006b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c2964fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5");
    auto encoded=identity_format::publicKey(key.data()+8,key.data()+40);
    storage_detail::require(Blob(encoded.begin(),encoded.end())==key,"CNG public representation changed");
    storage_detail::require(Storage::publicKeyId(key)=="0bc654178e6f3d5e4ab8b170722a7b7a388a76d58c97031fcad019cf4bdb5e91","Peer ID vector changed");
    auto path=(std::filesystem::current_path()/L"build"/L"android-foundation"/L"profiles"/storage_detail::wide(Storage::newMessageId())).wstring();
    Profile identity;std::string messageId;
    {Storage store(path);identity=store.profile();Blob payload{9,8,7};auto signature=signDevicePayload(payload,identity);
        storage_detail::require(signature.size()==64&&verifyDevicePayload(payload,signature,identity.publicKey),"ECDSA representation mismatch");
        StoredMessage message;message.id=Storage::newMessageId();messageId=message.id;message.sender=identity.peerId;message.recipient=Storage::publicKeyId(key);message.text=L"Storage regression";message.utcMilliseconds=123;message.delivery=Delivery::Queued;
        storage_detail::require(store.append(message.recipient,message),"Outbox insert failed");storage_detail::require(!store.append(message.recipient,message),"Dedupe failed");}
    {Storage store(path);storage_detail::require(store.profile().peerId==identity.peerId&&store.profile().publicKey==identity.publicKey,"Restart rotated identity");
        delivery::DeliveryManager delivery(store,identity.peerId);const auto peer=Storage::publicKeyId(key);
        storage_detail::require(delivery.pendingMessages(peer).size()==1,"Durable outbox missing");
        storage_detail::require(delivery.authorizeTransport({1},peer)&&delivery.submittedMessage(peer,messageId,{1})&&delivery.acknowledgeMessage(peer,{1},messageId),"Authenticated ACK failed");
        storage_detail::require(delivery.pendingMessages(peer).empty()&&store.history(peer,10,INT64_MAX).size()==1,"History/outbox mismatch");}
    std::cout<<"PASS: DPAPI cross-provider compatibility, context binding, Peer ID vector, raw ECDSA, identity restart, history/outbox/ACK/dedupe\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
