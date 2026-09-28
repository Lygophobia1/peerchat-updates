#include "voice_apm_capture.hpp"
#include <array>
#include <cstdint>
#include <iostream>

int main() {
    for (int generation = 0; generation != 12; ++generation) {
        peerchat::VoiceApmCapture apm;
        if (!apm.open() || !apm.active()) return 1;
        std::array<std::int16_t, 320> frame{};
        for (unsigned i = 0; i != frame.size(); ++i) frame[i] = static_cast<std::int16_t>(i * 13);
        const auto original = frame;
        if (!apm.processFrame(frame.data())) return 2;
        if (apm.captureCalls() != 2 || apm.processedFrames() != 1 || apm.bypassedFrames() != 0) return 3;
        apm.bypassForTest();
        frame = original;
        if (apm.processFrame(frame.data()) || frame != original || apm.bypassedFrames() != 1 || apm.failure() != 199) return 4;
    }
    std::cout << "PASS: per-generation APM creation, two 10 ms capture blocks per 20 ms frame, and raw-PCM bypass.\n";
}
