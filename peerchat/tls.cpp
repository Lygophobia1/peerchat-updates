#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define SECURITY_WIN32
#include "tls.hpp"
#include <windows.h>
#include <security.h>
#include <schannel.h>
#include <ncrypt.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <array>
#include <cstring>
#include <stdexcept>

namespace peerchat {
namespace tls_detail {
void check(bool ok,const char* context){if(!ok)throw std::runtime_error(context);}
void security(SECURITY_STATUS status,const char* context){if(status<0)throw std::runtime_error(std::string(context)+" ("+std::to_string(static_cast<unsigned long>(status))+")");}
constexpr size_t MaxBuffered=256*1024;
void append(Blob& target,const unsigned char* data,size_t size){check(size<=MaxBuffered&&target.size()<=MaxBuffered-size,"TLS buffer limit reached");if(size)target.insert(target.end(),data,data+size);}
struct Cert {PCCERT_CONTEXT h{};~Cert(){if(h)CertFreeCertificateContext(h);}};
struct Key {NCRYPT_KEY_HANDLE h{};~Key(){if(h)NCryptFreeObject(h);}};
struct Provider {NCRYPT_PROV_HANDLE h{};~Provider(){if(h)NCryptFreeObject(h);}};
struct Credential {CredHandle h{};bool valid=false;~Credential(){if(valid)FreeCredentialsHandle(&h);}};
Blob certificateKey(PCCERT_CONTEXT certificate) {
    BCRYPT_KEY_HANDLE key{};
    check(CryptImportPublicKeyInfoEx2(X509_ASN_ENCODING,&certificate->pCertInfo->SubjectPublicKeyInfo,0,nullptr,&key)!=FALSE,"Unsupported peer certificate public key");
    ULONG n=0;auto status=BCryptExportKey(key,nullptr,BCRYPT_ECCPUBLIC_BLOB,nullptr,0,&n,0);Blob result(n);
    if(status>=0&&n<=1024)status=BCryptExportKey(key,nullptr,BCRYPT_ECCPUBLIC_BLOB,result.data(),n,&n,0);
    BCryptDestroyKey(key);security(status,"Could not read peer public identity");
    check(result.size()==sizeof(BCRYPT_ECCKEY_BLOB)+64,"Peer identity is not P-256");
    auto* header=reinterpret_cast<const BCRYPT_ECCKEY_BLOB*>(result.data());check(header->dwMagic==BCRYPT_ECDSA_PUBLIC_P256_MAGIC,"Unsupported peer identity curve");return result;
}
}
using namespace tls_detail;
struct TlsCredentials::Impl {
    Provider provider;Key key;Cert certificate;Credential inbound,outbound;
    explicit Impl(const Profile& profile) {
        check(Storage::publicKeyId(profile.publicKey)==profile.peerId,"Invalid local identity");
        security(NCryptOpenStorageProvider(&provider.h,MS_KEY_STORAGE_PROVIDER,0),"Cannot open Windows key provider");
        std::wstring keyName=L"PeerChat-"+std::wstring(profile.peerId.begin(),profile.peerId.end());
        // Schannel requires a provider-backed key container on supported Windows
        // versions. Reuse this installation's named key, never a global shared key.
        if(NCryptOpenKey(provider.h,&key.h,keyName.c_str(),0,0)!=ERROR_SUCCESS) {
            NCryptBuffer keyParameter{static_cast<ULONG>((keyName.size()+1)*sizeof(wchar_t)),NCRYPTBUFFER_PKCS_KEY_NAME,keyName.data()};
            NCryptBufferDesc parameters{NCRYPTBUFFER_VERSION,1,&keyParameter};
            security(NCryptImportKey(provider.h,0,BCRYPT_ECCPRIVATE_BLOB,&parameters,&key.h,
                const_cast<PBYTE>(profile.privateKey.data()),static_cast<DWORD>(profile.privateKey.size()),0),"Cannot import persistent identity");
        }
        std::wstring name=L"CN=PeerChat "+std::wstring(profile.peerId.begin(),profile.peerId.end());DWORD size=0;
        check(CertStrToNameW(X509_ASN_ENCODING,name.c_str(),CERT_X500_NAME_STR,nullptr,nullptr,&size,nullptr)!=FALSE,"Cannot encode certificate name");
        Blob subject(size);check(CertStrToNameW(X509_ASN_ENCODING,name.c_str(),CERT_X500_NAME_STR,nullptr,subject.data(),&size,nullptr)!=FALSE,"Cannot encode identity certificate");
        CERT_NAME_BLOB subjectBlob{size,subject.data()};CRYPT_ALGORITHM_IDENTIFIER algorithm{};algorithm.pszObjId=const_cast<LPSTR>(szOID_ECDSA_SHA256);
        SYSTEMTIME end{};GetSystemTime(&end);end.wYear+=10;if(end.wMonth==2&&end.wDay==29)end.wDay=28;
        CRYPT_KEY_PROV_INFO keyInfo{};keyInfo.pwszContainerName=keyName.data();keyInfo.pwszProvName=const_cast<LPWSTR>(MS_KEY_STORAGE_PROVIDER);keyInfo.dwKeySpec=0;
        certificate.h=CertCreateSelfSignCertificate(key.h,&subjectBlob,0,&keyInfo,&algorithm,nullptr,&end,nullptr);
        check(certificate.h!=nullptr,"Cannot create identity certificate");
        HCRYPTPROV_OR_NCRYPT_KEY_HANDLE acquired{};DWORD spec=0;BOOL release=FALSE;
        check(CryptAcquireCertificatePrivateKey(certificate.h,CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG|CRYPT_ACQUIRE_COMPARE_KEY_FLAG,nullptr,&acquired,&spec,&release)!=FALSE,"Certificate provider cannot reopen the persistent private key");
        if(release)NCryptFreeObject(acquired);
        check(certificateKey(certificate.h)==profile.publicKey,"Certificate key does not match saved identity");
        acquire(inbound,true);acquire(outbound,false);
    }
    void acquire(Credential& target,bool server) {
        SCHANNEL_CRED config{};config.dwVersion=SCHANNEL_CRED_VERSION;config.cCreds=1;config.paCred=&certificate.h;
        config.grbitEnabledProtocols=server?SP_PROT_TLS1_2_SERVER:SP_PROT_TLS1_2_CLIENT;
        config.dwFlags=SCH_CRED_NO_SYSTEM_MAPPER|SCH_USE_STRONG_CRYPTO;
        if(!server)config.dwFlags|=SCH_CRED_NO_DEFAULT_CREDS|SCH_CRED_MANUAL_CRED_VALIDATION;
        TimeStamp expiry{};security(AcquireCredentialsHandleW(nullptr,const_cast<LPWSTR>(UNISP_NAME_W),server?SECPKG_CRED_INBOUND:SECPKG_CRED_OUTBOUND,nullptr,&config,nullptr,nullptr,&target.h,&expiry),server?"Cannot acquire inbound TLS credentials":"Cannot acquire outbound TLS credentials");target.valid=true;
    }
};
struct TlsSession::Impl {
    std::shared_ptr<TlsCredentials::Impl> identity;bool incoming,initialized=false,established=false,ended=false;
    CtxtHandle context{};Blob input,output,plaintext,remoteKey;SecPkgContext_StreamSizes sizes{};
    Impl(std::shared_ptr<TlsCredentials::Impl> credentials,bool incoming):identity(std::move(credentials)),incoming(incoming){if(!incoming)handshake();}
    ~Impl(){if(initialized)DeleteSecurityContext(&context);}
    void handshake() {
        for(int rounds=0;rounds<32&&!established&&!ended;rounds++) {
            if((incoming||initialized)&&input.empty())return;
            SecBuffer inBuffers[2]={{static_cast<ULONG>(input.size()),SECBUFFER_TOKEN,input.data()},{0,SECBUFFER_EMPTY,nullptr}};
            SecBufferDesc inDesc{SECBUFFER_VERSION,2,inBuffers};
            SecBuffer token{0,SECBUFFER_TOKEN,nullptr};SecBufferDesc outDesc{SECBUFFER_VERSION,1,&token};
            ULONG attributes=0;TimeStamp expiry{};SECURITY_STATUS result;
            if(incoming)result=AcceptSecurityContext(&identity->inbound.h,initialized?&context:nullptr,&inDesc,
                ASC_REQ_ALLOCATE_MEMORY|ASC_REQ_CONFIDENTIALITY|ASC_REQ_REPLAY_DETECT|ASC_REQ_SEQUENCE_DETECT|ASC_REQ_STREAM|ASC_REQ_MUTUAL_AUTH,
                SECURITY_NATIVE_DREP,&context,&outDesc,&attributes,&expiry);
            else result=InitializeSecurityContextW(&identity->outbound.h,initialized?&context:nullptr,nullptr,
                ISC_REQ_ALLOCATE_MEMORY|ISC_REQ_CONFIDENTIALITY|ISC_REQ_REPLAY_DETECT|ISC_REQ_SEQUENCE_DETECT|ISC_REQ_STREAM|ISC_REQ_USE_SUPPLIED_CREDS|ISC_REQ_MANUAL_CRED_VALIDATION,
                0,SECURITY_NATIVE_DREP,input.empty()?nullptr:&inDesc,0,&context,&outDesc,&attributes,&expiry);
            if(token.pvBuffer){try{append(output,static_cast<unsigned char*>(token.pvBuffer),token.cbBuffer);}catch(...){FreeContextBuffer(token.pvBuffer);throw;}FreeContextBuffer(token.pvBuffer);}
            if(result==SEC_E_INCOMPLETE_MESSAGE)return;
            if(result>=0)initialized=true;
            security(result,"TLS handshake failed");
            if(inBuffers[1].BufferType==SECBUFFER_EXTRA){check(inBuffers[1].cbBuffer<=input.size(),"Invalid TLS extra data");input.erase(input.begin(),input.end()-inBuffers[1].cbBuffer);}else input.clear();
            if(result==SEC_E_OK) {
                Cert peer;security(QueryContextAttributesW(&context,SECPKG_ATTR_REMOTE_CERT_CONTEXT,&peer.h),"Peer did not present a certificate");
                check(CertVerifyTimeValidity(nullptr,peer.h->pCertInfo)==0,"Peer certificate expired or is not yet valid");
                check(CryptVerifyCertificateSignatureEx(0,X509_ASN_ENCODING,CRYPT_VERIFY_CERT_SIGN_SUBJECT_CERT,const_cast<PCERT_CONTEXT>(peer.h),CRYPT_VERIFY_CERT_SIGN_ISSUER_CERT,const_cast<PCERT_CONTEXT>(peer.h),0,nullptr)!=FALSE,"Peer certificate is not a valid self-signed identity");
                remoteKey=certificateKey(peer.h);security(QueryContextAttributesW(&context,SECPKG_ATTR_STREAM_SIZES,&sizes),"Cannot query TLS stream sizes");
                established=true;return;
            }
            check(result==SEC_I_CONTINUE_NEEDED,"Unsupported TLS handshake state");
        }
    }
    void decrypt() {
        while(established&&!input.empty()&&!ended) {
            SecBuffer buffers[4]={{static_cast<ULONG>(input.size()),SECBUFFER_DATA,input.data()},{0,SECBUFFER_EMPTY,nullptr},{0,SECBUFFER_EMPTY,nullptr},{0,SECBUFFER_EMPTY,nullptr}};
            SecBufferDesc desc{SECBUFFER_VERSION,4,buffers};auto result=DecryptMessage(&context,&desc,0,nullptr);
            if(result==SEC_E_INCOMPLETE_MESSAGE)return;
            if(result==SEC_I_CONTEXT_EXPIRED){ended=true;return;}
            security(result,"TLS record authentication failed");check(result!=SEC_I_RENEGOTIATE,"TLS renegotiation is not supported");
            size_t remaining=0;
            for(const auto& b:buffers){if(b.BufferType==SECBUFFER_DATA)append(plaintext,static_cast<unsigned char*>(b.pvBuffer),b.cbBuffer);if(b.BufferType==SECBUFFER_EXTRA)remaining=b.cbBuffer;}
            check(remaining<input.size(),"TLS decrypt made no progress");input.erase(input.begin(),input.end()-static_cast<std::ptrdiff_t>(remaining));
        }
    }
};
TlsCredentials::TlsCredentials(const Profile& identity):impl_(std::make_shared<Impl>(identity)){}
TlsCredentials::~TlsCredentials()=default;
TlsSession::TlsSession(const TlsCredentials& identity,bool incoming):impl_(std::make_unique<Impl>(identity.impl_,incoming)){}
TlsSession::~TlsSession()=default;
void TlsSession::receive(const Blob& encrypted){append(impl_->input,encrypted.data(),encrypted.size());if(!impl_->established)impl_->handshake();impl_->decrypt();}
Blob TlsSession::takeOutput(){Blob result;result.swap(impl_->output);return result;}
Blob TlsSession::takePlaintext(){Blob result;result.swap(impl_->plaintext);return result;}
bool TlsSession::ready()const{return impl_->established&&!impl_->ended;}
bool TlsSession::closed()const{return impl_->ended;}
Blob TlsSession::remotePublicKey()const{return impl_->remoteKey;}
void TlsSession::send(const Blob& plaintext) {
    auto& s=*impl_;check(ready(),"TLS connection is not established");check(plaintext.size()<=MaxBuffered,"TLS write is too large");
    size_t offset=0;
    while(offset<plaintext.size()) {
        ULONG n=static_cast<ULONG>(std::min<size_t>(s.sizes.cbMaximumMessage,plaintext.size()-offset));Blob record(s.sizes.cbHeader+n+s.sizes.cbTrailer);
        memcpy(record.data()+s.sizes.cbHeader,plaintext.data()+offset,n);
        SecBuffer buffers[4]={{s.sizes.cbHeader,SECBUFFER_STREAM_HEADER,record.data()},{n,SECBUFFER_DATA,record.data()+s.sizes.cbHeader},{s.sizes.cbTrailer,SECBUFFER_STREAM_TRAILER,record.data()+s.sizes.cbHeader+n},{0,SECBUFFER_EMPTY,nullptr}};
        SecBufferDesc desc{SECBUFFER_VERSION,4,buffers};security(EncryptMessage(&s.context,0,&desc,0),"TLS encrypt failed");
        for(int i=0;i<3;i++)append(s.output,static_cast<unsigned char*>(buffers[i].pvBuffer),buffers[i].cbBuffer);
        offset+=n;
    }
}
void TlsSession::shutdown() {impl_->ended=true;} // Socket owner closes after draining its output.
}
