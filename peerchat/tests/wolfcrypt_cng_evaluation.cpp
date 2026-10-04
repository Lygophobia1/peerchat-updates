// Evaluation only: not linked into PeerChat. Published RFC scalars below are
// synthetic test fixtures, never production/private CNG identity material.
#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/hpke.h>
#include <wolfssl/wolfcrypt/cryptocb.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#include <wolfssl/wolfcrypt/wc_port.h>
#include "../platform/windows_recipient_key.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>

using Bytes = std::vector<unsigned char>;
static void check(bool ok) { if (!ok) throw std::runtime_error("evaluation assertion failed"); }
static Bytes hex(const std::string& s) {
    check(s.size() % 2 == 0 && s.size() <= 4096);
    Bytes out;
    for (size_t i=0;i<s.size();i+=2) {
        auto digit=[](char c)->int { if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; return -1; };
        int a=digit(s[i]),b=digit(s[i+1]);check(a>=0&&b>=0);out.push_back(static_cast<unsigned char>(a*16+b));
    }
    return out;
}
static std::string encode(const Bytes& b) {
    std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto c:b)out<<std::setw(2)<<int(c);return out.str();
}
static Bytes line() { std::string s;check(bool(std::getline(std::cin,s)));return hex(s); }
static Bytes text(const char* s) { return Bytes(s,s+std::char_traits<char>::length(s)); }
static void wipe(Bytes& b) { if(!b.empty())SecureZeroMemory(b.data(),b.size()); }

// Crash-debugging cleanup is explicitly limited to this harness's namespace.
// No normal identity or previously approved recipient container is eligible.
static bool testAlias(const std::string& name) {
    const std::string prefix="PeerChat.Recipient.test.wolf.";
    if(name.rfind(prefix,0)!=0)return false;
    auto suffix=name.substr(prefix.size());
    return !suffix.empty()&&std::count(suffix.begin(),suffix.end(),'.')==1&&
        std::all_of(suffix.begin(),suffix.end(),[](char c){return c=='.'||(c>='0'&&c<='9');});
}
static int maintenance(int argc,char** argv) {
    using namespace peerchat::platform::recipient_cng;
    Object provider;success(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0));
    if(argc==2&&std::string(argv[1])=="--list-test-keys") {
        NCryptKeyName* entry=nullptr;PVOID state=nullptr;SECURITY_STATUS status;
        while((status=NCryptEnumKeys(provider.value,nullptr,&entry,&state,0))==ERROR_SUCCESS){
            std::wstring wide(entry->pszName);std::string name(wide.begin(),wide.end());
            if(testAlias(name))std::cout<<name<<std::endl;
            NCryptFreeBuffer(entry);entry=nullptr;
        }
        if(state)NCryptFreeBuffer(state);
        // NTE_NO_MORE_ITEMS is absent in the pinned MinGW winerror headers.
        check(status==static_cast<SECURITY_STATUS>(0x8009002aL));return 0;
    }
    check(argc==3&&std::string(argv[1])=="--remove-test-key"&&testAlias(argv[2]));
    Object key;auto name=alias(argv[2]);success(NCryptOpenKey(provider.value,&key.value,name.c_str(),0,0));
    success(NCryptDeleteKey(key.value,0));key.value=0;return 0;
}

struct PublicKey {
    ecc_key key{};
    explicit PublicKey(const Bytes& point) {
        check(wc_ecc_init(&key)==0);
        if(point.size()!=65||point[0]!=4||wc_ecc_import_x963_ex(point.data(),word32(point.size()),&key,ECC_SECP256R1)!=0||wc_ecc_check_key(&key)!=0) {
            wc_ecc_free(&key);throw std::runtime_error("invalid P256 public point");
        }
    }
    ~PublicKey(){wc_ecc_free(&key);}
    PublicKey(const PublicKey&)=delete;
};

// The entire evaluation CNG bridge: no KEM/HKDF/AEAD implementation here.
struct OpaqueBridge {
    std::shared_ptr<peerchat::platform::RecipientKeyHandle> handle;
    ecc_key* expected;
    bool fail=false;
    unsigned agreements=0;
    static int callback(int device, wc_CryptoInfo* info, void* ctx) noexcept {
        auto& self=*static_cast<OpaqueBridge*>(ctx);
        if(self.fail||!self.handle||device!=self.expected->devId||!info||info->algo_type!=WC_ALGO_TYPE_PK||info->pk.type!=WC_PK_TYPE_ECDH)
            return WC_HW_E;
        auto& request=info->pk.ecdh;
        if(request.private_key!=self.expected||!request.public_key||!request.out||!request.outlen||*request.outlen<32)
            return WC_HW_E;
        // wolfCrypt invokes the callback before its software validation; retain
        // upstream validation explicitly BEFORE delegating DH to Windows CNG.
        if(!request.public_key->dp||request.public_key->dp->id!=ECC_SECP256R1||
           request.public_key->devId!=INVALID_DEVID||wc_ecc_check_key(request.public_key)!=0)
            return WC_HW_E;
        try {
            Bytes point(65);word32 length=65;
            if(wc_ecc_export_x963(request.public_key,point.data(),&length)!=0||length!=65||point[0]!=4)return WC_HW_E;
            auto dh=self.handle->agreeP256(point);
            if(dh.size()!=32){wipe(dh);return WC_HW_E;}
            std::copy(dh.begin(),dh.end(),request.out);*request.outlen=32;
            wipe(dh);++self.agreements;return 0;
        } catch(...) { return WC_HW_E; }
    }
};

struct Registration {
    int device;
    ~Registration(){wc_CryptoCb_UnRegisterDevice(device);}
};
struct CryptRuntime {
    CryptRuntime(){check(wolfCrypt_Init()==0);}
    ~CryptRuntime(){wolfCrypt_Cleanup();}
};

struct TestContainer {
    std::string name="PeerChat.Recipient.test.wolf."+std::to_string(GetCurrentProcessId())+"."+std::to_string(GetTickCount64());
    std::shared_ptr<peerchat::platform::RecipientKeyHandle> handle;
    bool owned=false;
    TestContainer(){peerchat::platform::WindowsRecipientKeyProvider provider;handle=provider.create(name);owned=true;}
    ~TestContainer(){
        handle.reset();if(!owned)return;
        peerchat::platform::recipient_cng::Object provider,key;
        if(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0)!=ERROR_SUCCESS)return;
        auto alias=peerchat::platform::recipient_cng::alias(name);
        if(NCryptOpenKey(provider.value,&key.value,alias.c_str(),0,0)!=ERROR_SUCCESS)return;
        if(NCryptDeleteKey(key.value,0)==ERROR_SUCCESS)key.value=0;
    }
    void deniedPrivateExport(){
        peerchat::platform::recipient_cng::Object provider,key;
        check(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0)==0);
        auto alias=peerchat::platform::recipient_cng::alias(name);
        check(NCryptOpenKey(provider.value,&key.value,alias.c_str(),0,0)==0);
        DWORD size=0;
        check(NCryptExportKey(key.value,0,BCRYPT_ECCPRIVATE_BLOB,nullptr,nullptr,0,&size,0)!=ERROR_SUCCESS);
    }
};

static void vectors(Hpke& hpke) {
    auto pub=hex("04a92719c6195d5085104f469a8b9814d5838ff72b60501e2c4466e5e67b325ac98536d7b61a1af4b78e5b7f951c0900be863c403ce65c9bfcb9382657222d18c4");
    auto scalar=hex("4995788ef4b9d6132b249ce59a77281493eb39af373d236a1fe415cb0c2d7beb");
    PublicKey ephemeral(pub);
    check(wc_ecc_import_private_key_ex(scalar.data(),word32(scalar.size()),pub.data(),word32(pub.size()),&ephemeral.key,ECC_SECP256R1)==0);wipe(scalar);
    Bytes enc(65);word16 encSize=65;
    check(wc_HpkeSerializePublicKey(&hpke,&ephemeral.key,enc.data(),&encSize)==0&&encSize==65&&enc==pub);
    PublicKey recipient(hex("04fe8c19ce0905191ebc298a9245792531f26f0cece2460639e8bc39cb7f706a826a779b4cf969b8a0e539c7f62fb3d30ad6aa8f80e30f1d128aafd68a2ce72ea0"));
    auto info=hex("4f6465206f6e2061204772656369616e2055726e");
    HpkeBaseContext context{};
    check(wc_HpkeInitSealContext(&hpke,&context,&ephemeral.key,&recipient.key,info.data(),word32(info.size()))==0);
    check(Bytes(context.key,context.key+16)==hex("868c066ef58aae6dc589b6cfdd18f97e"));
    check(Bytes(context.base_nonce,context.base_nonce+12)==hex("4e0bc5018beba4bf004cca59"));
    check(Bytes(context.exporter_secret,context.exporter_secret+32)==hex("14ad94af484a7ad3ef40e9f3be99ecc6fa9036df9d4920548424df127ee0d99f"));
    auto plain=hex("4265617574792069732074727574682c20747275746820626561757479");
    const char* expected[]={
        "5ad590bb8baa577f8619db35a36311226a896e7342a6d836d8b7bcd2f20b6c7f9076ac232e3ab2523f39513434",
        "fa6f037b47fc21826b610172ca9637e82d6e5801eb31cbd3748271affd4ecb06646e0329cbdf3c3cd655b28e82",
        "895cabfac50ce6c6eb02ffe6c048bf53b7f7be9a91fc559402cbc5b8dcaeb52b2ccc93e466c28fb55fed7a7fec"};
    for(int i=0;i<3;++i){auto aad=text(("Count-"+std::to_string(i)).c_str());Bytes out(plain.size()+16);
        check(wc_HpkeContextSealBase(&hpke,&context,aad.data(),word32(aad.size()),plain.data(),word32(plain.size()),out.data())==0);
        check(out==hex(expected[i]));}
    SecureZeroMemory(&context,sizeof(context));wipe(plain);
}

int main(int argc,char** argv) {
    constexpr int device=701;
    try {
        if(argc>1)return maintenance(argc,argv);
        CryptRuntime runtime;
        Hpke hpke{};check(wc_HpkeInit(&hpke,DHKEM_P256_HKDF_SHA256,HKDF_SHA256,HPKE_AES_128_GCM,nullptr)==0);
        vectors(hpke);
        TestContainer container;container.deniedPrivateExport();
        PublicKey opaque(container.handle->publicKey());opaque.key.devId=device;
        OpaqueBridge bridge{container.handle,&opaque.key};
        check(wc_CryptoCb_RegisterDevice(device,OpaqueBridge::callback,&bridge)==0);
        Registration registration{device};
        std::cout<<encode(container.handle->publicKey())<<std::endl;
        PublicKey recipient(line());
        auto info=text("PeerChat evaluation RFC9180 Base"),aad=text("synthetic-account/device/message-v1"),plain=text("Synthetic HPKE test fixture only");
        Bytes firstEnc,firstCt;
        auto open=[&](const Bytes& enc,const Bytes& ct,const Bytes& associated)->int {
            if(enc.size()!=65||enc[0]!=4||ct.size()!=plain.size()+16)return BAD_FUNC_ARG;
            Bytes out(plain.size());
            // wolfCrypt ctSz excludes the appended GCM tag, unlike BC's API.
            // The full input buffer still contains that tag at ct + ctSz.
            int result=wc_HpkeOpenBase(&hpke,&opaque.key,enc.data(),word16(enc.size()),info.data(),word32(info.size()),const_cast<byte*>(associated.data()),word32(associated.size()),const_cast<byte*>(ct.data()),word32(ct.size()-16),out.data());
            if(result==0)check(out==plain);
            wipe(out);return result;
        };
        for(int i=0;i<200;++i){auto enc=line(),ct=line();check(open(enc,ct,aad)==0);if(i==0){firstEnc=enc;firstCt=ct;}std::cout<<"PASS"<<std::endl;}
        auto bad=firstCt;bad[0]^=1;check(open(firstEnc,bad,aad)!=0);
        auto wrongAad=aad;wrongAad[0]^=1;check(open(firstEnc,firstCt,wrongAad)!=0);
        unsigned before=bridge.agreements;
        for(auto point:std::vector<Bytes>{Bytes{0},Bytes(65),Bytes(33,2),Bytes(97,4),Bytes(65,255)})check(open(point,firstCt,aad)!=0);
        Bytes offCurve(65);offCurve[0]=4;check(open(offCurve,firstCt,aad)!=0);check(bridge.agreements==before);
        Bytes outOfField(65,255);outOfField[0]=4;check(open(outOfField,firstCt,aad)!=0);check(bridge.agreements==before);
        {
            TestContainer wrongDevice;
            bridge.handle=wrongDevice.handle;check(open(firstEnc,firstCt,aad)!=0);
            bridge.handle=container.handle;
        }
        bridge.fail=true;check(open(firstEnc,firstCt,aad)!=0);bridge.fail=false;
        wc_CryptoCb_UnRegisterDevice(device);check(open(firstEnc,firstCt,aad)!=0);
        check(wc_CryptoCb_RegisterDevice(device,OpaqueBridge::callback,&bridge)==0);check(open(firstEnc,firstCt,aad)==0);
        WC_RNG rng{};check(wc_InitRng(&rng)==0);
        for(int i=0;i<200;++i){void* ephemeral=nullptr;check(wc_HpkeGenerateKeyPair(&hpke,&ephemeral,&rng)==0);
            Bytes enc(65),ct(plain.size()+16);word16 length=65;
            check(wc_HpkeSerializePublicKey(&hpke,ephemeral,enc.data(),&length)==0&&length==65);
            int result=wc_HpkeSealBase(&hpke,ephemeral,&recipient.key,info.data(),word32(info.size()),aad.data(),word32(aad.size()),plain.data(),word32(plain.size()),ct.data());
            wc_HpkeFreeKey(&hpke,DHKEM_P256_HKDF_SHA256,ephemeral,nullptr);check(result==0);
            std::cout<<encode(enc)<<'\n'<<encode(ct)<<std::endl;check(line()==Bytes{1});}
        wc_FreeRng(&rng);wc_CryptoCb_UnRegisterDevice(device);wipe(plain);
        std::cout<<"PASS: RFC vectors; CNG private export denied; 100 each direction with stock BC and patched host opaque oracle; wrong device/tamper/AAD/point/provider/no-fallback rejection"<<std::endl;
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
