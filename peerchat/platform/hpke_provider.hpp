#pragma once
#include "device_key_provider.hpp"
#include <atomic>
#include <cstdint>
#include <stdexcept>

namespace peerchat::platform {
struct HpkeSuite {
    static constexpr std::uint16_t kem=0x0010, kdf=0x0001, aead=0x0001;
};
struct HpkeEnvelope {
    std::vector<unsigned char> enc, ciphertext;
};
// Library-independent, one-shot RFC 9180 Base operations. Key authorization and
// operation leases belong to DeviceKeyManager, not this crypto adapter.
class HpkeProvider {
public:
    using Bytes=std::vector<unsigned char>;
    virtual ~HpkeProvider()=default;
    virtual HpkeEnvelope seal(const Bytes& recipientPublic,const Bytes& info,
                              const Bytes& aad,const Bytes& plaintext,
                              const std::atomic<bool>* cancellation=nullptr)=0;
    virtual Bytes open(const std::shared_ptr<RecipientKeyHandle>& recipient,
                       const Bytes& expectedPublic,const Bytes& info,const Bytes& aad,
                       const HpkeEnvelope& envelope,
                       const std::atomic<bool>* cancellation=nullptr)=0;
    // Reject new operations and wait for in-flight cryptographic execution.
    virtual void shutdown() noexcept=0;
};
// Explicitly evaluation-only. Never selected by a shipping/default factory.
std::shared_ptr<HpkeProvider> createWolfCryptEvaluationHpke();
inline std::shared_ptr<HpkeProvider> createShippingHpke() {
    throw std::runtime_error("No approved shipping HPKE provider configured");
}
}
