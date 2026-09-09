#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include "network.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace peerchat {
namespace {
using Bytes = std::vector<unsigned char>;
using Clock = std::chrono::steady_clock;
constexpr size_t MaxFrame = 4096;
constexpr size_t MaxOutput = 128 * 1024;
constexpr size_t AudioBacklog = 12800;
constexpr size_t MaxEvents = 1024;
constexpr size_t MaxCommands = 256;
constexpr size_t AudioSize = 640;
constexpr auto HandshakeTimeout = std::chrono::seconds(7);
enum Packet : unsigned char { Join = 1, Welcome, Roster, Text, Voice, Audio, Reject };

int hexDigit(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

bool toUtf8(const std::wstring& value, std::string& out) {
    if (value.empty() || value.size() > 4000) return false;
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!count) return false;
    out.resize(static_cast<size_t>(count));
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), &out[0], count, nullptr, nullptr) == count;
}

bool fromUtf8(const unsigned char* data, size_t size, std::wstring& out, bool name = false) {
    if (!size || size > (name ? 96u : 4000u)) return false;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        reinterpret_cast<const char*>(data), static_cast<int>(size), nullptr, 0);
    if (!count || (name && count > 32)) return false;
    out.resize(static_cast<size_t>(count));
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        reinterpret_cast<const char*>(data), static_cast<int>(size), &out[0], count) != count) return false;
    bool visible = false;
    for (wchar_t c : out) {
        if ((c < 32 && (name || (c != L'\n' && c != L'\t' && c != L'\r'))) || c == 127) return false;
        if (c > 32) visible = true;
    }
    return visible;
}

bool validName(const std::wstring& name) {
    std::string utf8;
    std::wstring decoded;
    return name.size() <= 32 && toUtf8(name, utf8) &&
        fromUtf8(reinterpret_cast<const unsigned char*>(utf8.data()), utf8.size(), decoded, true);
}

void cryptoCheck(NTSTATUS status) {
    if (status < 0) throw std::runtime_error("Windows cryptography failed");
}

void randomBytes(unsigned char* data, ULONG size) {
    cryptoCheck(BCryptGenRandom(nullptr, data, size, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

struct Algorithm {
    BCRYPT_ALG_HANDLE handle = nullptr;
    ~Algorithm() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
};
struct Hash {
    BCRYPT_HASH_HANDLE handle = nullptr;
    ~Hash() { if (handle) BCryptDestroyHash(handle); }
};
struct Key {
    BCRYPT_KEY_HANDLE handle = nullptr;
    ~Key() { if (handle) BCryptDestroyKey(handle); }
    Key() = default;
    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;
};

// Independent session/directional keys prevent nonce reuse between connections.
// Both nonces are fresh CSPRNG values. Sequence numbers are implicit and strictly
// ordered by TCP; replay, insertion, deletion, or header alteration fails GCM.
void derive(Key& key, const std::array<unsigned char, 32>& master,
            unsigned char challenges[64], unsigned char direction) {
    Algorithm hmac, aes;
    Hash hash;
    std::array<unsigned char, 32> material{};
    const unsigned char domain[] = "PeerChat-PC02-directional-session";
    cryptoCheck(BCryptOpenAlgorithmProvider(&hmac.handle, BCRYPT_SHA256_ALGORITHM,
        nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG));
    cryptoCheck(BCryptCreateHash(hmac.handle, &hash.handle, nullptr, 0,
        const_cast<PUCHAR>(master.data()), static_cast<ULONG>(master.size()), 0));
    cryptoCheck(BCryptHashData(hash.handle, const_cast<PUCHAR>(domain), sizeof(domain) - 1, 0));
    cryptoCheck(BCryptHashData(hash.handle, challenges, 64, 0));
    cryptoCheck(BCryptHashData(hash.handle, &direction, 1, 0));
    cryptoCheck(BCryptFinishHash(hash.handle, material.data(), static_cast<ULONG>(material.size()), 0));
    try {
        cryptoCheck(BCryptOpenAlgorithmProvider(&aes.handle, BCRYPT_AES_ALGORITHM, nullptr, 0));
        cryptoCheck(BCryptSetProperty(aes.handle, BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
            sizeof(BCRYPT_CHAIN_MODE_GCM), 0));
        cryptoCheck(BCryptGenerateSymmetricKey(aes.handle, &key.handle, nullptr, 0,
            material.data(), static_cast<ULONG>(material.size()), 0));
    } catch (...) { SecureZeroMemory(material.data(), material.size()); throw; }
    SecureZeroMemory(material.data(), material.size());
}

bool crypt(Key& key, uint64_t sequence, const unsigned char header[4],
           unsigned char* input, size_t size, unsigned char* output,
           unsigned char tag[16], bool encrypt) {
    unsigned char nonce[12]{};
    for (int i = 0; i < 8; ++i) nonce[11 - i] = static_cast<unsigned char>(sequence >> (i * 8));
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth;
    BCRYPT_INIT_AUTH_MODE_INFO(auth);
    auth.pbNonce = nonce; auth.cbNonce = sizeof(nonce);
    auth.pbAuthData = const_cast<PUCHAR>(header); auth.cbAuthData = 4;
    auth.pbTag = tag; auth.cbTag = 16;
    ULONG written = 0;
    NTSTATUS status = encrypt
        ? BCryptEncrypt(key.handle, input, static_cast<ULONG>(size), &auth, nullptr, 0,
                        output, static_cast<ULONG>(size), &written, 0)
        : BCryptDecrypt(key.handle, input, static_cast<ULONG>(size), &auth, nullptr, 0,
                        output, static_cast<ULONG>(size), &written, 0);
    return status >= 0 && written == size;
}

Bytes message(Packet type, const std::wstring& value) {
    std::string utf8;
    if (!toUtf8(value, utf8)) return {};
    Bytes result{type};
    result.insert(result.end(), utf8.begin(), utf8.end());
    return result;
}

struct Connection {
    SOCKET socket = INVALID_SOCKET;
    bool connecting = false, hello = false, authenticated = false;
    bool dead = false, closing = false;
    Peer peer;
    Key tx, rx;
    uint64_t sent = 0, received = 0;
    std::array<unsigned char, 36> challenge{};
    Bytes input;
    std::deque<Bytes> output;
    size_t outputOffset = 0, queuedBytes = 0;
    Clock::time_point deadline = Clock::now() + HandshakeTimeout;
    Clock::time_point rateStart = Clock::now();
    unsigned textCount = 0, audioCount = 0, voiceCount = 0;
    ~Connection() { if (socket != INVALID_SOCKET) closesocket(socket); }
};

void nonblocking(SOCKET socket) {
    u_long enabled = 1;
    if (ioctlsocket(socket, FIONBIO, &enabled)) throw std::runtime_error("Nonblocking socket failed");
    BOOL noDelay = TRUE;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&noDelay), sizeof(noDelay));
}

void startHello(Connection& c) {
    std::memcpy(c.challenge.data(), "PC02", 4);
    randomBytes(c.challenge.data() + 4, 32);
    c.output.emplace_back(c.challenge.begin(), c.challenge.end());
    c.queuedBytes += c.challenge.size();
}

bool enqueue(Connection& c, const Bytes& plain, bool audio = false) {
    if (c.dead || c.closing) return false;
    // Drop new audio before encryption/sequence allocation, keeping TCP counters
    // intact. Old encrypted packets must never be removed from a live stream.
    if (audio && c.queuedBytes > AudioBacklog) return true;
    if (plain.empty() || plain.size() > MaxFrame || c.queuedBytes + plain.size() + 20 > MaxOutput ||
        c.sent == std::numeric_limits<uint64_t>::max()) {
        c.dead = true;
        return false;
    }
    Bytes wire(plain.size() + 20);
    wire[0] = static_cast<unsigned char>(plain.size() >> 24);
    wire[1] = static_cast<unsigned char>(plain.size() >> 16);
    wire[2] = static_cast<unsigned char>(plain.size() >> 8);
    wire[3] = static_cast<unsigned char>(plain.size());
    if (!crypt(c.tx, c.sent++, wire.data(), const_cast<PUCHAR>(plain.data()), plain.size(),
               wire.data() + 4, wire.data() + 4 + plain.size(), true)) {
        c.dead = true;
        return false;
    }
    c.queuedBytes += wire.size();
    c.output.push_back(std::move(wire));
    return true;
}

bool flush(Connection& c) {
    size_t budget = 32768;
    while (!c.output.empty() && budget) {
        Bytes& front = c.output.front();
        size_t count = std::min(front.size() - c.outputOffset, budget);
        int sent = send(c.socket, reinterpret_cast<const char*>(front.data() + c.outputOffset),
                        static_cast<int>(count), 0);
        if (sent == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) return true;
        if (sent <= 0) return false;
        size_t n = static_cast<size_t>(sent);
        c.outputOffset += n; c.queuedBytes -= n; budget -= n;
        if (c.outputOffset == front.size()) { c.output.pop_front(); c.outputOffset = 0; }
    }
    return !c.closing || !c.output.empty();
}
} // namespace

struct Network::Impl {
    enum class CommandKind { Text, Voice, Audio };
    struct Command {
        CommandKind kind;
        int channel = 0;
        std::wstring text;
        bool joined = false, muted = true;
        Bytes audio;
    };
    std::atomic<bool> running{false}, connected{false};
    bool wsaReady = false;
    std::mutex lifecycle, commandMutex, eventMutex;
    std::deque<Command> commands;
    std::deque<NetEvent> events;
    std::thread worker;

    // Owned exclusively by the worker until stop() joins it.
    bool hosting = false;
    SOCKET listener = INVALID_SOCKET;
    Peer self;
    std::vector<Peer> roster;
    std::vector<std::unique_ptr<Connection>> connections;
    std::array<unsigned char, 32> master{};

    Impl() {
        WSADATA data{};
        wsaReady = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~Impl() {
        stop();
        SecureZeroMemory(master.data(), master.size());
        if (wsaReady) WSACleanup();
    }

    void emit(NetEvent event) {
        std::lock_guard<std::mutex> guard(eventMutex);
        if (events.size() >= MaxEvents) {
            auto audio = std::find_if(events.begin(), events.end(), [](const NetEvent& e) {
                return e.kind == NetEvent::Kind::Audio;
            });
            if (audio != events.end()) events.erase(audio);
            else if (event.kind == NetEvent::Kind::Audio) return;
            else {
                // A UI that stops draining even text/status events must not grow
                // without bound. End the session and expose the overflow clearly.
                events.pop_front();
                event = NetEvent{};
                event.kind = NetEvent::Kind::Error;
                event.text = L"The interface could not keep up. Reconnect to the room.";
                running = false;
            }
        }
        events.push_back(std::move(event));
    }

    void note(NetEvent::Kind kind, const std::wstring& text) {
        NetEvent e; e.kind = kind; e.text = text; emit(std::move(e));
    }

    bool command(Command c) {
        if (!connected) return false;
        std::lock_guard<std::mutex> guard(commandMutex);
        if (!connected || commands.size() >= MaxCommands) return false;
        commands.push_back(std::move(c));
        return true;
    }

    void stopUnlocked() {
        running = false;
        connected = false;
        if (worker.joinable()) worker.join();
        std::lock_guard<std::mutex> guard(commandMutex);
        commands.clear();
    }

    void stop() {
        std::lock_guard<std::mutex> guard(lifecycle);
        stopUnlocked();
    }

    void start(bool asHost, const std::wstring& address, unsigned short port,
               const std::wstring& key, const std::wstring& name) {
        std::lock_guard<std::mutex> guard(lifecycle);
        stopUnlocked();
        { std::lock_guard<std::mutex> eventGuard(eventMutex); events.clear(); }
        if (!wsaReady) { note(NetEvent::Kind::Error, L"Windows networking could not start."); return; }
        if (!Network::validKey(key)) { note(NetEvent::Kind::Error, L"Room key must be 64 hexadecimal characters."); return; }
        if (!validName(name)) { note(NetEvent::Kind::Error, L"Choose a display name of 1 to 32 characters."); return; }
        if (!port) { note(NetEvent::Kind::Error, L"Choose a port from 1 to 65535."); return; }
        sockaddr_in endpoint{};
        endpoint.sin_family = AF_INET;
        endpoint.sin_port = htons(port);
        std::string ip;
        if (!toUtf8(address, ip) || inet_pton(AF_INET, ip.c_str(), &endpoint.sin_addr) != 1) {
            note(NetEvent::Kind::Error, L"Enter a numeric IPv4 address, such as 192.168.1.20."); return;
        }
        for (size_t i = 0; i < master.size(); ++i)
            master[i] = static_cast<unsigned char>((hexDigit(key[i * 2]) << 4) | hexDigit(key[i * 2 + 1]));
        hosting = asHost;
        self = Peer{asHost ? 0 : -1, name, false, true};
        roster.clear();
        running = true;
        try { worker = std::thread([this, endpoint] { run(endpoint); }); }
        catch (...) {
            running = false;
            SecureZeroMemory(master.data(), master.size());
            note(NetEvent::Kind::Error, L"Could not start the network worker.");
        }
    }

    void connectedEvent() {
        connected = true;
        NetEvent e; e.kind = NetEvent::Kind::Connected; e.sender = self.id;
        e.text = hosting ? L"Room open. Share your address, port, and room key privately." : L"Connected to the room.";
        emit(std::move(e));
    }

    void broadcast(const Bytes& data, int except = -1, bool voiceOnly = false) {
        for (auto& p : connections) {
            if (p->authenticated && !p->dead && p->peer.id != except && (!voiceOnly || p->peer.voice))
                enqueue(*p, data, voiceOnly);
        }
    }

    void publishRoster() {
        roster.clear(); roster.push_back(self);
        for (auto& c : connections) if (c->authenticated && !c->dead) roster.push_back(c->peer);
        Bytes data{Roster, static_cast<unsigned char>(roster.size())};
        for (const auto& p : roster) {
            std::string name; toUtf8(p.name, name);
            data.push_back(static_cast<unsigned char>(p.id));
            data.push_back(static_cast<unsigned char>((p.voice ? 1 : 0) | (p.muted ? 2 : 0)));
            data.push_back(static_cast<unsigned char>(name.size()));
            data.insert(data.end(), name.begin(), name.end());
        }
        broadcast(data);
        NetEvent e; e.kind = NetEvent::Kind::Roster; e.peers = roster; emit(std::move(e));
    }

    void relayText(int sender, int channel, const std::wstring& text) {
        Bytes data = message(Text, text);
        data.insert(data.begin() + 1, {static_cast<unsigned char>(sender), static_cast<unsigned char>(channel)});
        broadcast(data);
        NetEvent e; e.kind = NetEvent::Kind::Text; e.sender = sender; e.channel = channel; e.text = text;
        emit(std::move(e));
    }

    void relayAudio(int sender, const unsigned char* pcm) {
        Bytes data{Audio, static_cast<unsigned char>(sender)};
        data.insert(data.end(), pcm, pcm + AudioSize);
        broadcast(data, sender, true);
        if (self.voice && sender != self.id) {
            NetEvent e; e.kind = NetEvent::Kind::Audio; e.sender = sender;
            e.audio.assign(pcm, pcm + AudioSize); emit(std::move(e));
        }
    }

    bool packet(Connection& c, const Bytes& data) {
        if (data.empty()) return false;
        if (!c.authenticated) {
            if (hosting) {
                std::wstring name;
                if (data[0] != Join || !fromUtf8(data.data() + 1, data.size() - 1, name, true)) return false;
                int id = 1;
                for (; id < 5; ++id) {
                    bool used = false;
                    for (auto& other : connections)
                        if (other->authenticated && !other->dead && other->peer.id == id) used = true;
                    if (!used) break;
                }
                if (id == 5) {
                    enqueue(c, message(Reject, L"This room is full (five people maximum)."));
                    c.closing = true;
                    c.deadline = Clock::now() + std::chrono::seconds(1);
                    return true;
                }
                c.peer = Peer{id, name, false, true}; c.authenticated = true;
                enqueue(c, Bytes{Welcome, static_cast<unsigned char>(id)});
                publishRoster();
                return true;
            }
            if (data[0] == Reject) {
                std::wstring reason;
                if (fromUtf8(data.data() + 1, data.size() - 1, reason)) note(NetEvent::Kind::Error, reason);
                return false;
            }
            if (data.size() != 2 || data[0] != Welcome || data[1] < 1 || data[1] > 4) return false;
            self.id = data[1]; c.authenticated = true; connectedEvent();
            return true;
        }

        if (Clock::now() - c.rateStart >= std::chrono::seconds(1)) {
            c.rateStart = Clock::now(); c.textCount = c.audioCount = c.voiceCount = 0;
        }
        if (hosting) {
            if (data[0] == Text) {
                std::wstring text;
                if (data.size() < 4 || data[1] != 0 || data[2] > 1 || ++c.textCount > 30 ||
                    !fromUtf8(data.data() + 3, data.size() - 3, text)) return false;
                relayText(c.peer.id, data[2], text); return true;
            }
            if (data[0] == Voice) {
                if (data.size() != 2 || data[1] > 3 || ++c.voiceCount > 20) return false;
                c.peer.voice = (data[1] & 1) != 0;
                c.peer.muted = !c.peer.voice || (data[1] & 2) != 0;
                publishRoster(); return true;
            }
            if (data[0] == Audio) {
                if (data.size() != AudioSize + 2 || data[1] != 0 || ++c.audioCount > 100) return false;
                if (c.peer.voice && !c.peer.muted) relayAudio(c.peer.id, data.data() + 2);
                return true;
            }
            return false;
        }
        if (data[0] == Roster) {
            if (data.size() < 2 || data[1] < 1 || data[1] > 5) return false;
            std::vector<Peer> updated;
            size_t offset = 2; unsigned seen = 0;
            for (unsigned i = 0; i < data[1]; ++i) {
                if (offset + 3 > data.size()) return false;
                int id = data[offset++]; unsigned flags = data[offset++]; size_t len = data[offset++];
                if (id > 4 || (seen & (1u << id)) || flags > 3 || offset + len > data.size()) return false;
                Peer p; p.id = id; p.voice = (flags & 1) != 0; p.muted = (flags & 2) != 0;
                if ((!p.voice && !p.muted) || !fromUtf8(data.data() + offset, len, p.name, true)) return false;
                seen |= 1u << id; offset += len; updated.push_back(std::move(p));
            }
            if (offset != data.size() || !(seen & 1) || !(seen & (1u << self.id))) return false;
            roster = std::move(updated);
            NetEvent e; e.kind = NetEvent::Kind::Roster; e.peers = roster; emit(std::move(e));
            return true;
        }
        if (data[0] == Text || data[0] == Audio) {
            if (data.size() < 3 || data[1] > 4) return false;
            auto sender = std::find_if(roster.begin(), roster.end(), [&](const Peer& p) { return p.id == data[1]; });
            if (sender == roster.end()) return false;
            if (data[0] == Text) {
                NetEvent e; e.kind = NetEvent::Kind::Text; e.sender = data[1]; e.channel = data[2];
                if (data.size() < 4 || data[2] > 1 ||
                    !fromUtf8(data.data() + 3, data.size() - 3, e.text)) return false;
                emit(std::move(e)); return true;
            }
            if (data.size() != AudioSize + 2 || data[1] == self.id || !sender->voice || sender->muted) return false;
            if (self.voice) {
                NetEvent e; e.kind = NetEvent::Kind::Audio; e.sender = data[1];
                e.audio.assign(data.begin() + 2, data.end()); emit(std::move(e));
            }
            return true;
        }
        return false;
    }

    bool parse(Connection& c) {
        size_t offset = 0;
        if (!c.hello) {
            if (c.input.size() < 36) return true;
            if (std::memcmp(c.input.data(), "PC02", 4)) return false;
            unsigned char challenges[64];
            std::memcpy(challenges, hosting ? c.input.data() + 4 : c.challenge.data() + 4, 32);
            std::memcpy(challenges + 32, hosting ? c.challenge.data() + 4 : c.input.data() + 4, 32);
            derive(c.tx, master, challenges, hosting ? 1 : 0);
            derive(c.rx, master, challenges, hosting ? 0 : 1);
            c.hello = true; offset = 36;
            if (!hosting) enqueue(c, message(Join, self.name));
        }
        while (!c.closing && c.input.size() - offset >= 4) {
            unsigned char* header = c.input.data() + offset;
            size_t size = (static_cast<size_t>(header[0]) << 24) |
                (static_cast<size_t>(header[1]) << 16) | (static_cast<size_t>(header[2]) << 8) | header[3];
            if (!size || size > MaxFrame || c.received == std::numeric_limits<uint64_t>::max()) return false;
            if (c.input.size() - offset < size + 20) break;
            Bytes plain(size);
            if (!crypt(c.rx, c.received++, header, header + 4, size, plain.data(), header + 4 + size, false)) return false;
            if (!packet(c, plain)) return false;
            offset += size + 20;
        }
        if (offset) c.input.erase(c.input.begin(), c.input.begin() + static_cast<std::ptrdiff_t>(offset));
        return !c.dead;
    }

    bool receive(Connection& c) {
        unsigned char buffer[8192];
        // Per-tick budget keeps an abusive socket from starving other peers.
        for (int iteration = 0; iteration < 4 && !c.closing; ++iteration) {
            int n = recv(c.socket, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
            if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) return true;
            if (n <= 0) return false;
            c.input.insert(c.input.end(), buffer, buffer + n);
            if (c.input.size() > MaxFrame + 20 + sizeof(buffer) || !parse(c)) return false;
        }
        return true;
    }

    void drainCommands() {
        std::deque<Command> pending;
        { std::lock_guard<std::mutex> guard(commandMutex); pending.swap(commands); }
        if (!connected) return;
        for (auto& cmd : pending) {
            if (cmd.kind == CommandKind::Text) {
                if (hosting) relayText(self.id, cmd.channel, cmd.text);
                else if (!connections.empty()) {
                    Bytes data = message(Text, cmd.text);
                    data.insert(data.begin() + 1, {0, static_cast<unsigned char>(cmd.channel)});
                    enqueue(*connections.front(), data);
                }
            } else if (cmd.kind == CommandKind::Voice) {
                self.voice = cmd.joined; self.muted = !self.voice || cmd.muted;
                if (hosting) publishRoster();
                else if (!connections.empty()) enqueue(*connections.front(),
                    Bytes{Voice, static_cast<unsigned char>((self.voice ? 1 : 0) | (self.muted ? 2 : 0))});
            } else if (self.voice && !self.muted) {
                if (hosting) relayAudio(self.id, cmd.audio.data());
                else if (!connections.empty()) {
                    Bytes data{Audio, 0}; data.insert(data.end(), cmd.audio.begin(), cmd.audio.end());
                    enqueue(*connections.front(), data, true);
                }
            }
        }
    }

    void acceptPending() {
        for (int attempts = 0; attempts < 4; ++attempts) {
            SOCKET s = accept(listener, nullptr, nullptr);
            if (s == INVALID_SOCKET) break;
            auto c = std::make_unique<Connection>(); c->socket = s;
            // Four active guests plus at most four handshakes, bounded at all times.
            if (connections.size() >= 8) continue;
            nonblocking(s); startHello(*c); connections.push_back(std::move(c));
        }
    }

    void prune() {
        bool changed = false;
        for (auto it = connections.begin(); it != connections.end();) {
            Connection& c = **it;
            if ((!c.authenticated || c.closing) && Clock::now() > c.deadline) c.dead = true;
            if (c.dead) {
                changed = changed || c.authenticated;
                if (!hosting) {
                    note(NetEvent::Kind::Error, c.authenticated
                        ? L"Connection to the host was lost. Reconnect when the host is available."
                        : L"Could not join. Check the address, room key, host capacity, and firewall.");
                    running = false;
                }
                it = connections.erase(it);
            } else ++it;
        }
        if (hosting && changed) publishRoster();
    }

    void run(sockaddr_in endpoint) {
        bool hadConnection = false;
        try {
            if (hosting) {
                listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (listener == INVALID_SOCKET) throw std::runtime_error("Socket creation failed");
                BOOL exclusive = TRUE;
                setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<char*>(&exclusive), sizeof(exclusive));
                nonblocking(listener);
                if (bind(listener, reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint)) || listen(listener, 8)) {
                    note(NetEvent::Kind::Error, L"Could not open this port. Check the address or choose another port.");
                    running = false;
                } else { connectedEvent(); publishRoster(); hadConnection = true; }
            } else {
                note(NetEvent::Kind::Status, L"Connecting and authenticating the room...");
                auto c = std::make_unique<Connection>();
                c->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (c->socket == INVALID_SOCKET) throw std::runtime_error("Socket creation failed");
                nonblocking(c->socket);
                int result = connect(c->socket, reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint));
                if (result && WSAGetLastError() != WSAEWOULDBLOCK) {
                    note(NetEvent::Kind::Error, L"Could not connect to this address and port."); running = false;
                } else { c->connecting = result != 0; startHello(*c); connections.push_back(std::move(c)); }
            }
            while (running) {
                drainCommands();
                fd_set reads, writes, errors;
                FD_ZERO(&reads); FD_ZERO(&writes); FD_ZERO(&errors);
                if (listener != INVALID_SOCKET) FD_SET(listener, &reads);
                for (auto& c : connections) {
                    if (c->dead) continue;
                    if (!c->connecting && !c->closing) FD_SET(c->socket, &reads);
                    if (c->connecting || !c->output.empty()) FD_SET(c->socket, &writes);
                    FD_SET(c->socket, &errors);
                }
                timeval timeout{}; timeout.tv_usec = 10000;
                int selected = select(0, &reads, &writes, &errors, &timeout);
                if (selected == SOCKET_ERROR) throw std::runtime_error("Network polling failed");
                if (!running) break;
                if (listener != INVALID_SOCKET && FD_ISSET(listener, &reads)) acceptPending();
                for (auto& c : connections) {
                    if (c->dead) continue;
                    if (FD_ISSET(c->socket, &errors)) { c->dead = true; continue; }
                    if (c->connecting && FD_ISSET(c->socket, &writes)) {
                        int error = 0; int size = sizeof(error);
                        if (getsockopt(c->socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size) || error)
                            c->dead = true;
                        c->connecting = false;
                    }
                    if (!c->dead && !c->connecting && FD_ISSET(c->socket, &reads) && !receive(*c)) c->dead = true;
                    if (!c->dead && !c->connecting && FD_ISSET(c->socket, &writes) && !flush(*c)) c->dead = true;
                }
                hadConnection = hadConnection || connected.load();
                prune();
            }
        } catch (...) {
            note(NetEvent::Kind::Error, L"A Windows networking or cryptography operation failed. The room was closed.");
        }
        connected = false; running = false;
        connections.clear();
        if (listener != INVALID_SOCKET) { closesocket(listener); listener = INVALID_SOCKET; }
        SecureZeroMemory(master.data(), master.size());
        roster.clear();
        if (hadConnection) note(NetEvent::Kind::Disconnected, L"Disconnected from the room.");
    }
};

Network::Network() : impl_(std::make_unique<Impl>()) {}
Network::~Network() = default;
void Network::host(const std::wstring& address, unsigned short port, const std::wstring& key, const std::wstring& name) {
    impl_->start(true, address, port, key, name);
}
void Network::join(const std::wstring& address, unsigned short port, const std::wstring& key, const std::wstring& name) {
    impl_->start(false, address, port, key, name);
}
void Network::stop() { impl_->stop(); }

bool Network::sendText(int channel, const std::wstring& text) {
    std::string utf8;
    std::wstring decoded;
    if (channel < 0 || channel > 1 || !toUtf8(text, utf8) ||
        !fromUtf8(reinterpret_cast<const unsigned char*>(utf8.data()), utf8.size(), decoded)) return false;
    Impl::Command c; c.kind = Impl::CommandKind::Text; c.channel = channel; c.text = text;
    return impl_->command(std::move(c));
}
void Network::setVoice(bool joined, bool muted) {
    Impl::Command c; c.kind = Impl::CommandKind::Voice; c.joined = joined; c.muted = muted;
    impl_->command(std::move(c));
}
bool Network::sendAudio(const Bytes& pcm) {
    if (pcm.size() != AudioSize) return false;
    Impl::Command c; c.kind = Impl::CommandKind::Audio; c.audio = pcm;
    return impl_->command(std::move(c));
}
std::vector<NetEvent> Network::poll() {
    std::lock_guard<std::mutex> guard(impl_->eventMutex);
    std::vector<NetEvent> result;
    result.reserve(impl_->events.size());
    while (!impl_->events.empty()) {
        result.push_back(std::move(impl_->events.front())); impl_->events.pop_front();
    }
    return result;
}
std::wstring Network::generateKey() {
    std::array<unsigned char, 32> key{};
    try { randomBytes(key.data(), static_cast<ULONG>(key.size())); }
    catch (...) { return {}; }
    constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring result;
    result.reserve(64);
    for (unsigned char byte : key) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    SecureZeroMemory(key.data(), key.size());
    return result;
}
bool Network::validKey(const std::wstring& key) {
    return key.size() == 64 && std::all_of(key.begin(), key.end(), [](wchar_t c) { return hexDigit(c) >= 0; });
}
} // namespace peerchat
