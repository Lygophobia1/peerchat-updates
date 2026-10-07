#include "../platform/hpke_provider.hpp"
#include "../platform/windows_recipient_key.hpp"
#include "../mailbox_protocol.hpp"
#include "../platform/go_hpke/abi.h"
#include <future>
#include <iostream>
#include <thread>
#include <sstream>
#include <iomanip>

using namespace peerchat::platform;
using Bytes=HpkeProvider::Bytes;
static void checked(bool ok,unsigned line){if(!ok)throw std::runtime_error("HPKE provider regression failed at line "+std::to_string(line));}
#define check(ok) checked((ok),__LINE__)
template<class F> static void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected);}
struct TestKey {
    std::string alias;std::shared_ptr<RecipientKeyHandle> key;bool owned=false;
    explicit TestKey(unsigned suffix){
        alias="PeerChat.Recipient.test.provider."+std::to_string(GetCurrentProcessId())+"."+std::to_string(GetTickCount64())+"."+std::to_string(suffix);
        WindowsRecipientKeyProvider provider;key=provider.create(alias);owned=true;
    }
    ~TestKey(){key.reset();if(!owned)return;recipient_cng::Object p,k;
        if(NCryptOpenStorageProvider(&p.value,MS_KEY_STORAGE_PROVIDER,0)!=0)return;
        auto name=recipient_cng::alias(alias);
        if(NCryptOpenKey(p.value,&k.value,name.c_str(),0,0)==0&&NCryptDeleteKey(k.value,0)==0)k.value=0;
    }
};
struct FailingKey : RecipientKeyHandle {
    Bytes point;explicit FailingKey(Bytes p):point(std::move(p)){}
    Bytes publicKey()override{return point;}
    Bytes agreeP256(const Bytes&)override{throw std::runtime_error("synthetic provider failure");}
};
struct GatedKey : RecipientKeyHandle {
    std::shared_ptr<RecipientKeyHandle> key;
    std::promise<void> entered;std::shared_future<void> released;
    GatedKey(std::shared_ptr<RecipientKeyHandle> k,std::shared_future<void> r):key(std::move(k)),released(r){}
    Bytes publicKey()override{return key->publicKey();}
    Bytes agreeP256(const Bytes& p)override{entered.set_value();released.wait();return key->agreeP256(p);}
};
static Bytes readHex(){
    std::string input;check(bool(std::getline(std::cin,input))&&input.size()%2==0&&input.size()<=140000);Bytes result;
    for(std::size_t i=0;i<input.size();i+=2){auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
        int a=digit(input[i]),b=digit(input[i+1]);check(a>=0&&b>=0);result.push_back(static_cast<unsigned char>(16*a+b));}return result;
}
static std::string hex(const Bytes& value){std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto c:value)out<<std::setw(2)<<int(c);return out.str();}
static Bytes text(const char* value){return Bytes(value,value+std::char_traits<char>::length(value));}
static int interoperability(){
    TestKey key(9);auto provider=createShippingHpke();auto publicKey=key.key->publicKey();
    peerchat::mailbox::Header header;
    header.senderAccount=1;header.recipientAccount=2;header.senderDevice=std::string(64,'a');header.recipientDevice=std::string(64,'b');
    header.keyId=std::string(32,'c');header.messageId=std::string(32,'d');header.keyGeneration=1;header.createdUtc=1000;header.expiresUtc=2000;header.logicalOrder=1;header.storageAllowed=true;
    auto info=peerchat::mailbox::info(),aad=peerchat::mailbox::headerPayload(header),plain=peerchat::mailbox::textBody(text("Synthetic HPKE test fixture only"),1000);
    std::cout<<hex(publicKey)<<'\n'<<hex(info)<<'\n'<<hex(aad)<<'\n'<<hex(plain)<<std::endl;auto remote=readHex();
    for(unsigned i=0;i<100;++i){HpkeEnvelope envelope;envelope.enc=readHex();envelope.ciphertext=readHex();check(provider->open(key.key,publicKey,info,aad,envelope)==plain);std::cout<<"PASS"<<std::endl;}
    for(unsigned i=0;i<100;++i){auto envelope=provider->seal(remote,info,aad,plain);std::cout<<hex(envelope.enc)<<'\n'<<hex(envelope.ciphertext)<<std::endl;check(readHex()==Bytes{1});}
    std::cout<<"PASS: hardened provider interoperates both directions with unchanged Android BC adapter (100 each direction); physical Android pending"<<std::endl;return 0;
}
int main(int argc,char** argv){try{
    if(argc==2&&std::string(argv[1])=="--interop")return interoperability();

    TestKey first(1),second(2);auto provider=createShippingHpke();
    Bytes info{1,0,2},aad{3,4},plain{5,0,6,7};
    auto envelope=provider->seal(first.key->publicKey(),info,aad,plain);
    // Exercise the public C boundary directly, including stale-output rejection.
    auto module=GetModuleHandleW(L"PeerChat.Hpke.dll");check(module!=nullptr);
    auto rawOpen=reinterpret_cast<decltype(&pc_go_hpke_open)>(GetProcAddress(module,"pc_go_hpke_open"));check(rawOpen!=nullptr);
    auto agreement=+[](void* cap,const unsigned char*,size_t,const unsigned char* point,size_t size,unsigned char* out,size_t capacity)->int {
        try{if(!cap||capacity!=32)return 1;auto key=static_cast<RecipientKeyHandle*>(cap);auto dh=key->agreeP256(Bytes(point,point+size));
            if(dh.size()!=32)return 1;
            std::copy(dh.begin(),dh.end(),out);SecureZeroMemory(dh.data(),dh.size());return 0;
        }catch(...){return 1;}
    };
    auto publicPoint=first.key->publicKey();Bytes output(plain.size(),0xa5);size_t outputLength=99;
    auto raw=[&](pc_dh_fn fn,void* cap,Bytes ciphertext,size_t capacity){
        std::fill(output.begin(),output.end(),0xa5);outputLength=99;
        return rawOpen(fn,cap,publicPoint.data(),publicPoint.size(),info.data(),info.size(),aad.data(),aad.size(),envelope.enc.data(),envelope.enc.size(),ciphertext.data(),ciphertext.size(),output.data(),capacity,&outputLength);
    };
    check(raw(agreement,first.key.get(),envelope.ciphertext,output.size())==0&&outputLength==plain.size()&&output==plain);
    auto altered=envelope.ciphertext;altered.back()^=1;
    check(raw(agreement,first.key.get(),altered,output.size())==2&&outputLength==0&&output==Bytes(plain.size()));
    check(raw(nullptr,first.key.get(),envelope.ciphertext,output.size())==1&&outputLength==0&&output==Bytes(plain.size()));
    check(raw(agreement,nullptr,envelope.ciphertext,output.size())==1&&outputLength==0&&output==Bytes(plain.size()));
    check(raw(agreement,first.key.get(),envelope.ciphertext,output.size()-1)==3&&outputLength==0&&output[0]==0);
    auto empty=provider->seal(first.key->publicKey(),info,aad,{});check(provider->open(first.key,first.key->publicKey(),info,aad,empty).empty());
    check(provider->open(first.key,first.key->publicKey(),info,aad,envelope)==plain);
    rejects([&]{provider->open(second.key,second.key->publicKey(),info,aad,envelope);});
    rejects([&]{provider->open(second.key,first.key->publicKey(),info,aad,envelope);});
    rejects([&]{provider->open(first.key,first.key->publicKey(),Bytes{9},aad,envelope);});
    rejects([&]{provider->open(first.key,first.key->publicKey(),info,Bytes{9},envelope);});
    auto bad=envelope;bad.ciphertext[0]^=1;rejects([&]{provider->open(first.key,first.key->publicKey(),info,aad,bad);});
    bad=envelope;bad.ciphertext.resize(15);rejects([&]{provider->open(first.key,first.key->publicKey(),info,aad,bad);});
    bad=envelope;bad.enc=Bytes(65);bad.enc[0]=4;rejects([&]{provider->open(first.key,first.key->publicKey(),info,aad,bad);});
    auto failing=std::make_shared<FailingKey>(first.key->publicKey());rejects([&]{provider->open(failing,first.key->publicKey(),info,aad,envelope);});
    std::atomic<bool> cancelled{true};rejects([&]{provider->seal(first.key->publicKey(),info,aad,plain,&cancelled);});
    rejects([&]{provider->open(first.key,first.key->publicKey(),info,aad,envelope,&cancelled);});
    rejects([&]{provider->seal(first.key->publicKey(),Bytes(8193),aad,plain);});
    rejects([&]{provider->seal(first.key->publicKey(),info,aad,Bytes(1024*1024+1));});
    // Warm the embedded runtime and its worker pools before measuring growth.
    for(unsigned i=0;i<1000;++i){auto e=provider->seal(first.key->publicKey(),info,aad,plain);check(provider->open(first.key,first.key->publicKey(),info,aad,e)==plain);}
    DWORD before=0,after=0;
    // Match the measured concurrent workload during warmup: Go initializes
    // process-lifetime worker resources lazily for concurrent cgo callers.
    for(unsigned round=0;round<12;++round){
    check(GetProcessHandleCount(GetCurrentProcess(),&before));
    std::atomic<unsigned> completed{0};std::vector<std::future<void>> tasks;
    for(unsigned t=0;t<8;++t)tasks.push_back(std::async(std::launch::async,[&,t]{
        auto instance=createShippingHpke();auto key=t%2?first.key:second.key;auto publicKey=key->publicKey();
        for(unsigned i=0;i<125;++i){auto value=plain;value.push_back(static_cast<unsigned char>(t));
            auto sealed=instance->seal(publicKey,info,aad,value);check(instance->open(key,publicKey,info,aad,sealed)==value);++completed;
            if(i%3==0){sealed.ciphertext.back()^=1;rejects([&]{instance->open(key,publicKey,info,aad,sealed);});}
            if(i%3==1)rejects([&]{instance->open(t%2?second.key:first.key,(t%2?second.key:first.key)->publicKey(),info,aad,sealed);});
        }
        instance->shutdown();rejects([&]{instance->seal(publicKey,info,aad,plain);});
    }));
    for(auto& task:tasks)task.get();
    check(completed==1000);
    check(GetProcessHandleCount(GetCurrentProcess(),&after));
    std::cout<<"METRIC concurrent_round="<<round<<" handles_before="<<before<<" handles_after="<<after<<'\n';
    if(round>=9)check(after<=before+4);
    check(first.key.use_count()==1&&second.key.use_count()==1);
    }
    for(unsigned i=0;i<100;++i){auto instance=createShippingHpke();auto sealed=instance->seal(first.key->publicKey(),info,aad,plain);
        check(instance->open(first.key,first.key->publicKey(),info,aad,sealed)==plain);}
    auto stopping=createShippingHpke();std::promise<void> release;
    auto gate=std::make_shared<GatedKey>(first.key,release.get_future().share());
    auto started=gate->entered.get_future();
    auto operation=std::async(std::launch::async,[&]{check(stopping->open(gate,first.key->publicKey(),info,aad,envelope)==plain);});
    const bool began=started.wait_for(std::chrono::seconds(10))==std::future_status::ready;
    if(!began){release.set_value();operation.get();check(false);}
    auto stop=std::async(std::launch::async,[&]{stopping->shutdown();});
    // New calls fail as soon as shutdown begins, not only after DH completes.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto admission=std::async(std::launch::async,[&]{try{stopping->seal(first.key->publicKey(),info,aad,plain);return false;}catch(const std::exception&){return true;}});
    const bool prompt=admission.wait_for(std::chrono::seconds(2))==std::future_status::ready;
    release.set_value();operation.get();stop.get();check(prompt&&admission.get());
    rejects([&]{stopping->seal(first.key->publicKey(),info,aad,plain);});
    std::cout<<"PASS: 1000 concurrent round trips, two CNG keys/eight providers, repeated initialization, cancellation, shutdown during DH, bounds/tamper/provider failures, bounded process handles\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
