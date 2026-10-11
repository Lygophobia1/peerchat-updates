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
        require(deviceCandidates(999,false)==std::vector<UINT>({999}),"Missing explicit speaker must not silently switch to another device");
        const std::vector<peerchat::Audio::Device> devices{{-1,L"Windows default microphone"},{0,L"Other mic"},{3,L"JLab Hands-Free"}};
        require(captureCandidates(-1,devices,3,false)==std::vector<UINT>({3,0}),
                "Default must resolve to the concrete JLab ID before fallback");
        require(captureCandidates(0,devices,3,false)==std::vector<UINT>({0}),
                "Explicit microphone must not be replaced by the default or another device");
        require(captureCandidates(7,devices,3,false).empty(),"Missing explicit mic must not silently switch");
        require(captureCandidates(-1,devices,3,true)==std::vector<UINT>({3}),"Windows preferred-only policy ignored");
        require(captureCandidates(-1,devices,WAVE_MAPPER,false)==std::vector<UINT>({0,3}),
                "Unresolved default must use concrete enumerated IDs, never WAVE_MAPPER");
        require(pcmLevel(tone(0)) == 0.0f,"Silence must produce a zero media level");
        require(pcmLevel(tone(16384)) > 0.49f && pcmLevel(tone(16384)) < 0.51f,
                "PCM RMS media level was not normalized");
        require(decayedLevel(0.8f,now,now+500ms)<0.05f,
                "Media level decay did not return stale audio toward zero");
        auto half=tone(10000);scalePcm(half,0.5f);require(std::equal(half.begin(),half.end(),tone(5000).begin()),"Input/output software gain did not scale PCM");
        auto clipped=tone(25000);scalePcm(clipped,2.0f);require(std::equal(clipped.begin(),clipped.end(),tone(32767).begin()),"Software gain did not clip safely");
        auto silent=tone(10000);scalePcm(silent,-1.0f);require(std::equal(silent.begin(),silent.end(),tone(0).begin()),"Software gain did not clamp below zero");
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
        const auto telemetry=audio.telemetry();
        require(!telemetry.active&&!telemetry.inputAvailable&&!telemetry.outputAvailable&&telemetry.inputLevel==0&&telemetry.outputLevel==0,
                "Closed audio retained media telemetry between calls");
        const auto defaults = deviceCandidates(-1, true);
        require(!defaults.empty() && defaults.front() == WAVE_MAPPER,
                "Default capture fallback must try the Windows mapper first");
        const auto stale = deviceCandidates(12345, false);
        require(stale == std::vector<UINT>({12345}),
                "Explicit stale output must fail rather than silently substitute a different speaker");
        std::cout << "PASS: PCM mixing, four peers, jitter, clipping, bounded queues, stale frames/devices, closed lifecycle. No audio devices opened.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
