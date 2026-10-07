#pragma once

#include <memory>
#include <cstdint>
#include <string>
#include <vector>
#ifdef PEERCHAT_TEST_VOICE_FRAMES
#include <functional>
#endif
#include "media_telemetry.hpp"
#include "media_ice_configuration.hpp"
#include "voice_processing_presets.hpp"

namespace peerchat {

// One-call native media engine. libdatachannel owns ICE/DTLS-SRTP and RTP;
// libopus and Audio own codec and Windows device work. Callbacks are reduced to
// a bounded event queue so the PeerChat node remains the signaling authority.
class VoiceMedia {
public:
    struct ProcessingOptions {
        bool enabled = true;
        bool noiseSuppression = true;
        bool automaticGain = true;
        bool echoCancellation = false;
        NoiseSuppressionStrength noiseSuppressionStrength = NoiseSuppressionStrength::Medium;
        AutomaticGainStrength automaticGainStrength = AutomaticGainStrength::Balanced;
    };
    // Supplied by the existing CallManager owner, never inferred from RTP/SSRC.
    struct ReceiveBinding {
        std::string remotePeerId, callId;
        std::uint64_t callLifetime=0, mediaGeneration=0;
    };
    enum class Route { Unknown, DirectLanIce, NatTraversedIce, TurnRelay };
    enum class EventKind { Description, Candidate, Connected, Disconnected, Failed, AudioError, AudioProcessingError };
    struct Event {
        EventKind kind;
        std::string value;
        std::string detail;
    };

    VoiceMedia(MediaIceConfiguration ice, int inputDevice=-1, int outputDevice=-1,
               std::uint64_t networkGeneration=0, std::string networkFingerprint={});
    VoiceMedia(MediaIceConfiguration ice, int inputDevice, int outputDevice,
               std::uint64_t networkGeneration, std::string networkFingerprint,
               ProcessingOptions processing);
    VoiceMedia(MediaIceConfiguration ice, int inputDevice, int outputDevice,
               std::uint64_t networkGeneration, std::string networkFingerprint,
               ProcessingOptions processing, ReceiveBinding receive);
    ~VoiceMedia();
    VoiceMedia(const VoiceMedia&) = delete;
    VoiceMedia& operator=(const VoiceMedia&) = delete;

    bool startOffer(std::wstring& error);
    bool startAnswer(const std::string& offer, std::wstring& error);
    bool setAnswer(const std::string& answer, std::wstring& error);
    bool addCandidate(const std::string& candidate, const std::string& mid, std::wstring& error);
    bool setMuted(bool muted, std::wstring& error);
    void setDeafened(bool deafened);
    void setVolumes(float inputGain, float outputGain);
    MediaTelemetry telemetry();
    Route route();
    std::wstring diagnostics();
#ifdef PEERCHAT_TEST_VOICE_FRAMES
    void testInjectPcm(const std::vector<unsigned char>& pcm);
    void testFailApmNextFrame();
    std::function<int(const unsigned char*,std::size_t)> testReceiveSink();
    bool testPullReceive(std::int16_t* pcm,std::size_t samples);
#endif
    std::vector<Event> poll();
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace peerchat
