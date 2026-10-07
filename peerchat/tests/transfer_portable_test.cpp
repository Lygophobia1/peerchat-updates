#include "transfer_manager.hpp"
// Built with the Android ARM64 NDK, without Windows headers, filesystem APIs,
// CNG, sockets, JNI or a platform private-key dependency.
static_assert(peerchat::transfer::ChunkBytes==32768,"Wire chunk bound");
static_assert(peerchat::transfer::MaxTransfers==4,"Concurrent transfer bound");
void compilePortableProviderBoundary(){
    peerchat::transfer::TransferManager manager([]()->std::unique_ptr<peerchat::transfer::Hash>{return {};});
    auto snapshots=manager.snapshots();
    auto demand=manager.demandedPeers();
    manager.tick(1,[](const std::string&)->std::uint64_t{return 0;},[](const std::string&,const peerchat::Blob&){return false;});
}
