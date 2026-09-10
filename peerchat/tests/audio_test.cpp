// Device-free tests. Including the implementation gives this single test
// translation unit access to the private mixer without exposing it in the API.
// Build this file alone with -lwinmm; do not also compile audio.cpp into the test.
#include "audio.cpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<unsigned char> tone(int sample) {
    std::vector<unsigned char> pcm(peerchat::audio_detail::frameBytes);
    const unsigned int bits = static_cast<unsigned int>(sample) & 0xffffu;
    for (std::size_t i = 0; i < pcm.size(); i += 2) {
        pcm[i] = static_cast<unsigned char>(bits & 0xffu);
        pcm[i + 1] = static_cast<unsigned char>((bits >> 8) & 0xffu);
    }
    return pcm;
}

void checkFrame(const peerchat::audio_detail::Frame& frame, int expected) {
    const auto wanted = tone(expected);
    require(std::equal(frame.begin(), frame.end(), wanted.begin()), "Unexpected mixed samples");
}
} // namespace

int main() {
    using namespace peerchat::audio_detail;
    using namespace std::chrono_literals;
    try {
        const Clock::time_point now{};
        Mixer mixer;

        checkFrame(mixer.mix(now), 0);
        mixer.push(1, tone(1000), now);
        checkFrame(mixer.mix(now + 20ms), 0); // One frame waits for jitter prefill.
        checkFrame(mixer.mix(now + 60ms), 1000); // Last isolated frame is not lost.
        checkFrame(mixer.mix(now + 80ms), 0);

        mixer.clear();
        for (int sender = 0; sender < 4; ++sender) {
            mixer.push(sender, tone(1000 * (sender + 1)), now);
            mixer.push(sender, tone(1000 * (sender + 1)), now);
        }
        checkFrame(mixer.mix(now), 10000); // Four simultaneous remote speakers.
        checkFrame(mixer.mix(now + 20ms), 10000);
        checkFrame(mixer.mix(now + 40ms), 0);

        mixer.clear();
        for (int sender = 0; sender < 4; ++sender) {
            mixer.push(sender, tone(25000), now);
            mixer.push(sender, tone(-25000), now);
        }
        checkFrame(mixer.mix(now), 32767);
        checkFrame(mixer.mix(now + 20ms), -32768);

        mixer.clear();
        for (int sample = 1; sample <= 20; ++sample) mixer.push(1, tone(sample), now);
        for (int expected = 13; expected <= 20; ++expected)
            checkFrame(mixer.mix(now), expected); // Only newest eight frames survive.
        checkFrame(mixer.mix(now), 0);

        mixer.clear();
        mixer.push(1, std::vector<unsigned char>(639, 255), now);
        mixer.push(2, std::vector<unsigned char>(641, 255), now);
        mixer.push(-1, tone(10000), now);
        checkFrame(mixer.mix(now + 100ms), 0);

        mixer.clear();
        for (int sender = 0; sender < 5; ++sender) {
            mixer.push(sender, tone(1000), now);
            mixer.push(sender, tone(1000), now);
        }
        checkFrame(mixer.mix(now), 4000); // A fifth remote cannot grow the map.
        checkFrame(mixer.mix(now + 3s), 0); // Discard stale audio after interruption.
        mixer.push(99, tone(-1000), now + 3s);
        mixer.push(99, tone(1000), now + 3s);
        checkFrame(mixer.mix(now + 3s), -1000); // Stale slots become reusable.
        checkFrame(mixer.mix(now + 3s), 1000);

        // Construction, closed-state calls, and destruction do not open devices.
        peerchat::Audio audio;
        std::wstring error;
        require(!audio.setMuted(false, error), "Capture must not start before joining");
        require(!error.empty(), "Missing closed-state error");
        require(audio.setMuted(true, error), "Muting a closed device should be safe");
        require(error.empty(), "Successful mute should clear stale caller error");
        audio.play(1, tone(10000));
        require(audio.takeCaptured().empty(), "Closed device returned captured audio");
        require(audio.takeError().empty(), "Unexpected worker error");
        audio.stop();
        audio.stop();
        const auto defaults = deviceCandidates(-1, true);
        require(!defaults.empty() && defaults.front() == WAVE_MAPPER,
                "Default capture fallback must try the Windows mapper first");
        const auto stale = deviceCandidates(12345, false);
        require(stale.size() >= 2 && stale[0] == 12345 && stale[1] == WAVE_MAPPER,
                "A stale selected output must fall back to the Windows mapper");
        std::cout << "PASS: PCM mixing, four peers, jitter, clipping, bounded queues, stale frames/devices, closed lifecycle. No audio devices opened.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
