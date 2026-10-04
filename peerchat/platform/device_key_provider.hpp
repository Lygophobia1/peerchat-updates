#pragma once
#include "identity_provider.hpp"
#include <memory>
#include <string>

namespace peerchat::platform {
// One role-specific opaque operation. Agreement output is a transient shared
// secret, NOT a private scalar. Callers must erase it after the reviewed KEM
// operation. No signing, generic derivation, or private-key export API exists.
class RecipientKeyHandle {
public:
    virtual ~RecipientKeyHandle() = default;
    // RFC 9180 P-256 uncompressed SEC1 public point (65 bytes).
    virtual std::vector<unsigned char> publicKey() = 0;
    virtual std::vector<unsigned char> agreeP256(
        const std::vector<unsigned char>& validatedPublicPoint) = 0;
};

class RecipientKeyProvider {
public:
    virtual ~RecipientKeyProvider() = default;
    virtual bool supported() const = 0;
    virtual bool exists(const std::string& alias) = 0;
    // create must reject an existing alias; open must never create a key.
    // Both return a non-exportable agreement-only key or throw.
    virtual std::shared_ptr<RecipientKeyHandle> create(const std::string& alias) = 0;
    virtual std::shared_ptr<RecipientKeyHandle> open(const std::string& alias) = 0;
};
}
