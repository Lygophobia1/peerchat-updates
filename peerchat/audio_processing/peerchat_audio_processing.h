#ifndef PEERCHAT_AUDIO_PROCESSING_H_
#define PEERCHAT_AUDIO_PROCESSING_H_

#include <stdint.h>

#if defined(_WIN32)
#if defined(PC_APM_BUILDING_DLL)
#define PC_APM_API __declspec(dllexport)
#else
#define PC_APM_API __declspec(dllimport)
#endif
#else
#define PC_APM_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PC_APM_SAMPLE_RATE_HZ = 16000,
    PC_APM_CHANNELS = 1,
    PC_APM_SAMPLES_PER_10MS = 160,
};

typedef enum pc_apm_result {
    PC_APM_OK = 0,
    PC_APM_INVALID_ARGUMENT = 1,
    PC_APM_UNSUPPORTED_FORMAT = 2,
    PC_APM_PROCESSING_FAILED = 3,
    PC_APM_INITIALIZATION_FAILED = 4,
} pc_apm_result;

typedef struct pc_apm_config {
    uint32_t struct_size;
    uint32_t sample_rate_hz;
    uint32_t channels;
    uint32_t noise_suppression_enabled;
    uint32_t echo_cancellation_enabled;
    uint32_t automatic_gain_enabled;
} pc_apm_config;

typedef struct pc_apm_stats {
    uint32_t struct_size;
    char webrtc_revision[41];
    uint32_t sample_rate_hz;
    uint32_t channels;
    uint32_t noise_suppression_enabled;
    uint32_t echo_cancellation_enabled;
    uint32_t automatic_gain_enabled;
    uint64_t capture_blocks_processed;
    uint64_t render_blocks_processed;
    uint64_t reset_count;
    uint32_t stream_delay_ms;
    uint32_t last_error;
} pc_apm_stats;

typedef struct pc_apm pc_apm;

PC_APM_API pc_apm* pc_apm_create(const pc_apm_config* config);
PC_APM_API void pc_apm_destroy(pc_apm* instance);
PC_APM_API pc_apm_result pc_apm_configure(pc_apm* instance,
                                          const pc_apm_config* config);
PC_APM_API pc_apm_result pc_apm_process_capture_10ms(
    pc_apm* instance,
    const int16_t* input,
    int16_t* output,
    uint32_t samples_per_channel);
PC_APM_API pc_apm_result pc_apm_process_render_10ms(
    pc_apm* instance,
    const int16_t* input,
    int16_t* output,
    uint32_t samples_per_channel);
PC_APM_API pc_apm_result pc_apm_set_stream_delay_ms(pc_apm* instance,
                                                     uint32_t delay_ms);
PC_APM_API pc_apm_result pc_apm_reset(pc_apm* instance);
PC_APM_API pc_apm_result pc_apm_get_stats(const pc_apm* instance,
                                           pc_apm_stats* stats);

#ifdef __cplusplus
}
#endif

#endif  // PEERCHAT_AUDIO_PROCESSING_H_
