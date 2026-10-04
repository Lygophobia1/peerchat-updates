#include "../platform/windows_recipient_key.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace peerchat::platform;
namespace {
void check(bool value) { if(!value) throw std::runtime_error("CNG recipient assertion failed"); }
template<class F> void rejects(F f) { bool rejected=false; try { f(); } catch(const std::exception&) { rejected=true; } check(rejected); }
// Destruction is deliberately test-only, namespaced, and restricted to exactly
// the two containers created by this invocation, never production aliases.
struct TestContainer {
    std::string name;
    bool created=false;
    ~TestContainer() {
        if(!created) return;
        recipient_cng::Object provider,key;
        if(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0)!=ERROR_SUCCESS) return;
        const auto wide=recipient_cng::alias(name);
        if(NCryptOpenKey(provider.value,&key.value,wide.c_str(),0,NCRYPT_SILENT_FLAG)==ERROR_SUCCESS) {
            if(NCryptDeleteKey(key.value,0)==ERROR_SUCCESS) key.value=0;
        }
    }
};
}
std::vector<unsigned char> fromHex(const std::string& value) {
    check(value.size()%2==0 && value.size()<=130);
    std::vector<unsigned char> result;
    for(std::size_t i=0;i<value.size();i+=2) {
        auto nibble=[](char c) { if(c>='0'&&c<='9') return c-'0'; if(c>='a'&&c<='f') return c-'a'+10; throw std::runtime_error("Invalid test hex"); };
        result.push_back(static_cast<unsigned char>((nibble(value[i])<<4)|nibble(value[i+1])));
    }
    return result;
}
int main(int argc,char** argv) { try {
    std::ostringstream suffix; suffix<<GetCurrentProcessId()<<'.'<<GetTickCount();
    TestContainer a{"PeerChat.Recipient.test."+suffix.str()+".a"},b{"PeerChat.Recipient.test."+suffix.str()+".b"};
    WindowsRecipientKeyProvider provider;
    check(!provider.exists(a.name)); rejects([&]{provider.open(a.name);});
    auto first=provider.create(a.name); a.created=true;
    auto second=provider.create(b.name); b.created=true;
    const auto pub=first->publicKey(); check(pub.size()==65 && provider.exists(a.name));
    if(argc==2 && std::string(argv[1])=="--standard-dh") {
        // Only test public material goes to stdout. Expected agreement is
        // compared in memory and never printed, on either success or failure.
        for(auto byte:pub) std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<static_cast<unsigned>(byte);
        std::cout<<std::endl;
        std::string remoteHex,expectedHex; check(bool(std::getline(std::cin,remoteHex)) && bool(std::getline(std::cin,expectedHex)));
        auto expected=fromHex(expectedHex); auto agreement=first->agreeP256(fromHex(remoteHex));
        const bool same=agreement==expected;
        SecureZeroMemory(expected.data(),expected.size()); SecureZeroMemory(agreement.data(),agreement.size());
        std::fill(expectedHex.begin(),expectedHex.end(),'0'); check(same);
        std::cout<<"PASS: standard BC ECDH -> opaque CNG agreement, RFC big-endian normalization\n"; return 0;
    }
    auto dh=first->agreeP256(second->publicKey()); auto reverse=second->agreeP256(pub);
    check(dh.size()==32 && dh==reverse); SecureZeroMemory(dh.data(),dh.size()); SecureZeroMemory(reverse.data(),reverse.size());
    for(unsigned i=0;i<100;++i) check(provider.open(a.name)->publicKey()==pub);
    rejects([&]{provider.create(a.name);});
    rejects([&]{first->agreeP256({0});}); rejects([&]{first->agreeP256(std::vector<unsigned char>(65,0));});
    auto offCurve=std::vector<unsigned char>(65,0); offCurve[0]=4; rejects([&]{first->agreeP256(offCurve);});
    auto invalid=pub; invalid[0]=2; rejects([&]{first->agreeP256(invalid);});
    recipient_cng::Object ksp,key;
    recipient_cng::success(NCryptOpenStorageProvider(&ksp.value,MS_KEY_STORAGE_PROVIDER,0));
    const auto wide=recipient_cng::alias(a.name);
    recipient_cng::success(NCryptOpenKey(ksp.value,&key.value,wide.c_str(),0,0));
    DWORD bytes=0;
    check(NCryptExportKey(key.value,0,BCRYPT_ECCPRIVATE_BLOB,nullptr,nullptr,0,&bytes,0)!=ERROR_SUCCESS);
    unsigned char signature[128]{}; unsigned char digest[32]{};
    check(NCryptSignHash(key.value,nullptr,digest,sizeof(digest),signature,sizeof(signature),&bytes,0)!=ERROR_SUCCESS);
    std::cout<<"PASS: CNG non-exportable agreement-only keys, symmetric DH, 100 persisted reopens, malformed/off-curve point rejection; test containers cleaned\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; } }
