#pragma once

#include <memory>
#include <functional>
#include <string>
#include <vector>
#include "media_telemetry.hpp"

namespace peerchat {

// Windows default audio devices. Frames are exactly 20 ms of signed 16-bit,
// little-endian, mono PCM at 16 kHz (640 bytes). Use headphones: this prototype
// does not implement acoustic echo cancellation or noise suppression.
class Audio {
public:
    struct Device { int id; std::wstring name; };
    Audio();
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // Opens capture before playback so a Bluetooth hands-free duplex endpoint
    // is not blocked by an already-open stereo output. Playback may still
    // start when capture fails; in that case error describes the microphone.
    bool start(std::wstring& error);
    void setDevices(int inputId, int outputId);
    static std::vector<Device> inputDevices();
    static std::vector<Device> outputDevices();
    void stop();
    // Muting closes the microphone and discards all pending capture frames;
    // unmuting reopens it.
    bool setMuted(bool muted, std::wstring& error);
    void play(int sender, const std::vector<unsigned char>& pcm);
    std::vector<std::vector<unsigned char>> takeCaptured();
    void setCaptureHandler(std::function<void(const std::vector<unsigned char>&)> handler);
    std::wstring takeError();
    std::wstring diagnostics();
    MediaTelemetry telemetry();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace peerchat
