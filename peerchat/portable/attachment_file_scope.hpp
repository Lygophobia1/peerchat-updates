#pragma once
#include "../transfer_manager.hpp"

namespace peerchat::transfer::posix {
inline std::uint64_t maximum(Purpose purpose){protocol::check(purpose==Purpose::PeerFile||purpose==Purpose::DirectCacheCiphertext);return purpose==Purpose::PeerFile?MaxFileBytes:MaxCacheCipherBytes;}
// App-private cache tokens bind their local purpose. Legacy bytes stay exact.
inline void tokenPurpose(protocol::Writer& w,Purpose purpose){maximum(purpose);if(purpose!=Purpose::PeerFile){w.string("PeerChat/CacheFilePurpose/v1");w.number(static_cast<unsigned>(purpose));}}
inline void tokenPurpose(protocol::Reader& r,Purpose purpose){maximum(purpose);if(purpose!=Purpose::PeerFile)protocol::check(r.string()=="PeerChat/CacheFilePurpose/v1"&&r.number()==static_cast<unsigned>(purpose));r.end();}
}
