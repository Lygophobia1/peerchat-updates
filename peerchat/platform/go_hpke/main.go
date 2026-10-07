// Production RFC 9180 provider. Permanent private keys remain in caller-authorized CNG capabilities.
package main

/*
#include "bridge.h"
*/
import "C"

import (
	"crypto/ecdh"
	"crypto/hpke"
	"errors"
	"runtime"
	"unsafe"
)

const maxBody = 65536
const maxMetadata = 8192

func view(p *C.uchar, n C.size_t, limit int) ([]byte, bool) {
	if uint64(n) > uint64(limit) || (n != 0 && p == nil) {
		return nil, false
	}
	if n == 0 {
		return nil, true
	}
	return unsafe.Slice((*byte)(unsafe.Pointer(p)), int(n)), true
}
func guard(result *C.int) {
	if recover() != nil {
		*result = 4
	}
}

type opaque struct {
	pub *ecdh.PublicKey
	cap unsafe.Pointer
	fn  C.pc_dh_fn
	dh  []byte
}

func (k *opaque) PublicKey() *ecdh.PublicKey { return k.pub }
func (k *opaque) Curve() ecdh.Curve          { return ecdh.P256() }
func (k *opaque) ECDH(remote *ecdh.PublicKey) ([]byte, error) {
	if remote.Curve() != ecdh.P256() {
		return nil, errors.New("curve rejected")
	}
	local, point := k.pub.Bytes(), remote.Bytes()
	dh := make([]byte, 32)
	r := C.pc_dh_call(k.fn, k.cap, (*C.uchar)(unsafe.Pointer(&local[0])), C.size_t(len(local)),
		(*C.uchar)(unsafe.Pointer(&point[0])), C.size_t(len(point)), (*C.uchar)(unsafe.Pointer(&dh[0])), 32)
	if r != 0 {
		clear(dh)
		return nil, errors.New("opaque agreement rejected")
	}
	k.dh = dh
	return dh, nil
}

//export pc_go_hpke_ping
func pc_go_hpke_ping() C.int { return 12608 }

//export pc_go_hpke_seal
func pc_go_hpke_seal(pub *C.uchar, pn C.size_t, info *C.uchar, in C.size_t,
	aad *C.uchar, an C.size_t, plain *C.uchar, n C.size_t,
	enc *C.uchar, ec C.size_t, en *C.size_t, ct *C.uchar, cc C.size_t, cn *C.size_t) (result C.int) {
	defer guard(&result)
	if en == nil || cn == nil {
		return 1
	}
	*en = 0
	*cn = 0
	eout, eok := view(enc, ec, 65)
	cout, cok := view(ct, cc, maxBody+16)
	if !eok || !cok {
		return 1
	}
	clear(eout)
	clear(cout)
	p, pok := view(pub, pn, 65)
	i, iok := view(info, in, maxMetadata)
	a, aok := view(aad, an, maxMetadata)
	b, bok := view(plain, n, maxBody)
	if !pok || !iok || !aok || !bok || len(p) != 65 {
		return 1
	}
	if len(eout) < 65 || len(cout) < len(b)+16 {
		return 3
	}
	k, err := hpke.DHKEM(ecdh.P256()).NewPublicKey(p)
	if err != nil {
		return 2
	}
	e, s, err := hpke.NewSender(k, hpke.HKDFSHA256(), hpke.AES128GCM(), i)
	if err != nil {
		return 2
	}
	c, err := s.Seal(a, b)
	if err != nil {
		return 2
	}
	copy(eout, e)
	copy(cout, c)
	*en = C.size_t(len(e))
	*cn = C.size_t(len(c))
	return 0
}

//export pc_go_hpke_open
func pc_go_hpke_open(fn C.pc_dh_fn, cap unsafe.Pointer, pub *C.uchar, pn C.size_t,
	info *C.uchar, in C.size_t, aad *C.uchar, an C.size_t,
	enc *C.uchar, en C.size_t, ct *C.uchar, cn C.size_t,
	plain *C.uchar, capacity C.size_t, n *C.size_t) (result C.int) {
	defer guard(&result)
	if n == nil {
		return 1
	}
	*n = 0
	out, ok := view(plain, capacity, maxBody)
	if !ok {
		return 1
	}
	clear(out)
	p, pok := view(pub, pn, 65)
	i, iok := view(info, in, maxMetadata)
	a, aok := view(aad, an, maxMetadata)
	e, eok := view(enc, en, 65)
	c, cok := view(ct, cn, maxBody+16)
	if fn == nil || cap == nil || !pok || !iok || !aok || !eok || !cok || len(p) != 65 || len(e) != 65 || len(c) < 16 {
		return 1
	}
	if len(out) < len(c)-16 {
		return 3
	}
	pubKey, err := ecdh.P256().NewPublicKey(p)
	if err != nil {
		return 2
	}
	k := &opaque{pub: pubKey, cap: cap, fn: fn}
	defer func() { clear(k.dh); k.dh = nil; k.cap = nil; k.fn = nil; runtime.KeepAlive(k) }()
	private, err := hpke.NewDHKEMPrivateKey(k)
	if err != nil {
		return 2
	}
	r, err := hpke.NewRecipient(e, private, hpke.HKDFSHA256(), hpke.AES128GCM(), i)
	if err != nil {
		return 2
	}
	b, err := r.Open(a, c)
	if err != nil {
		return 2
	}
	defer clear(b)
	copy(out, b)
	*n = C.size_t(len(b))
	return 0
}
func main() {}
