#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace peerchat::identity_format {
inline constexpr size_t PublicKeyBytes=72, SignatureBytes=64;
// BCRYPT_ECDSA_PUBLIC_P256_MAGIC and cbKey=32, little-endian header.
// Coordinates and signature r/s are fixed-width, big-endian. The Peer ID
// provider hashes ALL 72 bytes; never hash SPKI DER as a replacement.
inline constexpr std::array<uint8_t,8> PublicHeader{{0x45,0x43,0x53,0x31,0x20,0,0,0}};
inline std::array<uint8_t,PublicKeyBytes> publicKey(const uint8_t* x,const uint8_t* y){
    if(!x||!y)throw std::invalid_argument("Missing P-256 coordinates");
    std::array<uint8_t,PublicKeyBytes> out{};std::memcpy(out.data(),PublicHeader.data(),8);std::memcpy(out.data()+8,x,32);std::memcpy(out.data()+40,y,32);return out;
}
inline bool validPublicRepresentation(const uint8_t* bytes,size_t count) noexcept {
    return bytes&&count==PublicKeyBytes&&std::memcmp(bytes,PublicHeader.data(),8)==0;
}
// Providers must additionally validate curve points and ECDSA ranges. This
// routine only normalizes representation; it does not implement cryptography.
inline std::array<uint8_t,SignatureBytes> signature(const uint8_t* r,const uint8_t* s){
    if(!r||!s)throw std::invalid_argument("Missing signature scalars");
    std::array<uint8_t,SignatureBytes> out{};std::memcpy(out.data(),r,32);std::memcpy(out.data()+32,s,32);return out;
}
}
