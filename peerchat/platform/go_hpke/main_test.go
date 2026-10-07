package main

import (
	"bytes"
	"crypto/ecdh"
	"crypto/hpke"
	"crypto/rand"
	"testing"
)

// Software-key baseline intentionally separate from the CNG/ABI tests.
func TestStockSoftwareBaseline(t *testing.T) {
	k, err := ecdh.P256().GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	private, err := hpke.NewDHKEMPrivateKey(k)
	if err != nil {
		t.Fatal(err)
	}
	public, err := hpke.NewDHKEMPublicKey(k.PublicKey())
	if err != nil {
		t.Fatal(err)
	}
	info, aad, plain := []byte("info"), []byte("AAD"), []byte("synthetic baseline")
	for range 100 {
		enc, s, err := hpke.NewSender(public, hpke.HKDFSHA256(), hpke.AES128GCM(), info)
		if err != nil {
			t.Fatal(err)
		}
		ct, err := s.Seal(aad, plain)
		if err != nil {
			t.Fatal(err)
		}
		r, err := hpke.NewRecipient(enc, private, hpke.HKDFSHA256(), hpke.AES128GCM(), info)
		if err != nil {
			t.Fatal(err)
		}
		got, err := r.Open(aad, ct)
		if err != nil || !bytes.Equal(got, plain) {
			t.Fatal("software baseline failed")
		}
	}
}

type softwareOpaque struct{ key *ecdh.PrivateKey }

func (k softwareOpaque) PublicKey() *ecdh.PublicKey             { return k.key.PublicKey() }
func (k softwareOpaque) Curve() ecdh.Curve                      { return ecdh.P256() }
func (k softwareOpaque) ECDH(p *ecdh.PublicKey) ([]byte, error) { return k.key.ECDH(p) }
func TestOpaqueKeyCannotSerialize(t *testing.T) {
	k, e := ecdh.P256().GenerateKey(rand.Reader)
	if e != nil {
		t.Fatal(e)
	}
	private, e := hpke.NewDHKEMPrivateKey(softwareOpaque{k})
	if e != nil {
		t.Fatal(e)
	}
	if raw, e := private.Bytes(); e == nil || len(raw) != 0 {
		t.Fatal("opaque serialization unexpectedly succeeded")
	}
}
