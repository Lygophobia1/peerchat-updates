#pragma once
#include "device_key_provider.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <ncrypt.h>
#include <algorithm>
#include <stdexcept>

namespace peerchat::platform {
namespace recipient_cng {
inline void require(bool value) { if(!value) throw std::runtime_error("Recipient CNG provider rejected operation"); }
inline void success(SECURITY_STATUS status) { require(status==ERROR_SUCCESS); }
struct Object {
    NCRYPT_HANDLE value=0;
    ~Object() { if(value) NCryptFreeObject(value); }
    Object()=default;
    Object(const Object&)=delete;
    Object& operator=(const Object&)=delete;
};
inline std::wstring alias(const std::string& value) {
    require(value.size()>19 && value.size()<=200 && value.rfind("PeerChat.Recipient.",0)==0);
    require(std::all_of(value.begin(),value.end(),[](unsigned char c) {
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_';
    }));
    return {value.begin(),value.end()};
}

class Handle final : public RecipientKeyHandle {
    Object provider_,key_;
public:
    Handle(const std::string& name,bool create) {
        const auto container=alias(name);
        success(NCryptOpenStorageProvider(&provider_.value,MS_KEY_STORAGE_PROVIDER,0));
        if(create) {
            // No overwrite flag. Existing container is always an error.
            success(NCryptCreatePersistedKey(provider_.value,&key_.value,BCRYPT_ECDH_P256_ALGORITHM,container.c_str(),0,0));
            DWORD exports=0,usage=NCRYPT_ALLOW_KEY_AGREEMENT_FLAG;
            success(NCryptSetProperty(key_.value,NCRYPT_EXPORT_POLICY_PROPERTY,reinterpret_cast<PBYTE>(&exports),sizeof(exports),0));
            success(NCryptSetProperty(key_.value,NCRYPT_KEY_USAGE_PROPERTY,reinterpret_cast<PBYTE>(&usage),sizeof(usage),0));
            success(NCryptFinalizeKey(key_.value,0));
        } else success(NCryptOpenKey(provider_.value,&key_.value,container.c_str(),0,NCRYPT_SILENT_FLAG));
        DWORD exports=0,usage=0,size=0;
        success(NCryptGetProperty(key_.value,NCRYPT_EXPORT_POLICY_PROPERTY,reinterpret_cast<PBYTE>(&exports),sizeof(exports),&size,0));
        require(size==sizeof(exports) && exports==0);
        success(NCryptGetProperty(key_.value,NCRYPT_KEY_USAGE_PROPERTY,reinterpret_cast<PBYTE>(&usage),sizeof(usage),&size,0));
        require(size==sizeof(usage) && usage==NCRYPT_ALLOW_KEY_AGREEMENT_FLAG);
        // Exporting PUBLIC material also verifies curve/algorithm representation.
        publicKey();
    }
    std::vector<unsigned char> publicKey() override {
        DWORD size=0;
        success(NCryptExportKey(key_.value,0,BCRYPT_ECCPUBLIC_BLOB,nullptr,nullptr,0,&size,0));
        require(size==sizeof(BCRYPT_ECCKEY_BLOB)+64);
        std::vector<unsigned char> blob(size);
        success(NCryptExportKey(key_.value,0,BCRYPT_ECCPUBLIC_BLOB,nullptr,blob.data(),size,&size,0));
        const auto* header=reinterpret_cast<const BCRYPT_ECCKEY_BLOB*>(blob.data());
        require(header->dwMagic==BCRYPT_ECDH_PUBLIC_P256_MAGIC && header->cbKey==32);
        std::vector<unsigned char> point{4}; point.insert(point.end(),blob.begin()+sizeof(*header),blob.end()); return point;
    }
    std::vector<unsigned char> agreeP256(const std::vector<unsigned char>& point) override {
        require(point.size()==65 && point[0]==4);
        BCRYPT_ECCKEY_BLOB header{BCRYPT_ECDH_PUBLIC_P256_MAGIC,32};
        std::vector<unsigned char> blob(sizeof(header)+64);
        std::copy_n(reinterpret_cast<unsigned char*>(&header),sizeof(header),blob.begin());
        std::copy(point.begin()+1,point.end(),blob.begin()+sizeof(header));
        Object remote,secret;
        success(NCryptImportKey(provider_.value,0,BCRYPT_ECCPUBLIC_BLOB,nullptr,&remote.value,blob.data(),static_cast<DWORD>(blob.size()),0));
        success(NCryptSecretAgreement(key_.value,remote.value,&secret.value,0));
        std::vector<unsigned char> dh(32); DWORD size=0;
        const auto status=NCryptDeriveKey(secret.value,L"TRUNCATE",nullptr,dh.data(),static_cast<DWORD>(dh.size()),&size,0);
        if(status!=ERROR_SUCCESS || size!=32) { SecureZeroMemory(dh.data(),dh.size()); success(status); require(false); }
        // Microsoft RAW_SECRET is little endian; RFC 9180 DH is big endian.
        std::reverse(dh.begin(),dh.end()); return dh;
    }
};
}

class WindowsRecipientKeyProvider final : public RecipientKeyProvider {
public:
    bool supported() const override {
        recipient_cng::Object provider;
        if(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0)!=ERROR_SUCCESS) return false;
        return NCryptIsAlgSupported(provider.value,BCRYPT_ECDH_P256_ALGORITHM,0)==ERROR_SUCCESS;
    }
    bool exists(const std::string& name) override {
        const auto container=recipient_cng::alias(name);
        recipient_cng::Object provider,key;
        recipient_cng::success(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0));
        const auto status=NCryptOpenKey(provider.value,&key.value,container.c_str(),0,NCRYPT_SILENT_FLAG);
        if(status==NTE_BAD_KEYSET) return false;
        recipient_cng::success(status); return true;
    }
    std::shared_ptr<RecipientKeyHandle> create(const std::string& name) override { return std::make_shared<recipient_cng::Handle>(name,true); }
    std::shared_ptr<RecipientKeyHandle> open(const std::string& name) override { return std::make_shared<recipient_cng::Handle>(name,false); }
};
}
