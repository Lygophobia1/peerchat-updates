#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <string>

namespace peerchat {

// Short-lived media route input. It is created from authenticated discovery,
// kept only by the live Node/media generation, and is never profile storage.
struct MediaIceConfiguration {
    enum class Policy : std::uint32_t { RelayOnly = 1, DirectTestOnly = 2 };
    Policy policy = Policy::RelayOnly;
    std::string host;
    std::uint16_t port = 0;
    std::string username;
    std::string password;
    std::int64_t expiresUtc = 0;
    // Lifetime bindings supplied by authenticated application orchestration.
    // They are deliberately distinct from a peer endpoint or a route.
    std::uint64_t profileSessionGeneration = 0;
    std::uint64_t discoveryGeneration = 0;
    std::uint64_t generation = 0;

    static bool safeText(const std::string& value) {
        return !value.empty() && std::none_of(value.begin(), value.end(), [](unsigned char c) {
            return c < 0x21 || c == 0x7f;
        });
    }
    bool valid(std::int64_t nowUtc, std::int64_t safetySeconds = 30) const {
        if (policy == Policy::DirectTestOnly) {
#ifdef PEERCHAT_TEST_MEDIA_DIRECT
            return profileSessionGeneration != 0 && discoveryGeneration != 0 && generation != 0;
#else
            return false;
#endif
        }
        return policy == Policy::RelayOnly && safeText(host) && port != 0 &&
            safeText(username) && safeText(password) && expiresUtc > nowUtc + safetySeconds &&
            profileSessionGeneration != 0 && discoveryGeneration != 0 && generation != 0;
    }
    static std::string encode(const std::string& value) {
        static const char hex[] = "0123456789ABCDEF";
        std::string out;
        for (unsigned char c : value) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out.push_back(static_cast<char>(c));
            else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
        }
        return out;
    }
    std::string turnUdpUri() const {
        return "turn:" + encode(username) + ":" + encode(password) + "@" + host + ":" +
            std::to_string(port) + "?transport=udp";
    }
    static bool relayCandidate(const std::string& candidate) {
        return candidate.find(" typ relay") != std::string::npos;
    }
#ifdef PEERCHAT_TEST_MEDIA_DIRECT
    static MediaIceConfiguration directForTests() {
        MediaIceConfiguration value;
        value.policy = Policy::DirectTestOnly;
        value.profileSessionGeneration = value.discoveryGeneration = value.generation = 1;
        return value;
    }
#endif
};

} // namespace peerchat
