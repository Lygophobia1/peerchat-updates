#include "screen_share_manager.hpp"
#include "windows_media/peerchat_screen_media.h"
#include <windows.h>
#include <cassert>
#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    using namespace peerchat::screen;
    assert(negotiateBackend(Backend::NativeWindows,Backend::NativeWindows)==Backend::NativeWindows);
    assert(negotiateBackend(Backend::NativeWindows,Backend::WebRtcCompatibility)==Backend::WebRtcCompatibility);
    assert(negotiateBackend(Backend::WebRtcCompatibility,Backend::NativeWindows)==Backend::WebRtcCompatibility);
    assert(negotiateBackend(Backend::WebRtcCompatibility,Backend::WebRtcCompatibility)==Backend::WebRtcCompatibility);
    ScreenShareManager manager;
    assert(manager.start("first", "peer", true, State::Offering, 100));
    assert(manager.active()->backend == Backend::NativeWindows);
    manager.restart(200);
    assert(manager.active()->backend == Backend::NativeWindows);
    manager.clear();
    assert(manager.start("second", "peer", true, State::Offering, 100,
                         Backend::WebRtcCompatibility));
    assert(manager.active()->backend == Backend::WebRtcCompatibility);
    assert(manager.active()->localRequirement == Backend::WebRtcCompatibility);
    assert(!manager.active()->backendNegotiated);
    manager.restart(200);
    assert(manager.active()->backend == Backend::WebRtcCompatibility);
    manager.clear();
    assert(manager.start("third", "peer", false, State::Incoming, 100));
    assert(manager.active()->backend == Backend::NativeWindows);

    if (argc > 1) {
        HMODULE module = LoadLibraryA(argv[1]);
        assert(module);
        auto address = GetProcAddress(module, "pc_screen_query_capabilities");
        decltype(&pc_screen_query_capabilities) query = nullptr;
        static_assert(sizeof(query) == sizeof(address));
        std::memcpy(&query, &address, sizeof(query));
        assert(query);
        pc_screen_capabilities capabilities{};
        capabilities.structure_size = sizeof(capabilities);
        assert(query(&capabilities));
        std::cout << "native_h264=" << ((capabilities.hardware_encoder_codecs & PC_SCREEN_H264) != 0)
                  << " compatibility_h264=" << ((capabilities.software_encoder_codecs & PC_SCREEN_H264) != 0)
                  << " decoder_h264=" << ((capabilities.decoder_codecs & PC_SCREEN_H264) != 0) << '\n';
        FreeLibrary(module);
    }
    std::cout << "PASS: backend defaults, per-share selection and recovery snapshot\n";
}
