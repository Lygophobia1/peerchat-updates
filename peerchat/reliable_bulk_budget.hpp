#pragma once
#include "transfer_manager.hpp"

namespace peerchat::scheduling {
// Local ciphertext admission, not a wire window or bandwidth limit. At most
// this budget plus one already admitted frame can precede subsequent control.
// Never reorder encrypted TLS records to implement application priority.
constexpr size_t BulkOutputHighWater=128*1024;
inline bool payloadTransferFrame(const Blob& bytes){
    protocol::Reader r(bytes);protocol::check(r.type()==protocol::FileTransfer);
    const auto version=r.number(),kind=r.number();
    protocol::check((version==1||version==2)&&kind>=1&&kind<=7);
    return kind==static_cast<std::uint64_t>(transfer::Kind::Chunk);
}
inline bool admitsTransfer(const Blob& bytes,size_t queued){
    return !payloadTransferFrame(bytes)||queued<BulkOutputHighWater;
}
}
