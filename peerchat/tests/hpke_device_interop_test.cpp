#include "../platform/hpke_provider.hpp"
#include "../platform/windows_recipient_key.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace peerchat::platform;
using Bytes=HpkeProvider::Bytes;
static void check(bool ok){if(!ok)throw std::runtime_error("Physical HPKE interoperability failed");}
static Bytes text(const char* s){return Bytes(s,s+std::char_traits<char>::length(s));}
static Bytes plain(unsigned i){auto b=text("Synthetic physical HPKE interoperability");b.push_back(i>>8);b.push_back(i&255);return b;}
static Bytes read(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);check(bool(in));Bytes b((std::istreambuf_iterator<char>(in)),{});check(b.size()<=20000);return b;}
static void write(const std::filesystem::path& p,const Bytes& b){std::ofstream out(p,std::ios::binary);out.write(reinterpret_cast<const char*>(b.data()),b.size());check(bool(out));}
static void cleanup(const std::string& name){recipient_cng::Object p,k;recipient_cng::success(NCryptOpenStorageProvider(&p.value,MS_KEY_STORAGE_PROVIDER,0));
    const auto alias=recipient_cng::alias(name);auto status=NCryptOpenKey(p.value,&k.value,alias.c_str(),0,0);
    if(status==NTE_BAD_KEYSET)return;
    recipient_cng::success(status);recipient_cng::success(NCryptDeleteKey(k.value,0));k.value=0;
}
int main(int argc,char** argv){try{
    check(argc==4);const std::string mode=argv[1],token=argv[3];
    check(token.size()==32&&std::all_of(token.begin(),token.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');}));
    const auto alias="PeerChat.Recipient.test.physical."+token;
    if(mode=="--cleanup"){cleanup(alias);return 0;}
    const std::filesystem::path root=argv[2];WindowsRecipientKeyProvider keys;auto hpke=createShippingHpke();
    const auto info=text("PeerChat/physical-HPKE-test/v1"),aad=text("test-only/account/device/message");
    if(mode=="--seal"){
        auto key=keys.create(alias);write(root/"windows-public.bin",key->publicKey());
        const auto android=read(root/"android-public.bin");Bytes output;
        for(unsigned i=0;i<100;++i){auto envelope=hpke->seal(android,info,aad,plain(i));output.insert(output.end(),envelope.enc.begin(),envelope.enc.end());output.insert(output.end(),envelope.ciphertext.begin(),envelope.ciphertext.end());}
        write(root/"windows-cipher.bin",output);std::cout<<"PASS: 100 Windows Go envelopes sealed for physical Keystore recipient\n";
    }else{
        check(mode=="--open");auto key=keys.open(alias);const auto input=read(root/"android-cipher.bin");const auto size=65+plain(0).size()+16;check(input.size()==100*size);
        for(unsigned i=0;i<100;++i){auto begin=input.begin()+i*size;HpkeEnvelope e{Bytes(begin,begin+65),Bytes(begin+65,begin+size)};
            check(hpke->open(key,key->publicKey(),info,aad,e)==plain(i));e.ciphertext[0]^=1;bool rejected=false;try{hpke->open(key,key->publicKey(),info,aad,e);}catch(const std::exception&){rejected=true;}check(rejected);
        }
        // Public-only export policy must remain zero throughout both processes.
        recipient_cng::Object provider,opened;recipient_cng::success(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0));auto n=recipient_cng::alias(alias);
        recipient_cng::success(NCryptOpenKey(provider.value,&opened.value,n.c_str(),0,0));DWORD length=0;
        check(NCryptExportKey(opened.value,0,BCRYPT_ECCPRIVATE_BLOB,nullptr,nullptr,0,&length,0)!=ERROR_SUCCESS);
        std::cout<<"PASS: 100 physical Android envelopes opened by opaque Windows CNG; tampering rejected; private export denied\n";
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
