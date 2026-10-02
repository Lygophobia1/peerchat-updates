#include "peerchat_core.h"
#include "../session_transport_manager.hpp"
#include "../group.hpp"
#include "../delivery_manager.hpp"
#include "../protocol.hpp"
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>

namespace {
struct Instance {
    // Existing owner, not a mobile session implementation. No profile/device is
    // authenticated and no route can be opened by this foundation ABI.
    peerchat::session::SessionTransportManager sessions{"portability-proof","",{1}};
    std::mutex gate;
};
struct Registry {
    std::mutex gate;uint64_t next=1;
    std::map<pc_core_handle,std::shared_ptr<Instance>> instances;
};
Registry& registry(){static Registry value;return value;}
std::shared_ptr<Instance> lookup(pc_core_handle handle){auto& r=registry();std::lock_guard<std::mutex> lock(r.gate);auto it=r.instances.find(handle);return it==r.instances.end()?nullptr:it->second;}
template<class F> pc_core_status guarded(F operation) noexcept {
    try{return operation();}catch(const std::bad_alloc&){return PC_CORE_OUT_OF_MEMORY;}catch(...){return PC_CORE_INTERNAL_ERROR;}
}
}
extern "C" {
pc_core_status pc_core_create(pc_core_handle* result){return guarded([&]()->pc_core_status{
    if(!result)return PC_CORE_INVALID_ARGUMENT;
    *result=0;auto instance=std::make_shared<Instance>();auto& r=registry();std::lock_guard<std::mutex> lock(r.gate);
    if(r.next==std::numeric_limits<uint64_t>::max())return PC_CORE_INTERNAL_ERROR;
    auto id=r.next++;r.instances.emplace(id,std::move(instance));*result=id;return PC_CORE_OK;
});}
pc_core_status pc_core_destroy(pc_core_handle handle){return guarded([&]()->pc_core_status{
    auto& r=registry();std::lock_guard<std::mutex> lock(r.gate);return r.instances.erase(handle)?PC_CORE_OK:PC_CORE_INVALID_HANDLE;
});}
pc_core_status pc_core_get_info(pc_core_handle handle,pc_core_info* info){return guarded([&]()->pc_core_status{
    if(!info||info->struct_size!=sizeof(pc_core_info))return PC_CORE_INVALID_ARGUMENT;
    auto instance=lookup(handle);if(!instance)return PC_CORE_INVALID_HANDLE;
    std::lock_guard<std::mutex> lock(instance->gate);*info={sizeof(pc_core_info),PC_CORE_ABI_VERSION,1,0,0,0};return PC_CORE_OK;
});}
pc_core_status pc_core_exchange_utf8(pc_core_handle handle,const uint8_t* input,uint32_t length,uint8_t* output,uint32_t capacity,uint32_t* required){return guarded([&]()->pc_core_status{
    if(!required)return PC_CORE_INVALID_ARGUMENT;
    *required=0;if((!input&&length)||(!output&&capacity)||length>PC_CORE_MAX_EXCHANGE_BYTES)return PC_CORE_INVALID_ARGUMENT;
    auto instance=lookup(handle);if(!instance)return PC_CORE_INVALID_HANDLE;
    std::lock_guard<std::mutex> lock(instance->gate);
    std::string text;if(length)text.assign(reinterpret_cast<const char*>(input),length);
    try{(void)peerchat::unicode::decode(text);}catch(const std::invalid_argument&){return PC_CORE_INVALID_UTF8;}
    peerchat::protocol::Writer wire(peerchat::protocol::Text);wire.string(text);
    peerchat::protocol::Reader reader(wire.data);auto copy=reader.string();reader.end();
    *required=static_cast<uint32_t>(copy.size());if(capacity<copy.size())return PC_CORE_BUFFER_TOO_SMALL;
    if(!copy.empty())std::memmove(output,copy.data(),copy.size());
    return PC_CORE_OK;
});}
pc_core_status pc_core_open_profile(pc_core_handle handle,const uint8_t* path,uint32_t length){return guarded([&]()->pc_core_status{
    if(!path||!length||length>32768)return PC_CORE_INVALID_ARGUMENT;
    if(!lookup(handle))return PC_CORE_INVALID_HANDLE;
    return PC_CORE_PLATFORM_UNAVAILABLE;
});}
}
