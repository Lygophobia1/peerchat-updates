#pragma once
#include "platform/device_key_provider.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace peerchat {
enum class DeviceKeyRole : unsigned char { IdentitySigning = 1, RecipientEncryption = 2 };
enum class DeviceKeyState : unsigned char { Provisioning = 0, Active = 1, Retired = 2, Revoked = 3 };
enum class DeviceKeyRegistration : unsigned char { NotRegistered = 0, Pending = 1, Accepted = 2 };

struct DeviceKeyMetadata {
    std::string id;
    DeviceKeyRole role = DeviceKeyRole::RecipientEncryption;
    std::string algorithm;
    std::uint32_t generation = 0;
    std::string providerAlias;
    std::vector<unsigned char> publicKey;
    std::uint64_t createdUtcMilliseconds = 0;
    DeviceKeyState state = DeviceKeyState::Provisioning;
    DeviceKeyRegistration registration = DeviceKeyRegistration::NotRegistered;
    std::string predecessorId;
    std::vector<unsigned char> signedBinding;
};

// The adapter must serialize read/replace across processes, integrity protect
// the registry, and atomically persist replacement. An existing but unreadable
// registry must THROW, not return absent. No private material is serialized.
class DeviceKeyRegistry {
public:
    virtual ~DeviceKeyRegistry() = default;
    virtual void locked(const std::function<void()>& operation) = 0;
    virtual bool read(std::vector<DeviceKeyMetadata>& entries) = 0;
    virtual void replace(const std::vector<DeviceKeyMetadata>& entries) = 0;
};

class DeviceKeyManager {
    platform::IdentityProvider& identity_;
    platform::RecipientKeyProvider& recipient_;
    DeviceKeyRegistry& registry_;
    std::mutex mutex_;
    static void require(bool valid) {
        if (!valid) throw std::runtime_error("Device key registry/provider mismatch");
    }
    static bool label(const std::string& value) {
        return !value.empty() && value.size() <= 256 &&
            std::all_of(value.begin(), value.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
            });
    }
public:
    static constexpr const char* recipientSuite = "DHKEM-P256-HKDF-SHA256/HKDF-SHA256/AES128GCM";
    DeviceKeyManager(platform::IdentityProvider& identity,
                     platform::RecipientKeyProvider& recipient, DeviceKeyRegistry& registry)
        : identity_(identity), recipient_(recipient), registry_(registry) {}
    DeviceKeyManager(const DeviceKeyManager&) = delete;
    DeviceKeyManager& operator=(const DeviceKeyManager&) = delete;

    // Existing identity is borrowed as operations; it is neither created nor
    // exported by this manager. Legacy Windows custody stays in its old owner.
    std::vector<unsigned char> identityPublicKey() { return identity_.publicKey(); }
    std::vector<unsigned char> signIdentity(const std::vector<unsigned char>& original) {
        require(original.size() <= 65536);
        return identity_.sign(original);
    }
    bool recipientSupported() const { return recipient_.supported(); }

    static void validate(const std::vector<DeviceKeyMetadata>& entries) {
        require(entries.size() <= 32);
        std::size_t active = 0;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto& e = entries[i];
            require(label(e.id) && label(e.providerAlias) && e.generation > 0 &&
                    e.createdUtcMilliseconds > 0 && e.signedBinding.size() <= 8192 &&
                    static_cast<unsigned>(e.state) <= 3 && static_cast<unsigned>(e.registration) <= 2);
            require(e.predecessorId.empty() || label(e.predecessorId));
            // Identity metadata may be registered by a separate read-only
            // adapter, but this foundation never mutates the signing identity.
            require(e.role == DeviceKeyRole::RecipientEncryption && e.algorithm == recipientSuite);
            if (e.state == DeviceKeyState::Provisioning) {
                require(e.publicKey.empty() && e.registration == DeviceKeyRegistration::NotRegistered && e.signedBinding.empty());
            } else {
                require(e.publicKey.size() == 65 && e.publicKey[0] == 4);
                if (e.registration == DeviceKeyRegistration::Accepted) require(!e.signedBinding.empty());
            }
            if (e.state == DeviceKeyState::Active) ++active;
            for (std::size_t j = 0; j < i; ++j) require(entries[j].id != e.id &&
                entries[j].providerAlias != e.providerAlias && entries[j].generation != e.generation);
        }
        require(active <= 1);
    }

    // Stable alias/id supplied by the profile owner, not a route/display name.
    // Persist intent BEFORE platform creation. An interruption reopens exactly
    // that handle; it cannot silently replace a lost initialized key.
    DeviceKeyMetadata provisionRecipient(const std::string& id, const std::string& alias,
                                        std::uint32_t generation, std::uint64_t createdUtc) {
        std::lock_guard<std::mutex> local(mutex_);
        require(recipient_.supported());
        DeviceKeyMetadata result;
        registry_.locked([&] {
            std::vector<DeviceKeyMetadata> entries;
            registry_.read(entries);
            validate(entries);
            auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.id == id; });
            if (found == entries.end()) {
                // Rotation is a separate explicit protocol, never implicit
                // creation of another active key after an open failure.
                require(entries.empty() && !recipient_.exists(alias));
                DeviceKeyMetadata intent;
                intent.id = id; intent.providerAlias = alias; intent.generation = generation;
                intent.algorithm = recipientSuite; intent.createdUtcMilliseconds = createdUtc;
                entries.push_back(intent); validate(entries); registry_.replace(entries);
                found = entries.begin();
            }
            require(found->providerAlias == alias && found->generation == generation);
            if (found->state == DeviceKeyState::Provisioning) {
                auto key = recipient_.exists(alias) ? recipient_.open(alias) : recipient_.create(alias);
                require(static_cast<bool>(key));
                auto publicKey = key->publicKey();
                require(publicKey.size() == 65 && publicKey[0] == 4);
                found->publicKey = std::move(publicKey); found->state = DeviceKeyState::Active;
                validate(entries); registry_.replace(entries);
            } else {
                require(found->state == DeviceKeyState::Active);
                auto key = recipient_.open(alias);
                require(key && key->publicKey() == found->publicKey);
            }
            result = *found;
        });
        return result;
    }

    std::shared_ptr<platform::RecipientKeyHandle> openRecipient(const std::string& id) {
        std::lock_guard<std::mutex> local(mutex_);
        std::shared_ptr<platform::RecipientKeyHandle> result;
        registry_.locked([&] {
            std::vector<DeviceKeyMetadata> entries;
            require(registry_.read(entries)); validate(entries);
            auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.id == id; });
            require(found != entries.end() && (found->state == DeviceKeyState::Active || found->state == DeviceKeyState::Retired));
            result = recipient_.open(found->providerAlias);
            require(result && result->publicKey() == found->publicKey);
        });
        return result;
    }
};
}
