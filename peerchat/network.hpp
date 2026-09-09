#pragma once

#include <memory>
#include <string>
#include <vector>

namespace peerchat {

struct Peer {
    int id = 0;
    std::wstring name;
    bool voice = false;
    bool muted = true;
};

struct NetEvent {
    enum class Kind { Status, Connected, Disconnected, Roster, Text, Audio, Error };
    Kind kind = Kind::Status;
    std::wstring text;
    std::vector<Peer> peers;
    int sender = 0;
    int channel = 0;
    std::vector<unsigned char> audio;
};

// Host-relayed, five-person room. The host can read every relayed message.
// PC02 is an experimental shared-key protocol, NOT an audited secure messenger.
// AES-256-GCM authenticates each hop; it does NOT provide forward secrecy or
// individual identity verification. Anyone holding the room key can join.
// Room addresses are numeric IPv4; connecting and all socket I/O run off the UI.
class Network {
public:
    Network();
    ~Network();
    Network(const Network&) = delete;
    Network& operator=(const Network&) = delete;

    void host(const std::wstring& bindAddress, unsigned short port,
              const std::wstring& keyHex, const std::wstring& name);
    void join(const std::wstring& address, unsigned short port,
              const std::wstring& keyHex, const std::wstring& name);
    void stop();
    bool sendText(int channel, const std::wstring& text);
    void setVoice(bool joined, bool muted);
    // Exactly 20 ms of mono, signed 16-bit little-endian PCM at 16 kHz (640 B).
    bool sendAudio(const std::vector<unsigned char>& pcm);
    std::vector<NetEvent> poll();
    static std::wstring generateKey();
    static bool validKey(const std::wstring& key);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace peerchat
