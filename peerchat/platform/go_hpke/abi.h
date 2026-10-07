#pragma once
#include "bridge.h"
// Matches cgo's generated declarations; pointers are borrowed synchronously.
extern "C" {
int pc_go_hpke_ping();
int pc_go_hpke_seal(unsigned char*,size_t,unsigned char*,size_t,unsigned char*,size_t,unsigned char*,size_t,unsigned char*,size_t,size_t*,unsigned char*,size_t,size_t*);
int pc_go_hpke_open(pc_dh_fn,void*,unsigned char*,size_t,unsigned char*,size_t,unsigned char*,size_t,unsigned char*,size_t,unsigned char*,size_t,unsigned char*,size_t,size_t*);
}
