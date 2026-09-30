#include "windows_media/peerchat_screen_media.h"
#include <windows.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

template<class T> T import(HMODULE module, const char* name) {
    auto address = GetProcAddress(module, name);
    T result = nullptr;
    static_assert(sizeof(result) == sizeof(address));
    std::memcpy(&result, &address, sizeof(result));
    return result;
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    HMODULE module = LoadLibraryA(argv[1]);
    if (!module) return 3;
    std::cerr << "module_loaded\n";
    const auto enumerate = import<decltype(&pc_screen_enumerate_sources)>(module, "pc_screen_enumerate_sources");
    const auto open = import<decltype(&pc_screen_encoder_open)>(module, "pc_screen_encoder_open");
    const auto pop = import<decltype(&pc_screen_encoder_pop)>(module, "pc_screen_encoder_pop");
    const auto close = import<decltype(&pc_screen_encoder_close)>(module, "pc_screen_encoder_close");
    if (!enumerate || !open || !pop || !close) return 4;
    std::uint64_t monitor = 0;
    std::cerr << "enumerating\n";
    enumerate([](void* opaque, std::uint32_t kind, std::uint64_t handle, const wchar_t*,
                 std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) {
        if (kind == PC_SCREEN_MONITOR && !*static_cast<std::uint64_t*>(opaque))
            *static_cast<std::uint64_t*>(opaque) = handle;
    }, &monitor);
    if (!monitor) return 5;
    std::cerr << "monitor_selected\n";
    pc_screen_encoder_options options{};
    options.structure_size = sizeof(options);
    options.source_kind = PC_SCREEN_MONITOR;
    options.source_handle = monitor;
    options.frames_per_second = 15;
    options.bitrate_bits_per_second = 2000000;
    options.backend = PC_SCREEN_BACKEND_WEBRTC_COMPATIBILITY;
    auto* encoder = open(&options);
    std::cerr << "encoder_open_returned\n";
    if (!encoder) { std::cerr << "Compatibility encoder could not start\n"; return 6; }
    std::vector<std::uint8_t> bytes(8 * 1024 * 1024);
    std::uint32_t frames = 0, keyframes = 0, sps = 0, pps = 0, idr = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while (std::chrono::steady_clock::now() < deadline && frames < 3) {
        if (!frames) std::cerr << "polling\n";
        pc_screen_encoded_frame frame{};
        frame.structure_size = sizeof(frame);
        const int result = pop(encoder, bytes.data(), static_cast<std::uint32_t>(bytes.size()), &frame);
        if (result == 1) {
            ++frames;
            keyframes += frame.keyframe != 0;
            for (std::size_t index = 0; index + 4 < frame.length; ++index) {
                const bool longStart = bytes[index] == 0 && bytes[index+1] == 0 &&
                    bytes[index+2] == 0 && bytes[index+3] == 1;
                const bool shortStart = bytes[index] == 0 && bytes[index+1] == 0 && bytes[index+2] == 1;
                if (!longStart && !shortStart) continue;
                const auto type = bytes[index+(longStart ? 4 : 3)] & 31;
                if (type == 7) ++sps;
                else if (type == 8) ++pps;
                else if (type == 5) ++idr;
                index += longStart ? 3 : 2;
            }
            std::cerr << "frame=" << frames << '\n';
        }
        else if (result < 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::cerr << "closing\n";
    close(encoder);
    std::cerr << "closed\n";
    FreeLibrary(module);
    std::cout << "encoded_frames=" << frames << " keyframes=" << keyframes
              << " sps=" << sps << " pps=" << pps << " idr=" << idr << '\n';
    return frames >= 3 && keyframes && sps && pps && idr ? 0 : 7;
}
