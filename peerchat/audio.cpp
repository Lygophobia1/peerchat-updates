#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "audio.hpp"
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace peerchat {
namespace audio_detail {

constexpr std::size_t frameBytes = 640;
constexpr std::size_t frameSamples = frameBytes / 2;
constexpr std::size_t maxQueuedFrames = 8;
constexpr std::size_t maxRemotePeers = 4;
using Frame = std::array<unsigned char, frameBytes>;
using Clock = std::chrono::steady_clock;

// Pure queue/mixer logic is separate from the devices so it can be tested
// without opening either a microphone or a speaker.
class Mixer {
    struct Stream {
        std::deque<Frame> frames;
        Clock::time_point firstQueued{}, lastReceived{};
        bool playing = false;
    };
    std::map<int, Stream> streams_;

    void expire(Clock::time_point now) {
        for (auto it = streams_.begin(); it != streams_.end();) {
            if (now - it->second.lastReceived > std::chrono::seconds(2))
                it = streams_.erase(it);
            else
                ++it;
        }
    }

public:
    void clear() { streams_.clear(); }

    void push(int sender, const std::vector<unsigned char>& pcm, Clock::time_point now) {
        if (sender < 0 || pcm.size() != frameBytes) return;
        expire(now);
        auto it = streams_.find(sender);
        if (it == streams_.end()) {
            if (streams_.size() >= maxRemotePeers) return;
            it = streams_.emplace(sender, Stream{}).first;
        }
        auto& stream = it->second;
        if (stream.frames.empty()) stream.firstQueued = now;
        stream.lastReceived = now;
        // Prefer current speech if the UI/network falls behind.
        if (stream.frames.size() == maxQueuedFrames) stream.frames.pop_front();
        Frame frame{};
        std::copy(pcm.begin(), pcm.end(), frame.begin());
        stream.frames.push_back(frame);
    }

    Frame mix(Clock::time_point now) {
        expire(now);
        std::array<int, frameSamples> sum{};
        for (auto& item : streams_) {
            auto& stream = item.second;
            if (stream.frames.empty()) {
                stream.playing = false;
                continue;
            }
            if (!stream.playing) {
                // Two frames absorb minor network jitter. A final isolated
                // frame still plays after a bounded wait.
                if (stream.frames.size() < 2 &&
                    now - stream.firstQueued < std::chrono::milliseconds(60)) continue;
                stream.playing = true;
            }
            const auto& frame = stream.frames.front();
            for (std::size_t i = 0; i < frameSamples; ++i) {
                const int raw = static_cast<int>(frame[i * 2]) |
                                (static_cast<int>(frame[i * 2 + 1]) << 8);
                sum[i] += raw >= 32768 ? raw - 65536 : raw;
            }
            stream.frames.pop_front();
        }
        Frame mixed{};
        for (std::size_t i = 0; i < frameSamples; ++i) {
            const int sample = std::clamp(sum[i], -32768, 32767);
            const unsigned int bits = static_cast<unsigned int>(sample) & 0xffffu;
            mixed[i * 2] = static_cast<unsigned char>(bits & 0xffu);
            mixed[i * 2 + 1] = static_cast<unsigned char>((bits >> 8) & 0xffu);
        }
        return mixed;
    }
};

std::wstring deviceError(const wchar_t* action, MMRESULT result, bool input) {
    wchar_t description[MAXERRORLENGTH]{};
    if (input) waveInGetErrorTextW(result, description, MAXERRORLENGTH);
    else waveOutGetErrorTextW(result, description, MAXERRORLENGTH);
    return std::wstring(action) + L": " +
           (description[0] ? description : L"Windows audio device error") + L".";
}

WAVEFORMATEX pcmFormat() {
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = 16000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec = 32000;
    return format;
}

struct Buffer {
    Frame data{};
    WAVEHDR header{};
    void initialize() {
        data.fill(0);
        header = {};
        header.lpData = reinterpret_cast<LPSTR>(data.data());
        header.dwBufferLength = static_cast<DWORD>(data.size());
    }
};

} // namespace audio_detail

struct Audio::Impl {
    // Lifecycle calls belong to the UI thread. The same lock protects its
    // device changes and the worker's processing, with no function callbacks.
    std::mutex mutex;
    std::thread worker;
    HANDLE stopEvent = nullptr, inputEvent = nullptr, outputEvent = nullptr;
    HWAVEIN input = nullptr;
    HWAVEOUT output = nullptr;
    std::array<audio_detail::Buffer, 4> inputBuffers;
    std::array<audio_detail::Buffer, 3> outputBuffers;
    std::size_t nextInput = 0, nextOutput = 0;
    bool started = false;
    audio_detail::Mixer mixer;
    std::deque<audio_detail::Frame> captured;
    std::wstring pendingError;

    // Called with the lock held, never while a worker touches these headers.
    void closeInput() {
        if (input) {
            waveInReset(input); // Returns all outstanding buffers before release.
            for (auto& buffer : inputBuffers) {
                if (buffer.header.dwFlags & WHDR_PREPARED)
                    waveInUnprepareHeader(input, &buffer.header, sizeof(WAVEHDR));
            }
            waveInClose(input);
            input = nullptr;
        }
        captured.clear();
        nextInput = 0;
    }

    void closeDevices() {
        closeInput();
        if (output) {
            waveOutReset(output);
            for (auto& buffer : outputBuffers) {
                if (buffer.header.dwFlags & WHDR_PREPARED)
                    waveOutUnprepareHeader(output, &buffer.header, sizeof(WAVEHDR));
            }
            waveOutClose(output);
            output = nullptr;
        }
        nextOutput = 0;
        mixer.clear();
        for (HANDLE* event : {&stopEvent, &inputEvent, &outputEvent}) {
            if (*event) CloseHandle(*event);
            *event = nullptr;
        }
    }

    void run() {
        const HANDLE events[] = {stopEvent, inputEvent, outputEvent};
        for (;;) {
            const DWORD wait = WaitForMultipleObjects(3, events, FALSE, INFINITE);
            if (wait == WAIT_OBJECT_0) return;
            std::lock_guard<std::mutex> lock(mutex);
            if (!started) return;
            if (wait == WAIT_FAILED) {
                pendingError = L"Audio processing stopped because a Windows event failed.";
                closeInput();
                started = false;
                return;
            }
            // Consume ring buffers in submission order, including when several
            // finish between wakeups. Scanning from index zero would reorder PCM.
            for (std::size_t n = 0; input && n < inputBuffers.size(); ++n) {
                auto& buffer = inputBuffers[nextInput];
                if (!(buffer.header.dwFlags & WHDR_DONE)) break;
                if (buffer.header.dwBytesRecorded == audio_detail::frameBytes) {
                    if (captured.size() == audio_detail::maxQueuedFrames) captured.pop_front();
                    captured.push_back(buffer.data);
                }
                buffer.header.dwBytesRecorded = 0;
                const MMRESULT result = waveInAddBuffer(input, &buffer.header, sizeof(WAVEHDR));
                if (result != MMSYSERR_NOERROR) {
                    pendingError = audio_detail::deviceError(L"Microphone stopped", result, true);
                    closeInput();
                    break;
                }
                nextInput = (nextInput + 1) % inputBuffers.size();
            }
            for (std::size_t n = 0; n < outputBuffers.size(); ++n) {
                auto& buffer = outputBuffers[nextOutput];
                if (!(buffer.header.dwFlags & WHDR_DONE)) break;
                buffer.data = mixer.mix(audio_detail::Clock::now());
                const MMRESULT result = waveOutWrite(output, &buffer.header, sizeof(WAVEHDR));
                if (result != MMSYSERR_NOERROR) {
                    pendingError = audio_detail::deviceError(L"Voice playback stopped", result, false);
                    closeInput();
                    started = false;
                    return;
                }
                nextOutput = (nextOutput + 1) % outputBuffers.size();
            }
        }
    }
};

Audio::Audio() : impl_(std::make_unique<Impl>()) {}
Audio::~Audio() { stop(); }

bool Audio::start(std::wstring& error) {
    error.clear();
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->started) return true;
    }
    stop(); // Also reaps a worker that ended after a device failure.
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto& state = *impl_;
    state.pendingError.clear();
    state.stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    state.inputEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    state.outputEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!state.stopEvent || !state.inputEvent || !state.outputEvent) {
        error = L"Could not create Windows audio events.";
        state.closeDevices();
        return false;
    }
    WAVEFORMATEX format = audio_detail::pcmFormat();
    MMRESULT result = waveOutOpen(&state.output, WAVE_MAPPER, &format,
        reinterpret_cast<DWORD_PTR>(state.outputEvent), 0, CALLBACK_EVENT);
    if (result != MMSYSERR_NOERROR) {
        error = audio_detail::deviceError(L"Could not open the default speaker", result, false);
        state.closeDevices();
        return false;
    }
    for (auto& buffer : state.outputBuffers) {
        buffer.initialize();
        result = waveOutPrepareHeader(state.output, &buffer.header, sizeof(WAVEHDR));
        if (result == MMSYSERR_NOERROR)
            result = waveOutWrite(state.output, &buffer.header, sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR) {
            error = audio_detail::deviceError(L"Could not start voice playback", result, false);
            state.closeDevices();
            return false;
        }
    }
    state.started = true;
    try {
        state.worker = std::thread([&state] { state.run(); });
    } catch (const std::system_error&) {
        state.started = false;
        error = L"Could not create the audio processing thread.";
        state.closeDevices();
        return false;
    }
    return true;
}

void Audio::stop() {
    auto& state = *impl_;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.started = false;
        if (state.stopEvent) SetEvent(state.stopEvent);
    }
    // Never join while holding the lock needed by the finishing worker.
    if (state.worker.joinable()) state.worker.join();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.closeDevices();
}

bool Audio::setMuted(bool muted, std::wstring& error) {
    error.clear();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto& state = *impl_;
    if (muted) {
        state.closeInput();
        return true;
    }
    if (!state.started) {
        error = L"Join a voice call before turning on your microphone.";
        return false;
    }
    if (state.input) return true;
    WAVEFORMATEX format = audio_detail::pcmFormat();
    MMRESULT result = waveInOpen(&state.input, WAVE_MAPPER, &format,
        reinterpret_cast<DWORD_PTR>(state.inputEvent), 0, CALLBACK_EVENT);
    if (result != MMSYSERR_NOERROR) {
        error = audio_detail::deviceError(L"Could not open the default microphone", result, true);
        state.closeInput();
        return false;
    }
    for (auto& buffer : state.inputBuffers) {
        buffer.initialize();
        result = waveInPrepareHeader(state.input, &buffer.header, sizeof(WAVEHDR));
        if (result == MMSYSERR_NOERROR)
            result = waveInAddBuffer(state.input, &buffer.header, sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR) {
            error = audio_detail::deviceError(L"Could not prepare microphone capture", result, true);
            state.closeInput();
            return false;
        }
    }
    result = waveInStart(state.input);
    if (result != MMSYSERR_NOERROR) {
        error = audio_detail::deviceError(L"Could not start the microphone", result, true);
        state.closeInput();
        return false;
    }
    return true;
}

void Audio::play(int sender, const std::vector<unsigned char>& pcm) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->started) impl_->mixer.push(sender, pcm, audio_detail::Clock::now());
}

std::vector<std::vector<unsigned char>> Audio::takeCaptured() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<std::vector<unsigned char>> frames;
    frames.reserve(impl_->captured.size());
    for (const auto& frame : impl_->captured) frames.emplace_back(frame.begin(), frame.end());
    impl_->captured.clear();
    return frames;
}

std::wstring Audio::takeError() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::wstring error;
    error.swap(impl_->pendingError);
    return error;
}

} // namespace peerchat
