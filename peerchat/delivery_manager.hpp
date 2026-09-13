#pragma once
#include "session_transport_types.hpp"
#include "storage.hpp"
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace peerchat::delivery {

// Profile-scoped durable delivery ownership. Storage remains the durable source of truth;
// transport instances only prove which authenticated destination was actually sent an ID.
// No packet type, ciphertext, message ID, ordering rule, or group-ledger rule lives here.
class DeliveryManager {
public:
    DeliveryManager(Storage& storage, std::string localDeviceId)
        : storage_(storage), localDeviceId_(std::move(localDeviceId)) {
        if (localDeviceId_.empty()) throw std::invalid_argument("Local delivery identity is required");
    }

    bool authorizeTransport(session::TransportInstanceId instance,
                            const std::string& authenticatedDestination) {
        if (!instance.value || authenticatedDestination.empty()) return false;
        auto found = transports_.find(instance.value);
        if (found != transports_.end())
            return found->second.destination == authenticatedDestination;
        transports_.emplace(instance.value,
                            TransportAuthorization{authenticatedDestination, false});
        return true;
    }

    bool beginDraining(session::TransportInstanceId instance) {
        auto found = transports_.find(instance.value);
        if (found == transports_.end()) return false;
        found->second.draining = true;
        return true;
    }

    void retireTransport(session::TransportInstanceId instance) {
        transports_.erase(instance.value);
        for (auto it = submissions_.begin(); it != submissions_.end();) {
            it->second.erase(instance.value);
            if (it->second.empty()) it = submissions_.erase(it); else ++it;
        }
    }

    std::vector<StoredMessage> pendingMessages(const std::string& destination,
                                                int limit = 100) {
        return storage_.pending(destination, limit);
    }

    std::vector<GroupEvent> pendingGroupEvents(const std::string& destination,
                                                int limit = 64) {
        return storage_.pendingGroupEvents(destination, limit);
    }

    bool submittedMessage(const std::string& destination, const std::string& messageId,
                          session::TransportInstanceId instance) {
        return submitted(Kind::Message, destination, messageId, instance);
    }

    bool submittedGroupEvent(const std::string& destination, const std::string& eventId,
                             session::TransportInstanceId instance) {
        return submitted(Kind::GroupEvent, destination, eventId, instance);
    }

    bool wasSubmittedMessage(const std::string& destination, const std::string& messageId,
                             session::TransportInstanceId instance) const {
        return wasSubmitted(Kind::Message, destination, messageId, instance);
    }

    bool wasSubmittedGroupEvent(const std::string& destination, const std::string& eventId,
                                session::TransportInstanceId instance) const {
        return wasSubmitted(Kind::GroupEvent, destination, eventId, instance);
    }

    bool acknowledgeMessage(const std::string& authenticatedDestination,
                            session::TransportInstanceId instance,
                            const std::string& messageId) {
        if (!acknowledges(Kind::Message, authenticatedDestination, messageId, instance))
            return false;
        storage_.acknowledge(messageId, Delivery::Delivered);
        submissions_.erase(Key{Kind::Message, authenticatedDestination, messageId});
        return true;
    }

    bool acknowledgeGroupEvent(const std::string& authenticatedDestination,
                               session::TransportInstanceId instance,
                               const std::string& eventId) {
        if (!acknowledges(Kind::GroupEvent, authenticatedDestination, eventId, instance))
            return false;
        storage_.acknowledgeGroupDelivery(eventId, authenticatedDestination);
        submissions_.erase(Key{Kind::GroupEvent, authenticatedDestination, eventId});
        return true;
    }

    // Returns false for an identical replay and lets Storage reject conflicting ID reuse.
    bool acceptIncomingMessage(const std::string& authenticatedSource,
                               const StoredMessage& message) {
        if (authenticatedSource.empty() || message.sender != authenticatedSource ||
            message.recipient != localDeviceId_)
            throw std::runtime_error("Incoming message destination is not authenticated");
        return storage_.append(authenticatedSource, message);
    }

    std::size_t inFlightCount(session::TransportInstanceId instance) const {
        std::size_t count = 0;
        for (const auto& item : submissions_)
            if (item.second.count(instance.value)) ++count;
        return count;
    }

private:
    enum class Kind : unsigned char { Message, GroupEvent };

    struct Key {
        Kind kind;
        std::string destination;
        std::string id;
        bool operator<(const Key& other) const {
            return std::tie(kind, destination, id) <
                   std::tie(other.kind, other.destination, other.id);
        }
    };

    struct TransportAuthorization {
        std::string destination;
        bool draining = false;
    };

    bool submitted(Kind kind, const std::string& destination, const std::string& id,
                   session::TransportInstanceId instance) {
        auto transport = transports_.find(instance.value);
        if (transport == transports_.end() || transport->second.draining ||
            transport->second.destination != destination || id.empty()) return false;
        return submissions_[Key{kind, destination, id}].insert(instance.value).second;
    }

    bool wasSubmitted(Kind kind, const std::string& destination, const std::string& id,
                      session::TransportInstanceId instance) const {
        auto found = submissions_.find(Key{kind, destination, id});
        return found != submissions_.end() && found->second.count(instance.value) != 0;
    }

    bool acknowledges(Kind kind, const std::string& destination, const std::string& id,
                      session::TransportInstanceId instance) const {
        auto transport = transports_.find(instance.value);
        if (transport == transports_.end() || transport->second.destination != destination)
            return false;
        return wasSubmitted(kind, destination, id, instance);
    }

    Storage& storage_;
    std::string localDeviceId_;
    std::map<std::uint64_t, TransportAuthorization> transports_;
    std::map<Key, std::set<std::uint64_t>> submissions_;
};

} // namespace peerchat::delivery
