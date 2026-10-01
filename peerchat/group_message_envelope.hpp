#pragma once
#include "group_key_manager.hpp"
#include <functional>

namespace peerchat::group_messages {
// Separate from the legacy signed-plaintext ledger event format. No network dispatch here.
constexpr std::uint16_t EnvelopeVersion=1;
constexpr std::size_t MaxPlaintextBytes=65536;
constexpr std::size_t HeaderBytes=174;
constexpr std::size_t MaxEnvelopeBytes=HeaderBytes+MaxPlaintextBytes+16+64;

enum class Result {
    Accepted, UnsupportedVersion, MalformedEnvelope, WrongGroup, RosterUnavailable,
    StaleRosterGeneration, FutureRosterGeneration, StaleKeyGeneration, FutureKeyGeneration,
    MissingGroupKey, LocalNotMember, SenderNotMember, UnauthorizedSenderDevice,
    SignatureFailed, AeadAuthFailed, DuplicateMessage, ConflictingMessage,
    CryptoFailed, StorageFailed, InvalidMessageBody
};
const char* resultName(Result result)noexcept;
struct Envelope {
    std::uint16_t version=EnvelopeVersion;
    std::string groupId,messageId,senderDevice;
    std::uint64_t rosterGeneration=0,keyGeneration=0,senderAccount=0;
    Blob nonce,ciphertext,tag,signature;
};
// Fixed-width canonical header is also GCM AAD. All integers are big endian.
Blob associatedData(const Envelope& envelope);
Blob signingPayload(const Envelope& envelope);
Blob serialize(const Envelope& envelope);
Result parse(const Blob& wire,Envelope& envelope)noexcept;

struct Creation { Result result=Result::CryptoFailed;Envelope envelope;Blob wire; };
struct Reception {
    Result result=Result::MalformedEnvelope;
    Envelope envelope;
    Blob plaintext; // Populated only after authentication AND durable dedupe/history commit.
};

// Thread-confined message layer. Borrows the existing profile, roster/key owner and store.
// Owns neither sessions nor delivery, and never treats GroupControl as a chat channel.
class MessageLayer {
public:
    MessageLayer(Storage& store,const group_keys::GroupKeyManager& keys,const Profile& device,std::uint64_t account)
        :store_(store),keys_(keys),device_(device),account_(account){}
    Creation create(const std::string& localGroupId,const GroupState& ledger,const Blob& plaintext);
    Reception receive(const std::string& localGroupId,const GroupState& ledger,const Blob& wire,
        const std::function<bool(const Blob&)>& validateBody={});
private:
    Result context(const std::string& localGroupId,const GroupState& ledger,
        const group_keys::RosterSnapshot*& roster)const;
    Storage& store_;
    const group_keys::GroupKeyManager& keys_;
    const Profile& device_;
    std::uint64_t account_;
};
}
