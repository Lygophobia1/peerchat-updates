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
// Requests the next usable encoder input to become an IDR/bootstrap point. This
// is best effort on hardware that exposes the standard Media Foundation control.
PC_SCREEN_API int __cdecl pc_screen_encoder_request_keyframe(pc_screen_encoder* encoder);
PC_SCREEN_API void __cdecl pc_screen_encoder_close(pc_screen_encoder* encoder);

typedef struct pc_screen_encoder_status {
    uint32_t structure_size;
    uint64_t captured_frames;
    uint64_t submitted_to_encoder;
    uint64_t encoded_frames;
    uint64_t dropped_before_encode;
    uint64_t dropped_encoded_queue;
    uint64_t capture_callback_count;
    uint64_t last_capture_callback_age_ms;
    uint32_t capture_callback_stage;
    uint64_t capture_callback_stage_elapsed_ms;
    uint32_t encoder_worker_stage;
    uint64_t encoder_worker_stage_elapsed_ms;
    uint32_t pending_capture_frame;
    uint32_t encoded_queue_depth;
    uint32_t encoder_input_ready;
    uint32_t failed;
    // Startup is intentionally separate from ICE/media state. The adapter
    // retains the last local stage/error until it is closed.
    uint32_t startup_stage;
    uint32_t startup_failure_operation;
    int32_t startup_failure_hresult;
    uint64_t keyframe_requests;
    uint64_t sps_nals;
    uint64_t pps_nals;
    uint64_t idr_nals;
    uint64_t non_idr_nals;
    // Bounded, non-secret encoder failure/configuration telemetry. HRESULTs
    // are reported only after a failure or media operation; names describe
    // local hardware/software and never include captured content.
    uint32_t failed_operation;
    int32_t last_hresult;
    int32_t process_input_hresult;
    int32_t process_output_hresult;
    uint64_t process_input_failures;
    uint64_t process_output_failures;
    // MF_E_TRANSFORM_STREAM_CHANGE is normal Media Foundation control flow,
    // not an encoder fault. These values describe the bounded H.264 output
    // type renegotiation and never include encoded screen data.
    uint64_t stream_change_count;
    uint64_t output_type_renegotiation_count;
    int32_t output_type_renegotiation_hresult;
    uint32_t output_type_width;
    uint32_t output_type_height;
    uint32_t output_type_frames_per_second;
    uint32_t cbr_control_supported;
    uint32_t cbr_control_applied;
    uint32_t low_latency_control_supported;
    uint32_t low_latency_control_applied;
    uint32_t force_keyframe_control_supported;
    uint32_t force_keyframe_control_applied;
    wchar_t encoder_name[128];
    wchar_t adapter_name[128];
    wchar_t output_type_name[64];
    uint64_t driver_version;
} pc_screen_encoder_status;
// Stages identify the current native operation; no per-frame content is logged.
enum {
    PC_SCREEN_CAPTURE_IDLE = 0, PC_SCREEN_CAPTURE_NEXT = 1,
    PC_SCREEN_CAPTURE_COPY = 2, PC_SCREEN_CAPTURE_QUEUE = 3,
    PC_SCREEN_CAPTURE_CREATE_TEXTURE = 4, PC_SCREEN_CAPTURE_GPU_LOCK = 5,
    PC_SCREEN_ENCODER_INITIALIZING = 1, PC_SCREEN_ENCODER_WAITING = 2,
    PC_SCREEN_ENCODER_CONVERT = 3, PC_SCREEN_ENCODER_INPUT = 4,
    PC_SCREEN_ENCODER_OUTPUT = 5, PC_SCREEN_ENCODER_STOPPING = 6,
    PC_SCREEN_ENCODER_STOPPED = 7,
    PC_SCREEN_ENCODER_CREATE_SURFACES = 8,
    PC_SCREEN_ENCODER_GPU_LOCK = 9,
    PC_SCREEN_ENCODER_VIDEO_BLT = 10,
    PC_SCREEN_ENCODER_CREATE_SAMPLE = 11,
    PC_SCREEN_ENCODER_CREATE_COPY_TEXTURE = 12,
    PC_SCREEN_ENCODER_COPY_CAPTURE = 13
};
enum {
    PC_SCREEN_START_NONE = 0,
    PC_SCREEN_START_SOURCE_VALIDATED = 1,
    PC_SCREEN_START_D3D_DEVICE_CREATED = 2,
    PC_SCREEN_START_D3D_BRIDGE_CREATED = 3,
    PC_SCREEN_START_WGC_ITEM_ACQUIRED = 4,
    PC_SCREEN_START_ENCODER_ENUMERATION = 5,
    PC_SCREEN_START_ENCODER_CREATED = 6,
    PC_SCREEN_START_FRAME_POOL_CREATED = 7,
    PC_SCREEN_START_CAPTURE_SESSION_CREATED = 8,
    PC_SCREEN_START_CALLBACK_REGISTERED = 9,
    PC_SCREEN_START_CAPTURE_STARTED = 10,
    PC_SCREEN_START_FIRST_CAPTURED = 11,
    PC_SCREEN_START_FIRST_ENCODER_INPUT = 12
};
enum {
    PC_SCREEN_ENCODER_FAILURE_NONE = 0,
    PC_SCREEN_ENCODER_FAILURE_MF_STARTUP = 1,
    PC_SCREEN_ENCODER_FAILURE_WGC = 2,
    PC_SCREEN_ENCODER_FAILURE_D3D11 = 3,
    PC_SCREEN_ENCODER_FAILURE_VIDEO_PROCESSOR = 4,
    PC_SCREEN_ENCODER_FAILURE_ENUMERATE_MFT = 5,
    PC_SCREEN_ENCODER_FAILURE_ACTIVATE_MFT = 6,
    PC_SCREEN_ENCODER_FAILURE_DXGI_MANAGER = 7,
    PC_SCREEN_ENCODER_FAILURE_MEDIA_TYPE = 8,
    PC_SCREEN_ENCODER_FAILURE_PROCESS_MESSAGE = 9,
    PC_SCREEN_ENCODER_FAILURE_PROCESS_INPUT = 10,
    PC_SCREEN_ENCODER_FAILURE_PROCESS_OUTPUT = 11,
    PC_SCREEN_ENCODER_FAILURE_CAPTURE = 12,
    PC_SCREEN_ENCODER_FAILURE_EXCEPTION = 13
};
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
    uint32_t queue_depth;
    uint32_t worker_stage;
    uint64_t worker_stage_elapsed_ms;
    uint32_t largest_access_unit_bytes;
    int32_t last_process_input_hresult;
    int32_t last_process_output_hresult;
    uint64_t process_input_failures;
    uint64_t process_output_failures;
    uint64_t stream_change_count;
    uint64_t need_more_input_count;
    uint64_t output_success_count;
} pc_screen_decoder_status;

// A snapshot of the decoder worker, not a separate recovery or lifecycle owner.
enum {
    PC_SCREEN_DECODER_INITIALIZING = 1,
    PC_SCREEN_DECODER_WAITING = 2,
    PC_SCREEN_DECODER_PROCESS_INPUT = 3,
    PC_SCREEN_DECODER_PROCESS_OUTPUT = 4,
    PC_SCREEN_DECODER_PRESENT_SETUP = 5,
    PC_SCREEN_DECODER_PRESENT_BLT = 6,
    PC_SCREEN_DECODER_PRESENT_SWAP = 7,
    PC_SCREEN_DECODER_STOPPING = 8,
    PC_SCREEN_DECODER_STOPPED = 9
};

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
