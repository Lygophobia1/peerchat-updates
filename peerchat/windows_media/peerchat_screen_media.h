#pragma once

#include <stdint.h>

#ifdef _WIN32
#define PC_SCREEN_API __declspec(dllexport)
#else
#define PC_SCREEN_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

// The handle is an ephemeral HMONITOR or HWND, not an identity or persisted preference.
// Names are borrowed only for the duration of the callback.
typedef void (__cdecl *pc_screen_source_callback)(void* context, uint32_t kind,
    uint64_t handle, const wchar_t* name, uint32_t width, uint32_t height,
    uint32_t refresh_numerator, uint32_t refresh_denominator);

enum { PC_SCREEN_MONITOR = 1, PC_SCREEN_WINDOW = 2 };
enum { PC_SCREEN_H264 = 1, PC_SCREEN_HEVC = 2, PC_SCREEN_AV1 = 4 };

typedef struct pc_screen_capabilities {
    uint32_t structure_size;
    uint32_t capture_supported;
    uint32_t hardware_encoder_codecs;
    uint32_t decoder_codecs;
    uint32_t d3d11_supported;
} pc_screen_capabilities;

// Returns the number of sources reported. No capture, media session, or policy is started.
PC_SCREEN_API uint32_t __cdecl pc_screen_enumerate_sources(
    pc_screen_source_callback callback, void* context);
PC_SCREEN_API int __cdecl pc_screen_query_capabilities(pc_screen_capabilities* capabilities);

#ifdef __cplusplus
}
#endif
