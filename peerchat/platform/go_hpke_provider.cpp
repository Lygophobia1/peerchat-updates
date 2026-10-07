// Go runtime is loaded once from the containing module directory and never unloaded.
#include "hpke_provider.hpp"
#include "go_hpke/abi.h"
#include <windows.h>
#include <mutex>
#include <condition_variable>
#include <algorithm>

namespace peerchat::platform {
namespace {
using namespace peerchat::platform;
using Bytes=HpkeProvider::Bytes;
inline void require(bool ok){if(!ok)throw std::runtime_error("Go HPKE rejected operation");}
struct Library {
    HMODULE module;
    decltype(&pc_go_hpke_seal) seal;
    decltype(&pc_go_hpke_open) open;
    decltype(&pc_go_hpke_ping) ping;
    explicit Library(const std::wstring& absolutePath) {
        module=LoadLibraryExW(absolutePath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        require(module!=nullptr);
        seal=reinterpret_cast<decltype(seal)>(GetProcAddress(module,"pc_go_hpke_seal"));
        open=reinterpret_cast<decltype(open)>(GetProcAddress(module,"pc_go_hpke_open"));
        ping=reinterpret_cast<decltype(ping)>(GetProcAddress(module,"pc_go_hpke_ping"));
        require(seal&&open&&ping&&ping()==12608);
    }
    // Intentionally NO FreeLibrary: embedded Go runtime lives until process exit.
};
struct Capability {
    std::shared_ptr<RecipientKeyHandle> handle;
    Bytes expected;
    const std::atomic<bool>* cancelled;
    // This call-scoped capability wraps a caller-authorized RecipientKeyHandle.
    // No alias opening/registry/lifecycle authority exists here.
    static int agree(void* context,const unsigned char* local,size_t ln,
                     const unsigned char* remote,size_t rn,unsigned char* out,size_t n) noexcept {
        if(!out||n!=32)return 1;
        SecureZeroMemory(out,n);
        if(!context||!local||!remote||ln!=65||rn!=65)return 1;
        auto& cap=*static_cast<Capability*>(context);
        try {
            require(cap.handle&&(!cap.cancelled||!cap.cancelled->load()));
            require(cap.expected==Bytes(local,local+ln)&&cap.handle->publicKey()==cap.expected);
            auto dh=cap.handle->agreeP256(Bytes(remote,remote+rn));
            if(dh.size()!=32){if(!dh.empty())SecureZeroMemory(dh.data(),dh.size());return 1;}
            std::copy(dh.begin(),dh.end(),out);SecureZeroMemory(dh.data(),dh.size());return 0;
        }catch(...){return 1;}
    }
};
class Provider final:public HpkeProvider {
    Library& dll;std::mutex mutex;std::condition_variable idle;size_t active=0;bool closing=false;
    struct Operation {
        Provider& owner;
        explicit Operation(Provider& p):owner(p){std::lock_guard<std::mutex> lock(p.mutex);require(!p.closing);++p.active;}
        ~Operation(){std::lock_guard<std::mutex> lock(owner.mutex);if(--owner.active==0)owner.idle.notify_all();}
    };
    static unsigned char* ptr(const Bytes& b){return const_cast<unsigned char*>(b.data());}
    static void admission(const std::atomic<bool>* c){require(!c||!c->load());}
public:
    explicit Provider(Library& lib):dll(lib){}
    ~Provider(){shutdown();}
    HpkeEnvelope seal(const Bytes& pub,const Bytes& info,const Bytes& aad,const Bytes& plain,
                      const std::atomic<bool>* c=nullptr) override {
        Operation op(*this);admission(c);require(plain.size()<=65536);
        HpkeEnvelope e;e.enc.resize(65);e.ciphertext.resize(plain.size()+16);size_t en=0,cn=0;
        require(dll.seal(ptr(pub),pub.size(),ptr(info),info.size(),ptr(aad),aad.size(),ptr(plain),plain.size(),
                        e.enc.data(),e.enc.size(),&en,e.ciphertext.data(),e.ciphertext.size(),&cn)==0);
        require(en==65&&cn==plain.size()+16);admission(c);return e;
    }
    Bytes open(const std::shared_ptr<RecipientKeyHandle>& handle,const Bytes& pub,const Bytes& info,
               const Bytes& aad,const HpkeEnvelope& e,const std::atomic<bool>* c=nullptr) override {
        Operation op(*this);admission(c);
        require(handle&&handle->publicKey()==pub&&e.ciphertext.size()>=16&&e.ciphertext.size()<=65552);
        Capability cap{handle,pub,c};Bytes out(e.ciphertext.size()-16,0xa5);size_t n=0;
        int rc=dll.open(&Capability::agree,&cap,ptr(pub),pub.size(),ptr(info),info.size(),ptr(aad),aad.size(),
                        ptr(e.enc),e.enc.size(),ptr(e.ciphertext),e.ciphertext.size(),out.data(),out.size(),&n);
        if(rc!=0||n!=out.size()||(c&&c->load())){SecureZeroMemory(out.data(),out.size());throw std::runtime_error("Go HPKE open failed");}
        return out;
    }
    void shutdown() noexcept override {std::unique_lock<std::mutex> lock(mutex);closing=true;idle.wait(lock,[&]{return active==0;});}
};
}

std::shared_ptr<HpkeProvider> createShippingHpke() {
    // Function-local initialization serializes loading only, never HPKE execution.
    static Library library([]{
        HMODULE owner=nullptr;
        require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&createShippingHpke),&owner)!=0);
        std::wstring path(32768,L'\0');
        DWORD n=GetModuleFileNameW(owner,path.data(),static_cast<DWORD>(path.size()));
        require(n>0&&n<path.size());path.resize(n);
        auto separator=path.find_last_of(L"\\/");require(separator!=std::wstring::npos);
        return path.substr(0,separator+1)+L"PeerChat.Hpke.dll";
    }());
    return std::make_shared<Provider>(library);
}
}
