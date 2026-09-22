#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "audio.hpp"
#include <windows.h>
#include <mmsystem.h>
#include <sstream>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace peerchat {
namespace audio_detail {

// Mmddk.h constants, absent from the pinned TDM-GCC headers. These are stable
// Windows message values, not device IDs (mingw-w64 mmddk.h definitions).
constexpr UINT preferredDeviceQuery = 0x2000 + 21;
constexpr DWORD preferredDeviceOnly = 0x00000001;

constexpr std::size_t frameBytes = 640;
constexpr std::size_t frameSamples = frameBytes / 2;
constexpr std::size_t maxQueuedFrames = 8;
constexpr std::size_t maxRemotePeers = 4;
using Frame = std::array<unsigned char, frameBytes>;
using Clock = std::chrono::steady_clock;

template<class Bytes>
float pcmLevel(const Bytes& pcm) {
    if (pcm.size() < 2) return 0.0f;
    double squares = 0.0;
    std::size_t samples = 0;
    for (std::size_t offset = 0; offset + 1 < pcm.size(); offset += 2) {
        const auto bits = static_cast<std::uint16_t>(pcm[offset]) |
            (static_cast<std::uint16_t>(pcm[offset + 1]) << 8);
        const auto sample = static_cast<std::int16_t>(bits);
        const double normalized = static_cast<double>(sample) / 32768.0;
        squares += normalized * normalized;
        ++samples;
    }
    return samples ? static_cast<float>(std::sqrt(squares / samples)) : 0.0f;
}

float decayedLevel(float level, Clock::time_point updated, Clock::time_point now) {
    if (level <= 0.0f || updated == Clock::time_point{}) return 0.0f;
    const auto seconds = std::chrono::duration<double>(now - updated).count();
    return std::max(0.0f, std::min(1.0f,
        static_cast<float>(level * std::exp(-6.0 * std::max(0.0, seconds)))));
}

template<class Bytes>
void scalePcm(Bytes& pcm, float gain) {
    gain = std::max(0.0f, std::min(2.0f, gain));
    for (std::size_t offset = 0; offset + 1 < pcm.size(); offset += 2) {
        const int raw = static_cast<int>(pcm[offset]) | (static_cast<int>(pcm[offset + 1]) << 8);
        const int sample = raw >= 32768 ? raw - 65536 : raw;
        const int scaled = std::max(-32768, std::min(32767, static_cast<int>(sample * gain)));
        const unsigned bits = static_cast<unsigned>(scaled) & 0xffffu;
        pcm[offset] = static_cast<unsigned char>(bits & 0xffu);
        pcm[offset + 1] = static_cast<unsigned char>((bits >> 8) & 0xffu);
    }
}

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

std::vector<UINT> deviceCandidates(int selected, bool input) {
    std::vector<UINT> result;
    auto add = [&](UINT id) {
        if (std::find(result.begin(), result.end(), id) == result.end()) result.push_back(id);
    };
    if (selected >= 0) add(static_cast<UINT>(selected));
    add(WAVE_MAPPER);
    const UINT count = input ? waveInGetNumDevs() : waveOutGetNumDevs();
    for (UINT id = 0; id < count; ++id) add(id);
    return result;
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

std::vector<UINT> captureCandidates(int requested, const std::vector<Audio::Device>& devices,
                                    UINT preferred, bool preferredOnly) {
    std::vector<UINT> result;
    auto add = [&](UINT id) {
        for (const auto& device : devices)
            if (device.id >= 0 && static_cast<UINT>(device.id) == id &&
                std::find(result.begin(), result.end(), id) == result.end()) result.push_back(id);
    };
    // An explicit selection never silently switches to a different microphone.
    if (requested >= 0) { add(static_cast<UINT>(requested)); return result; }
    add(preferred);
    if (!preferredOnly) for (const auto& device : devices) if (device.id >= 0) add(device.id);
    return result;
}

MMRESULT openCapture(HWAVEIN& handle, int requested, HANDLE event, WAVEFORMATEX& format,
                     std::wstring& diagnostics) {
    const auto devices = Audio::inputDevices();
    auto name = [&](int id) { for (const auto& device : devices) if (device.id == id) return device.name;
                            return std::wstring(L"unavailable"); };
    DWORD preferred = WAVE_MAPPER, flags = 0;
    const auto query = waveInMessage(reinterpret_cast<HWAVEIN>(static_cast<UINT_PTR>(WAVE_MAPPER)),
        preferredDeviceQuery, reinterpret_cast<DWORD_PTR>(&preferred), reinterpret_cast<DWORD_PTR>(&flags));
    if (query != MMSYSERR_NOERROR) { preferred = WAVE_MAPPER; flags = 0; }
    const auto candidates = captureCandidates(requested, devices, preferred,
        (flags & preferredDeviceOnly) != 0);
    std::wostringstream out;
    out << L"capture_requested_id=" << requested << L"\ncapture_requested_name=" << name(requested)
        << L"\ncapture_default_query_mmresult=" << query << L"\ncapture_default_winmm_id="
        << (preferred == WAVE_MAPPER ? -1 : static_cast<int>(preferred));
    for (const auto& device : devices) if (device.id >= 0 && out.tellp() < 1200)
        out << L"\nwinmm_capture_device=" << device.id << L"; name=" << device.name;
    MMRESULT result = MMSYSERR_BADDEVICEID;
    int resolved = -1;
    bool fallback = false;
    for (UINT device : candidates) {
        resolved = static_cast<int>(device);
        fallback = requested < 0 && device != preferred;
        handle = nullptr;
        result = waveInOpen(&handle, device, &format, reinterpret_cast<DWORD_PTR>(event), 0, CALLBACK_EVENT);
        if (out.tellp() < 2000) out << L"\ncapture_open_attempt=" << device << L"; mmresult=" << result;
        if (result == MMSYSERR_NOERROR) break;
    }
    out << L"\ncapture_resolved_winmm_id=" << resolved << L"\ncapture_resolved_winmm_name=" << (resolved < 0 ? L"unresolved" : name(resolved))
        << L"\ncapture_waveinopen_mmresult=";
    if (candidates.empty()) out << L"not attempted (requested device unavailable)";
    else out << result;
    out << L"\ncapture_fallback_used=" << (fallback ? L"true" : L"false")
        << L"\ncapture_open_succeeded=" << (result == MMSYSERR_NOERROR ? L"true" : L"false");
    diagnostics = out.str();
    return result;
}

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
    std::function<void(const std::vector<unsigned char>&)> captureHandler;
    std::wstring pendingError;
    std::wstring captureDiagnostics = L"capture_waveinopen_mmresult=not attempted";
    std::wstring captureStage = L"idle", playbackDiagnostics;
    // -1 means this stage was not attempted; zero is a real successful call.
    long prepareResult = -1, queueResult = -1, startResult = -1;
    std::uint64_t inputWakes = 0, outputWakes = 0, completedBuffers = 0,
                  completedBytes = 0, deliveredFrames = 0, partialBuffers = 0;
    int inputDevice = -1, outputDevice = -1;
    float inputGain = 1.0f, outputGain = 1.0f;
    std::uint64_t inputGeneration = 0;
    float inputLevel = 0.0f, outputLevel = 0.0f;
    audio_detail::Clock::time_point inputLevelUpdated{}, outputLevelUpdated{};

    // Called with the lock held, never while a worker touches these headers.
    void closeInput() {
        ++inputGeneration;
        inputLevel = 0.0f;
        inputLevelUpdated = {};
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
        outputLevel = 0.0f;
        outputLevelUpdated = {};
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
            std::unique_lock<std::mutex> lock(mutex);
            if (!started) return;
            if (wait == WAIT_FAILED) {
                pendingError = L"Audio processing stopped because a Windows event failed.";
                closeInput();
                started = false;
                return;
            }
            if (wait == WAIT_OBJECT_0 + 1) ++inputWakes;
            if (wait == WAIT_OBJECT_0 + 2) ++outputWakes;
            // Consume ring buffers in submission order, including when several
            // finish between wakeups. Scanning from index zero would reorder PCM.
            for (std::size_t n = 0; input && n < inputBuffers.size(); ++n) {
                auto& buffer = inputBuffers[nextInput];
                if (!(buffer.header.dwFlags & WHDR_DONE)) break;
                ++completedBuffers;
                completedBytes += buffer.header.dwBytesRecorded;
                if (buffer.header.dwBytesRecorded != audio_detail::frameBytes) ++partialBuffers;
                if (buffer.header.dwBytesRecorded == audio_detail::frameBytes) {
                    const auto levelNow = audio_detail::Clock::now();
                    std::vector<unsigned char> frame(buffer.data.begin(), buffer.data.end());
                    audio_detail::scalePcm(frame, inputGain);
                    inputLevel = std::max(audio_detail::pcmLevel(frame),
                        audio_detail::decayedLevel(inputLevel, inputLevelUpdated, levelNow));
                    inputLevelUpdated = levelNow;
                    if (captured.size() == audio_detail::maxQueuedFrames) captured.pop_front();
                    captured.push_back(buffer.data);
                    auto handler = captureHandler;
                    const auto generation = inputGeneration;
                    if (handler) ++deliveredFrames;
                    lock.unlock();
                    if (handler) handler(frame);
                    lock.lock();
                    if (!started) return;
                    // Mute/reopen may run while the capture callback is outside
                    // the lock. Never requeue an old header on a new/closed device.
                    if (generation != inputGeneration) break;
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
                audio_detail::scalePcm(buffer.data, outputGain);
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
void Audio::setVolumes(float inputGain,float outputGain){std::lock_guard<std::mutex> lock(impl_->mutex);impl_->inputGain=std::max(0.0f,std::min(2.0f,inputGain));impl_->outputGain=std::max(0.0f,std::min(2.0f,outputGain));}

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
    state.captureStage = L"opening";
    state.prepareResult = state.queueResult = state.startResult = -1;
    state.inputWakes = state.outputWakes = state.completedBuffers = state.completedBytes = 0;
    state.deliveredFrames = state.partialBuffers = 0;
    state.stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    state.inputEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    state.outputEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!state.stopEvent || !state.inputEvent || !state.outputEvent) {
        error = L"Could not create Windows audio events.";
        state.closeDevices();
        return false;
    }
    WAVEFORMATEX format = audio_detail::pcmFormat();
    MMRESULT result = MMSYSERR_BADDEVICEID;

    // Open capture before playback. Bluetooth headsets commonly expose a
    // stereo playback endpoint and a separate hands-free duplex endpoint.
    // Opening stereo playback first can make the hands-free microphone fail
    // with the unhelpful MMSYSERR_ERROR result.
    std::wstring microphoneError;
    result = audio_detail::openCapture(state.input, state.inputDevice, state.inputEvent, format, state.captureDiagnostics);
    if (result == MMSYSERR_NOERROR) {
        state.captureStage = L"preparing";
        for (auto& buffer : state.inputBuffers) {
            buffer.initialize();
            result = state.prepareResult = waveInPrepareHeader(state.input, &buffer.header, sizeof(WAVEHDR));
            if (result == MMSYSERR_NOERROR)
                result = state.queueResult = waveInAddBuffer(state.input, &buffer.header, sizeof(WAVEHDR));
            if (result != MMSYSERR_NOERROR) break;
        }
        if (result == MMSYSERR_NOERROR) {
            state.captureStage = L"starting";
            result = state.startResult = waveInStart(state.input);
            if (result == MMSYSERR_NOERROR) state.captureStage = L"started";
        }
    }
    if (result != MMSYSERR_NOERROR) {
        microphoneError = audio_detail::deviceError((L"Microphone " + state.captureStage + L" failed").c_str(), result, true);
        state.captureStage += L" failed";
        state.closeInput();
    }

    state.playbackDiagnostics.clear();
    for (UINT device : audio_detail::deviceCandidates(state.outputDevice, false)) {
        result = waveOutOpen(&state.output, device, &format,
            reinterpret_cast<DWORD_PTR>(state.outputEvent), 0, CALLBACK_EVENT);
        if (state.playbackDiagnostics.size() < 700)
            state.playbackDiagnostics += L"\nplayback_open_attempt=" + std::to_wstring(static_cast<int>(device)) + L"; mmresult=" + std::to_wstring(result);
        if (result == MMSYSERR_NOERROR) break;
    }
    UINT outputId = WAVE_MAPPER;
    const auto outputIdResult = waveOutGetID(state.output, &outputId);
    WAVEOUTCAPSW outputCaps{};
    waveOutGetDevCapsW(outputId, &outputCaps, sizeof(outputCaps));
    state.playbackDiagnostics += L"\nplayback_resolved_id=" + std::to_wstring(static_cast<int>(outputId)) +
        L"\nplayback_resolved_name=" + outputCaps.szPname + L"\nplayback_get_id_mmresult=" + std::to_wstring(outputIdResult);
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
    // Playback remains usable if capture is unavailable. Report the mic error
    // while keeping receive-only audio active.
    error = std::move(microphoneError);
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
        state.captureStage = L"muted";
        return true;
    }
    if (!state.started) {
        error = L"Join a voice call before turning on your microphone.";
        return false;
    }
    if (state.input) return true;
    state.captureStage = L"opening";
    state.prepareResult = state.queueResult = state.startResult = -1;
    WAVEFORMATEX format = audio_detail::pcmFormat();
    MMRESULT result = MMSYSERR_BADDEVICEID;
    result = audio_detail::openCapture(state.input, state.inputDevice, state.inputEvent, format, state.captureDiagnostics);
    if (result != MMSYSERR_NOERROR) {
        error = audio_detail::deviceError(
            state.inputDevice >= 0 ? L"Could not open the selected microphone" :
                                     L"Could not open the default microphone",
            result, true);
        state.closeInput();
        state.captureStage = L"opening failed";
        return false;
    }
    for (auto& buffer : state.inputBuffers) {
        buffer.initialize();
        state.captureStage = L"preparing";
        result = state.prepareResult = waveInPrepareHeader(state.input, &buffer.header, sizeof(WAVEHDR));
        if (result == MMSYSERR_NOERROR)
            result = state.queueResult = waveInAddBuffer(state.input, &buffer.header, sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR) {
            error = audio_detail::deviceError(L"Could not prepare microphone capture", result, true);
            state.closeInput();
            return false;
        }
    }
    state.captureStage = L"starting";
    result = state.startResult = waveInStart(state.input);
    if (result != MMSYSERR_NOERROR) {
        error = audio_detail::deviceError(L"Could not start the microphone", result, true);
        state.closeInput();
        return false;
    }
    state.captureStage = L"started";
    return true;
}

void Audio::play(int sender, const std::vector<unsigned char>& pcm) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->started && impl_->output) {
        const auto now = audio_detail::Clock::now();
        impl_->outputLevel = std::max(audio_detail::pcmLevel(pcm),
            audio_detail::decayedLevel(impl_->outputLevel, impl_->outputLevelUpdated, now));
        impl_->outputLevelUpdated = now;
        impl_->mixer.push(sender, pcm, now);
    }
}

std::vector<std::vector<unsigned char>> Audio::takeCaptured() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<std::vector<unsigned char>> frames;
    frames.reserve(impl_->captured.size());
    for (const auto& frame : impl_->captured) frames.emplace_back(frame.begin(), frame.end());
    impl_->captured.clear();
    return frames;
}

void Audio::setCaptureHandler(std::function<void(const std::vector<unsigned char>&)> handler) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->captureHandler = std::move(handler);
}

void Audio::setDevices(int inputId, int outputId) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->started) { impl_->inputDevice=inputId;impl_->outputDevice=outputId; }
}

std::vector<Audio::Device> Audio::inputDevices() {
    std::vector<Device> devices{{-1,L"Windows default microphone"}};
    for(UINT id=0;id<waveInGetNumDevs();++id){WAVEINCAPSW caps{};if(waveInGetDevCapsW(id,&caps,sizeof(caps))==MMSYSERR_NOERROR)devices.push_back({static_cast<int>(id),caps.szPname});}
    return devices;
}

std::vector<Audio::Device> Audio::outputDevices() {
    std::vector<Device> devices{{-1,L"Windows default output"}};
    for(UINT id=0;id<waveOutGetNumDevs();++id){WAVEOUTCAPSW caps{};if(waveOutGetDevCapsW(id,&caps,sizeof(caps))==MMSYSERR_NOERROR)devices.push_back({static_cast<int>(id),caps.szPname});}
    return devices;
}

std::wstring Audio::takeError() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::wstring error;
    error.swap(impl_->pendingError);
    return error;
}

MediaTelemetry Audio::telemetry() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto now = audio_detail::Clock::now();
    MediaTelemetry result;
    result.inputLevel = impl_->input ? audio_detail::decayedLevel(impl_->inputLevel, impl_->inputLevelUpdated, now) : 0.0f;
    result.outputLevel = impl_->output ? audio_detail::decayedLevel(impl_->outputLevel, impl_->outputLevelUpdated, now) : 0.0f;
    result.inputAvailable = impl_->input != nullptr;
    result.outputAvailable = impl_->output != nullptr;
    result.active = impl_->started;
    return result;
}

std::wstring Audio::diagnostics() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto& state = *impl_;
    std::wostringstream out;
    out << state.captureDiagnostics << state.playbackDiagnostics
        << L"\ncapture_stage=" << state.captureStage << L"\naudio_worker_running=" << (state.started ? L"true" : L"false")
        << L"\ncapture_prepare_mmresult=" << state.prepareResult << L"\ncapture_queue_mmresult=" << state.queueResult
        << L"\ncapture_stage_result_note=-1 means not attempted"
        << L"\ncapture_start_mmresult=" << state.startResult
        << L"\ncapture_event_wakes=" << state.inputWakes << L"\nplayback_event_wakes=" << state.outputWakes
        << L"\ncapture_completed_buffers=" << state.completedBuffers << L"\ncapture_completed_bytes=" << state.completedBytes
        << L"\ncapture_partial_buffers=" << state.partialBuffers << L"\ncapture_handler_frames=" << state.deliveredFrames;
    const auto levelNow = audio_detail::Clock::now();
    out << L"\nmedia_input_level=" << (state.input ? audio_detail::decayedLevel(state.inputLevel,state.inputLevelUpdated,levelNow) : 0.0f)
        << L"\nmedia_output_level=" << (state.output ? audio_detail::decayedLevel(state.outputLevel,state.outputLevelUpdated,levelNow) : 0.0f)
        << L"\nmedia_input_available=" << (state.input ? L"true" : L"false")
        << L"\nmedia_output_available=" << (state.output ? L"true" : L"false");
    if (state.input) {
        // Read only when the user requests diagnostics; no polling or PCM reads.
        MMTIME position{}; position.wType = TIME_BYTES;
        const auto result = waveInGetPosition(state.input, &position, sizeof(position));
        out << L"\ncapture_position_mmresult=" << result << L"\ncapture_position_type=" << position.wType;
        if (result == MMSYSERR_NOERROR && position.wType == TIME_BYTES) out << L"\ncapture_position_bytes=" << position.u.cb;
        for (std::size_t i = 0; i < state.inputBuffers.size(); ++i) {
            const auto& header = state.inputBuffers[i].header;
            out << L"\ncapture_buffer_" << i << L"=flags:" << header.dwFlags << L"; recorded_bytes:" << header.dwBytesRecorded;
        }
        if (state.captureStage == L"started" && state.completedBuffers == 0)
            out << L"\ncapture_progress=started; no completed buffers observed";
    }
    return out.str();
}

} // namespace peerchat
