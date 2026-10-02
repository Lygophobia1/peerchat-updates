#ifndef PEERCHAT_PORTABLE_CORE_H
#define PEERCHAT_PORTABLE_CORE_H
#include <stdint.h>
#if defined(_WIN32)
#define PC_CORE_API __declspec(dllexport)
#else
#define PC_CORE_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef uint64_t pc_core_handle;
typedef enum pc_core_status {
    PC_CORE_OK=0, PC_CORE_INVALID_ARGUMENT=1, PC_CORE_INVALID_HANDLE=2,
    PC_CORE_BUFFER_TOO_SMALL=3, PC_CORE_INVALID_UTF8=4,
    PC_CORE_OUT_OF_MEMORY=5, PC_CORE_INTERNAL_ERROR=6,
    PC_CORE_PLATFORM_UNAVAILABLE=7
} pc_core_status;
enum { PC_CORE_ABI_VERSION=1, PC_CORE_MAX_EXCHANGE_BYTES=65536 };
typedef struct pc_core_info {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t session_policy_available;
    uint32_t identity_available;
    uint32_t storage_available;
    uint32_t peer_tls_available;
} pc_core_info;
// Callers must supply writable pointers/buffer extents as declared. Invalid
// handle IDs are rejected; arbitrary dangling caller pointers cannot be checked.
// Every function contains C++ exceptions. Handles are non-reused instance IDs,
// not addresses. Null/stale handles fail. Calls serialize per instance; destroy
// invalidates future calls while an already-admitted call may finish safely.
PC_CORE_API pc_core_status pc_core_create(pc_core_handle* result);
PC_CORE_API pc_core_status pc_core_destroy(pc_core_handle handle);
PC_CORE_API pc_core_status pc_core_get_info(pc_core_handle handle,pc_core_info* info);
// Standard UTF-8 -> existing PT1 string encoder/parser -> exact copy. No message
// transmission or authorization. Buffers belong to caller; no borrowed pointers.
// Size query: output=NULL, capacity=0. required excludes a terminating NUL.
// On BUFFER_TOO_SMALL no output is written. Invalid UTF-8 is rejected.
PC_CORE_API pc_core_status pc_core_exchange_utf8(pc_core_handle handle,
    const uint8_t* input,uint32_t length,uint8_t* output,uint32_t capacity,uint32_t* required);
// No insecure stand-ins: profile/identity/TLS/storage are not wired yet.
PC_CORE_API pc_core_status pc_core_open_profile(pc_core_handle handle,const uint8_t* path,uint32_t length);
#ifdef __cplusplus
}
#endif
#endif
