#ifndef PEERCHAT_HPKE_EVALUATION
#error wolfCrypt shipping license unresolved: this provider requires PEERCHAT_HPKE_EVALUATION
#endif
#include "hpke_provider.hpp"
#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/hpke.h>
#include <wolfssl/wolfcrypt/cryptocb.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#include <wolfssl/wolfcrypt/wc_port.h>
#include <algorithm>
#include <mutex>

// A binary marker as well as a sidecar: copying only the DLL must not evade the
// updater packaging gate. This status is not a wire/protocol field.
extern "C" __declspec(dllexport) const char* peerchat_hpke_evaluation_status() {
    return "PEERCHAT_HPKE_EVALUATION_UNLICENSED";
}

namespace peerchat::platform {
namespace {
using Bytes=HpkeProvider::Bytes;
constexpr std::size_t maxBody=1024*1024, maxMetadata=8192;
constexpr int callbackDevice=1707;
// wolfCrypt registry/initialization are global. ALL use of this isolated static
// dependency is serialized here. No callback can overlap registration/cleanup;
// no routing/session scheduler is introduced. Every operation owns its context.
std::mutex execution;
void require(bool ok){if(!ok)throw std::runtime_error("HPKE operation rejected");}
void clear(Bytes& bytes) noexcept {volatile unsigned char* p=bytes.data();for(std::size_t i=0;i<bytes.size();++i)p[i]=0;}
struct ProtectedBytes { Bytes value;~ProtectedBytes(){clear(value);} };
struct Runtime {
    Runtime(){require(wolfCrypt_Init()==0);}
    ~Runtime(){wolfCrypt_Cleanup();}
};
struct Rng {
    WC_RNG value{};
    Rng(){require(wc_InitRng(&value)==0);}
    ~Rng(){wc_FreeRng(&value);}
};
struct PublicKey {
    ecc_key value{};
    explicit PublicKey(const Bytes& point){
        require(wc_ecc_init(&value)==0);
        if(point.size()!=65||point[0]!=4||
           wc_ecc_import_x963_ex(point.data(),word32(point.size()),&value,ECC_SECP256R1)!=0||wc_ecc_check_key(&value)!=0){
            wc_ecc_free(&value);throw std::runtime_error("HPKE public point rejected");
        }
    }
    ~PublicKey(){wc_ecc_free(&value);}
};
struct Ephemeral {
    Hpke& hpke;void* value=nullptr;
    Ephemeral(Hpke& h,WC_RNG& rng):hpke(h){require(wc_HpkeGenerateKeyPair(&hpke,&value,&rng)==0);}
    ~Ephemeral(){if(value)wc_HpkeFreeKey(&hpke,HpkeSuite::kem,value,nullptr);}
};
struct Callback {
    std::shared_ptr<RecipientKeyHandle> handle;ecc_key* expected;
    static int run(int id,wc_CryptoInfo* info,void* context) noexcept {
        if(!context||!info)return WC_HW_E;
        auto& self=*static_cast<Callback*>(context);
        if(id!=callbackDevice||!self.handle||info->algo_type!=WC_ALGO_TYPE_PK||info->pk.type!=WC_PK_TYPE_ECDH)return WC_HW_E;
        auto& r=info->pk.ecdh;
        if(r.private_key!=self.expected||!r.public_key||!r.out||!r.outlen||*r.outlen<32||
           !r.public_key->dp||r.public_key->dp->id!=ECC_SECP256R1||
           r.public_key->devId!=INVALID_DEVID||wc_ecc_check_key(r.public_key)!=0)return WC_HW_E;
        try {
            Bytes point(65);word32 length=65;
            if(wc_ecc_export_x963(r.public_key,point.data(),&length)!=0||length!=65||point[0]!=4)return WC_HW_E;
            ProtectedBytes dh{self.handle->agreeP256(point)};
            if(dh.value.size()!=32)return WC_HW_E;
            std::copy(dh.value.begin(),dh.value.end(),r.out);*r.outlen=32;return 0;
        }catch(...){return WC_HW_E;} // NEVER request software fallback.
    }
};
struct Registration {
    explicit Registration(Callback& callback){require(wc_CryptoCb_RegisterDevice(callbackDevice,Callback::run,&callback)==0);}
    ~Registration(){wc_CryptoCb_UnRegisterDevice(callbackDevice);}
};
class Provider final : public HpkeProvider {
    std::atomic<bool> closing_{false};
    void usable(const std::atomic<bool>* cancellation)const{require(!closing_.load()&&(!cancellation||!cancellation->load()));}
    static Hpke suite(){Hpke h{};require(wc_HpkeInit(&h,HpkeSuite::kem,HpkeSuite::kdf,HpkeSuite::aead,nullptr)==0);return h;}
    static void metadata(const Bytes& info,const Bytes& aad){require(info.size()<=maxMetadata&&aad.size()<=maxMetadata);}
public:
    HpkeEnvelope seal(const Bytes& recipientPublic,const Bytes& info,const Bytes& aad,const Bytes& plaintext,const std::atomic<bool>* cancellation)override{
        usable(cancellation);metadata(info,aad);require(plaintext.size()<=maxBody);
        std::lock_guard<std::mutex> lock(execution);usable(cancellation);
        Runtime runtime;auto hpke=suite();PublicKey recipient(recipientPublic);Rng rng;Ephemeral ephemeral(hpke,rng.value);
        HpkeEnvelope out;out.enc.resize(65);out.ciphertext.resize(plaintext.size()+16);word16 encLength=65;
        require(wc_HpkeSerializePublicKey(&hpke,ephemeral.value,out.enc.data(),&encLength)==0&&encLength==65);
        // These upstream arguments are read-only despite their legacy mutable types.
        byte empty=0;auto* input=plaintext.empty()?&empty:const_cast<byte*>(plaintext.data());
        require(wc_HpkeSealBase(&hpke,ephemeral.value,&recipient.value,const_cast<byte*>(info.data()),word32(info.size()),
            const_cast<byte*>(aad.data()),word32(aad.size()),input,word32(plaintext.size()),out.ciphertext.data())==0);
        usable(cancellation);return out;
    }
    Bytes open(const std::shared_ptr<RecipientKeyHandle>& recipient,const Bytes& expectedPublic,const Bytes& info,const Bytes& aad,const HpkeEnvelope& envelope,const std::atomic<bool>* cancellation)override{
        usable(cancellation);metadata(info,aad);
        require(recipient&&envelope.ciphertext.size()>=16&&envelope.ciphertext.size()<=maxBody+16);
        std::lock_guard<std::mutex> lock(execution);usable(cancellation);
        Runtime runtime;auto hpke=suite();PublicKey remote(envelope.enc),key(expectedPublic);
        require(recipient->publicKey()==expectedPublic);key.value.devId=callbackDevice;
        Callback callback{recipient,&key.value};Registration registration(callback);
        // Allocate at least one byte for the upstream zero-length API case.
        ProtectedBytes plain{Bytes(std::max<std::size_t>(1,envelope.ciphertext.size()-16))};
        require(wc_HpkeOpenBase(&hpke,&key.value,envelope.enc.data(),word16(envelope.enc.size()),
            const_cast<byte*>(info.data()),word32(info.size()),const_cast<byte*>(aad.data()),word32(aad.size()),
            const_cast<byte*>(envelope.ciphertext.data()),word32(envelope.ciphertext.size()-16),plain.value.data())==0);
        usable(cancellation);plain.value.resize(envelope.ciphertext.size()-16);return std::move(plain.value);
    }
    void shutdown()noexcept override{closing_.store(true);std::lock_guard<std::mutex> lock(execution);}
    ~Provider(){shutdown();}
};
}
std::shared_ptr<HpkeProvider> createWolfCryptEvaluationHpke(){return std::make_shared<Provider>();}
}
