// Existing-profile diagnosis only: SQLite READONLY, no Storage/Profile creation,
// no key access, no policy mutation. Output contains public identity references.
#include "../storage.cpp"
#include "../archive_authorization.hpp"
#include <iostream>
int main(int argc,char** argv){try{
    using namespace peerchat;namespace ar=peerchat::archive;
    if(argc!=2)throw std::runtime_error("Existing database path required");
    storage_detail::Sqlite api;int (*open)(const char*,storage_detail::sqlite3**,int,const char*){};
    api.load(open,"sqlite3_open_v2");storage_detail::sqlite3* db=nullptr;
    if(open(argv[1],&db,1,nullptr)!=0)throw std::runtime_error("Readonly database open failed");
    struct Close{storage_detail::Sqlite& api;storage_detail::sqlite3* db;~Close(){api.close(db);}}close{api,db};
    Blob plain;
    {storage_detail::WindowsStatement s(api,db,"SELECT b FROM objects WHERE k='archive-authorization/v1'");
     if(api.step(s.handle)!=100){std::cout<<"policy=missing\n";return 0;}
     auto n=api.column_bytes(s.handle,0);if(n<=0||n>1024*1024)throw std::runtime_error("Protected policy bounds rejected");
     auto p=static_cast<const unsigned char*>(api.column_blob(s.handle,0));
     platform::WindowsRecordProtector records;plain=records.unprotect(Blob(p,p+n),"PeerChat.v1/archive-authorization/v1");}
    struct Wipe{Blob& bytes;~Wipe(){SecureZeroMemory(bytes.data(),bytes.size());}}wipe{plain};
    protocol::Reader r(plain);ar::require(r.type()==ar::LocalFormat);auto version=r.string();
    ar::require(version=="PeerChat/ArchiveLocalPolicy/v1"||version=="PeerChat/ArchiveLocalPolicy/v2");
    auto account=r.number();auto device=r.string();std::cout<<"account="<<account<<" local_device="<<device<<"\n";
    auto count=r.number();ar::require(count<=ar::MaxAuthorities);std::map<std::string,Blob> keys;
    for(std::uint64_t i=0;i<count;++i){auto id=r.string();auto key=r.bytes();auto enabled=r.number();ar::require(protocol::id(id)&&key.size()==72&&Storage::publicKeyId(key)==id&&enabled<=1);keys[id]=key;std::cout<<"authorizer="<<id<<" trusted="<<enabled<<"\n";}
    count=r.number();ar::require(count<=ar::MaxGrants);
    for(std::uint64_t i=0;i<count;++i){auto g=ar::decodeGrant(r.bytes());auto key=keys.find(g.issuer);ar::require(key!=keys.end()&&verifyDevicePayload(ar::grantPayload(g),g.signature,key->second));std::cout<<"grant authorizer="<<g.issuer<<" device="<<g.device<<" id="<<g.issuanceId<<" generation="<<g.generation<<" permissions="<<g.capabilities<<" revoked="<<g.revoked<<" signature_verified=1\n";}
    if(version=="PeerChat/ArchiveLocalPolicy/v2"){count=r.number();ar::require(count<=ar::MaxPendingGrants);for(std::uint64_t i=0;i<count;++i)(void)ar::decodeGrant(r.bytes());std::cout<<"pending_publications="<<count<<"\n";}r.end();
    protocol::Writer context(ar::LocalFormat);context.string("PeerChat/ArchiveIntentContext/v1");context.number(account);context.string(device);
    std::string name="archive-sync/";const char hex[]="0123456789abcdef";for(auto b:digestDevicePayload(context.data)){name+=hex[b>>4];name+=hex[b&15];}
    storage_detail::WindowsStatement s(api,db,"SELECT b FROM objects WHERE k=?");api.bind_text(s.handle,1,name.data(),static_cast<int>(name.size()),reinterpret_cast<void(*)(void*)>(-1));
    if(api.step(s.handle)==100){auto n=api.column_bytes(s.handle,0);ar::require(n>0&&n<=65536);auto p=static_cast<const unsigned char*>(api.column_blob(s.handle,0));platform::WindowsRecordProtector records;auto intent=records.unprotect(Blob(p,p+n),"PeerChat.v1/"+name);protocol::Reader state(intent);ar::require(state.type()==ar::LocalFormat);std::cout<<"intent_format="<<state.string()<<" account="<<state.number()<<" local_device="<<state.string()<<" issuer="<<state.string()<<" source="<<state.string()<<" disposition="<<state.number()<<"\n";state.end();}
    else std::cout<<"intent=missing\n";
    return 0;
}catch(...){std::cerr<<"Readonly policy diagnosis failed closed\n";return 1;}}
