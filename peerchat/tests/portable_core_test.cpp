#include "../portable/peerchat_core.h"
#include "../portable/identity_format.hpp"
#include "../portable/utf8.hpp"
#include "../protocol.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <array>
void check(bool condition){if(!condition)throw std::runtime_error("Portable ABI regression failed");}
int main(){try{
    check(pc_core_create(nullptr)==PC_CORE_INVALID_ARGUMENT);
    check(pc_core_destroy(0)==PC_CORE_INVALID_HANDLE);
    std::string text=u8"PeerChat: Jonah / 日本語 / café / 😀";
    check(peerchat::unicode::encode(peerchat::unicode::decode(text))==text);
    std::string embedded("a\0b",3);check(peerchat::unicode::encode(peerchat::unicode::decode(embedded))==embedded);
    peerchat::protocol::Writer writer(peerchat::protocol::Text);writer.text(peerchat::unicode::decode(text));
    peerchat::protocol::Reader reader(writer.data);check(peerchat::unicode::encode(reader.text())==text);reader.end();
    uint64_t previous=0;
    for(int cycle=0;cycle<1000;++cycle){pc_core_handle h=0;check(pc_core_create(&h)==PC_CORE_OK&&h>previous);previous=h;
        pc_core_info info{};info.struct_size=sizeof(info);check(pc_core_get_info(h,&info)==PC_CORE_OK&&info.abi_version==1&&info.session_policy_available&&!info.identity_available&&!info.storage_available&&!info.peer_tls_available);
        uint32_t size=0;check(pc_core_exchange_utf8(h,reinterpret_cast<const uint8_t*>(text.data()),static_cast<uint32_t>(text.size()),nullptr,0,&size)==PC_CORE_BUFFER_TOO_SMALL&&size==text.size());
        std::vector<uint8_t> out(size+2,0xaa);check(pc_core_exchange_utf8(h,reinterpret_cast<const uint8_t*>(text.data()),size,out.data()+1,size-1,&size)==PC_CORE_BUFFER_TOO_SMALL);for(auto b:out)check(b==0xaa);
        check(pc_core_exchange_utf8(h,reinterpret_cast<const uint8_t*>(text.data()),size,out.data()+1,size,&size)==PC_CORE_OK);
        check(out.front()==0xaa&&out.back()==0xaa&&std::string(out.begin()+1,out.end()-1)==text);
        check(pc_core_exchange_utf8(h,nullptr,1,nullptr,0,&size)==PC_CORE_INVALID_ARGUMENT);
        check(pc_core_exchange_utf8(h,nullptr,0,nullptr,0,&size)==PC_CORE_OK&&size==0);
        check(pc_core_open_profile(h,reinterpret_cast<const uint8_t*>("profile"),7)==PC_CORE_PLATFORM_UNAVAILABLE);
        check(pc_core_destroy(h)==PC_CORE_OK&&pc_core_destroy(h)==PC_CORE_INVALID_HANDLE);
        check(pc_core_get_info(h,&info)==PC_CORE_INVALID_HANDLE);
    }
    pc_core_handle h=0;check(pc_core_create(&h)==PC_CORE_OK);uint32_t size=0;
    std::array<uint8_t,3> embeddedCopy{};
    check(pc_core_exchange_utf8(h,reinterpret_cast<const uint8_t*>(embedded.data()),3,embeddedCopy.data(),3,&size)==PC_CORE_OK&&std::string(embeddedCopy.begin(),embeddedCopy.end())==embedded);
    const std::vector<std::string> bad={std::string("\xc0\x80",2),std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4),std::string("\xe2\x82",2),std::string("\x80",1)};
    for(auto& s:bad)check(pc_core_exchange_utf8(h,reinterpret_cast<const uint8_t*>(s.data()),static_cast<uint32_t>(s.size()),nullptr,0,&size)==PC_CORE_INVALID_UTF8);
    std::string maximum(65536,'x');check(pc_core_exchange_utf8(h,reinterpret_cast<const uint8_t*>(maximum.data()),65536,nullptr,0,&size)==PC_CORE_BUFFER_TOO_SMALL&&size==65536);
    check(pc_core_exchange_utf8(h,reinterpret_cast<const uint8_t*>(maximum.data()),65537,nullptr,0,&size)==PC_CORE_INVALID_ARGUMENT);
    check(pc_core_exchange_utf8(h,nullptr,0,nullptr,0,nullptr)==PC_CORE_INVALID_ARGUMENT);
    pc_core_info wrong{};check(pc_core_get_info(h,&wrong)==PC_CORE_INVALID_ARGUMENT);
    std::thread worker([h]{for(int i=0;i<1000;++i){pc_core_info info{};info.struct_size=sizeof(info);auto status=pc_core_get_info(h,&info);check(status==PC_CORE_OK||status==PC_CORE_INVALID_HANDLE);}});
    check(pc_core_destroy(h)==PC_CORE_OK);worker.join();
    std::array<uint8_t,32> x{},y{};x[31]=1;y[31]=2;auto key=peerchat::identity_format::publicKey(x.data(),y.data());
    check(key.size()==72&&key[0]==0x45&&key[3]==0x31&&key[4]==32&&key[39]==1&&key[71]==2);
    check(peerchat::identity_format::validPublicRepresentation(key.data(),key.size()));key[0]=0;check(!peerchat::identity_format::validPublicRepresentation(key.data(),key.size()));
    auto sig=peerchat::identity_format::signature(x.data(),y.data());check(sig.size()==64&&sig[31]==1&&sig[63]==2);
    std::cout<<"PASS: 1000 lifecycle cycles, UTF-8/PT1, buffers, errors, concurrent retirement, public/signature representation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
