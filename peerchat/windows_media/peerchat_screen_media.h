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

// Media mechanics only. The caller owns the share lifetime, peer, negotiation,
// transport and recovery policy. Frames are complete Annex-B H.264 access units.
typedef struct pc_screen_encoder pc_screen_encoder;

typedef struct pc_screen_encoder_options {
    uint32_t structure_size;
    uint32_t source_kind;
    uint64_t source_handle;
    uint32_t frames_per_second;
    uint32_t bitrate_bits_per_second;
    uint32_t capture_cursor;
} pc_screen_encoder_options;

typedef struct pc_screen_encoded_frame {
    uint32_t structure_size;
    uint32_t length;
    uint32_t width;
    uint32_t height;
    uint64_t timestamp_100ns;
    uint32_t keyframe;
} pc_screen_encoded_frame;

// Returns null on unsupported source/encoder or failed capture initialization.
// Pop returns 1 for a frame, 0 for no frame, -1 if output_capacity is too small,
// and -2 after a terminal media error. A bounded queue drops stale encoded frames.
PC_SCREEN_API pc_screen_encoder* __cdecl pc_screen_encoder_open(
    const pc_screen_encoder_options* options);
PC_SCREEN_API int __cdecl pc_screen_encoder_pop(pc_screen_encoder* encoder,
    uint8_t* output, uint32_t output_capacity, pc_screen_encoded_frame* frame);
PC_SCREEN_API void __cdecl pc_screen_encoder_close(pc_screen_encoder* encoder);

typedef struct pc_screen_encoder_status {
    uint32_t structure_size;
    uint64_t captured_frames;
    uint64_t submitted_to_encoder;
    uint64_t encoded_frames;
    uint64_t dropped_before_encode;
    uint64_t dropped_encoded_queue;
} pc_screen_encoder_status;
PC_SCREEN_API int __cdecl pc_screen_encoder_status_get(pc_screen_encoder* encoder,
    pc_screen_encoder_status* status);

typedef struct pc_screen_decoder pc_screen_decoder;

typedef struct pc_screen_decoder_options {
    uint32_t structure_size;
    uint64_t presentation_window;
    uint32_t width;
    uint32_t height;
    uint32_t frames_per_second;
} pc_screen_decoder_options;

typedef struct pc_screen_decoder_status {
    uint32_t structure_size;
    uint64_t accepted_frames;
    uint64_t decoded_frames;
    uint64_t presented_frames;
    uint32_t failed;
    uint32_t decoded_width;
    uint32_t decoded_height;
    uint64_t dropped_decode_queue;
    uint64_t decode_latency_microseconds_total;
    uint64_t decode_operations;
} pc_screen_decoder_status;

// Copies one complete Annex-B access unit into a bounded decoder queue. A full
// queue returns 0; the caller owns the resulting keyframe/recovery decision.
PC_SCREEN_API pc_screen_decoder* __cdecl pc_screen_decoder_open(
    const pc_screen_decoder_options* options);
PC_SCREEN_API int __cdecl pc_screen_decoder_push(pc_screen_decoder* decoder,
    const uint8_t* encoded, uint32_t length, uint64_t timestamp_100ns);
PC_SCREEN_API int __cdecl pc_screen_decoder_status_get(pc_screen_decoder* decoder,
    pc_screen_decoder_status* status);
PC_SCREEN_API void __cdecl pc_screen_decoder_close(pc_screen_decoder* decoder);

#ifdef __cplusplus
}
#endif
