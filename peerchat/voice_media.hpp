#pragma once

#include <memory>
#include <string>
#include <vector>

namespace peerchat {

// One-call native media engine. libdatachannel owns ICE/DTLS-SRTP and RTP;
// libopus and Audio own codec and Windows device work. Callbacks are reduced to
// a bounded event queue so the PeerChat node remains the signaling authority.
class VoiceMedia {
public:
    enum class EventKind { Description, Candidate, Connected, Disconnected, Failed, AudioError };
    struct Event {
        EventKind kind;
        std::string value;
        std::string detail;
    };

    VoiceMedia(int inputDevice=-1, int outputDevice=-1);
    ~VoiceMedia();
    VoiceMedia(const VoiceMedia&) = delete;
    VoiceMedia& operator=(const VoiceMedia&) = delete;

    bool startOffer(std::wstring& error);
    bool startAnswer(const std::string& offer, std::wstring& error);
    bool setAnswer(const std::string& answer, std::wstring& error);
    bool addCandidate(const std::string& candidate, const std::string& mid, std::wstring& error);
    bool setMuted(bool muted, std::wstring& error);
    void setDeafened(bool deafened);
    std::wstring diagnostics();
#ifdef PEERCHAT_TEST_VOICE_FRAMES
    void testInjectPcm(const std::vector<unsigned char>& pcm);
#endif
    std::vector<Event> poll();
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace peerchat
