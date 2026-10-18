#include "../portable/voice_application.hpp"
#include <cassert>

int main(){
    using peerchat::portable::VoiceApplication;
    const auto safe=VoiceApplication::safeMediaDiagnostics(
        L"selected_ice_pair=10.0.0.1 -> 10.0.0.2\n"
        L"local_ice_candidate_summaries=candidate password\n"
        L"latest_media_error=TURN secret\n"
        L"neteq_packets_inserted=19\n"
        L"neteq_current_delay_ms=40\n"
        L"receive_mixed_10ms_blocks=23\n"
        L"perf_receive_pull=count:3;last_us:12;max_us:17;mean_us:14;deadline_misses:0\n"
        L"perf_receive_pull=count:bad;last_us:12;max_us:17;mean_us:14;deadline_misses:0\n"
        L"capture_frames=12-34\n");
    assert(safe=="neteq_packets_inserted=19\nneteq_current_delay_ms=40\nreceive_mixed_10ms_blocks=23\n"
        "perf_receive_pull=count:3;last_us:12;max_us:17;mean_us:14;deadline_misses:0\n");
    assert(VoiceApplication::safeMediaDiagnostics(L"selected_ice_pair=private") == "Media diagnostics unavailable");
    assert(VoiceApplication::safeMediaDiagnostics(L"capture_frames=\nperf_receive_pull=\n") == "Media diagnostics unavailable");
    assert(VoiceApplication::safeMediaDiagnostics(std::wstring(262145,L'x')) == "Media diagnostics unavailable");
}
