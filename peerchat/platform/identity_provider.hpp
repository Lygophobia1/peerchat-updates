#pragma once
#include <vector>
#include <cstdint>

namespace peerchat::platform {
// Instance-owned operations, not a serialized key or a transport/session owner.
// Loading must fail closed if an existing device identity cannot be reopened.
class IdentityProvider {
public:
    virtual ~IdentityProvider() = default;
    virtual std::vector<unsigned char> publicKey() = 0;
    // Signs the original bytes with ECDSA/SHA-256, returning existing raw r||s.
    virtual std::vector<unsigned char> sign(const std::vector<unsigned char>&) = 0;
};
}
