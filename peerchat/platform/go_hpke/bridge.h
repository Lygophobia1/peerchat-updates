#ifndef PEERCHAT_GO_HPKE_BRIDGE_H
#define PEERCHAT_GO_HPKE_BRIDGE_H
#include <stdint.h>
#include <stddef.h>
/* Synchronous operation-scoped authorized capability; never retained by Go.
   Callback validates local binding/authorization and returns 32 big-endian DH
   bytes. No private-key representation crosses this ABI. */
typedef int (*pc_dh_fn)(void*, const unsigned char*, size_t,
                        const unsigned char*, size_t, unsigned char*, size_t);
static inline int pc_dh_call(pc_dh_fn f, void* cap,
    const unsigned char* local, size_t local_n,
    const unsigned char* remote, size_t remote_n, unsigned char* dh, size_t n) {
    return f(cap, local, local_n, remote, remote_n, dh, n);
}
/* Codes: 0 success, 1 invalid input, 2 crypto/provider rejection,
   3 insufficient output capacity, 4 contained Go panic. Lengths are zero on
   failure; valid output buffers are cleared. Buffers must not overlap and must
   remain valid for the synchronous call. */
#endif
