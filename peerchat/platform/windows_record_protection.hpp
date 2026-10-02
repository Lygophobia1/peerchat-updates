#pragma once
#include "record_protection.hpp"
#include <windows.h>
#include <wincrypt.h>
#include <stdexcept>

namespace peerchat::platform {
class WindowsRecordProtector final : public RecordProtector {
    static Blob transform(const Blob& plain,const std::string& context,bool encrypt){
        if(plain.empty()||plain.size()>1024*1024)throw std::runtime_error("Invalid protected record size");
        DATA_BLOB in{static_cast<DWORD>(plain.size()),const_cast<BYTE*>(plain.data())};
        DATA_BLOB entropy{static_cast<DWORD>(context.size()),reinterpret_cast<BYTE*>(const_cast<char*>(context.data()))},out{};
        BOOL ok=encrypt?CryptProtectData(&in,L"PeerChat record",&entropy,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out)
                       :CryptUnprotectData(&in,nullptr,&entropy,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out);
        if(ok==FALSE){DWORD error=GetLastError();throw std::runtime_error(std::string(encrypt?"Could not encrypt local data":"Local data failed authentication or belongs to another Windows account")+" (DPAPI Win32 error "+std::to_string(error)+")");}
        struct Cleanup{DATA_BLOB& blob;~Cleanup(){SecureZeroMemory(blob.pbData,blob.cbData);LocalFree(blob.pbData);}} cleanup{out};
        return Blob(out.pbData,out.pbData+out.cbData);
    }
public:
    Blob protect(const Blob& value,const std::string& context) override{return transform(value,context,true);}
    Blob unprotect(const Blob& value,const std::string& context) override{return transform(value,context,false);}
};
}
