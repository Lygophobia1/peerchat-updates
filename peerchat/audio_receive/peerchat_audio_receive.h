#ifndef PEERCHAT_AUDIO_RECEIVE_H
#define PEERCHAT_AUDIO_RECEIVE_H
#include <stdint.h>
#ifdef PC_RECEIVE_BUILD
#define PC_RX_API __declspec(dllexport)
#else
#define PC_RX_API __declspec(dllimport)
#endif
#ifdef __cplusplus
extern "C" {
#endif
enum { PC_RX_OK=0, PC_RX_INVALID=1, PC_RX_BUSY=2, PC_RX_FAILED=3, PC_RX_LIMIT=4 };
// Config ABI layout is unchanged. Version 1 is the deterministic proof clock;
// version 2 uses WebRTC's real monotonic clock and device-paced Pull calls.
// For version 2, caller clock_ms/arrival_ms are not used as wall-clock authority.
enum { PC_RX_DETERMINISTIC=1, PC_RX_DEVICE_PACED=2 };
typedef uint64_t pc_rx_engine;
typedef uint64_t pc_rx_source;
typedef struct pc_rx_config { uint32_t size, version, max_sources, max_packets, max_delay_ms; } pc_rx_config;
typedef struct pc_rx_source_config { uint32_t size, version, ssrc, payload_type; uint64_t endpoint_generation; } pc_rx_source_config;
typedef struct pc_rx_packet { uint32_t size, version, ssrc, timestamp, sequence, payload_type; uint64_t endpoint_generation, arrival_ms; } pc_rx_packet;
typedef struct pc_rx_source_stats {
 uint32_t size, version, sample_rate, channels, rtp_clock_rate, current_delay_ms, target_delay_ms;
 uint64_t endpoint_generation, packets_inserted, insert_failures, pulled_blocks, pull_failures;
 uint64_t concealed_samples, concealment_events, fec_packets_received, fec_packets_discarded, packets_discarded;
 uint64_t accelerated_samples, decelerated_samples, buffer_flushes, playout_timestamp;
 uint32_t playout_timestamp_valid, last_frame_muted, secondary_decoded_rate_q14;
} pc_rx_source_stats;
typedef struct pc_rx_mixer_stats { uint32_t size, version, active_sources, sample_rate, channels; uint64_t mixed_blocks, clock_ms; } pc_rx_mixer_stats;
PC_RX_API int pc_rx_create(const pc_rx_config*, pc_rx_engine*);
PC_RX_API int pc_rx_destroy(pc_rx_engine);
PC_RX_API int pc_rx_source_add(pc_rx_engine,const pc_rx_source_config*,pc_rx_source*);
PC_RX_API int pc_rx_source_remove(pc_rx_engine,pc_rx_source);
PC_RX_API int pc_rx_source_reset(pc_rx_engine,pc_rx_source,const pc_rx_source_config*);
PC_RX_API int pc_rx_insert(pc_rx_engine,pc_rx_source,const pc_rx_packet*,const uint8_t*,uint32_t);
PC_RX_API int pc_rx_pull_10ms(pc_rx_engine,uint64_t clock_ms,int16_t*,uint32_t capacity);
PC_RX_API int pc_rx_get_source_stats(pc_rx_engine,pc_rx_source,pc_rx_source_stats*);
PC_RX_API int pc_rx_get_mixer_stats(pc_rx_engine,pc_rx_mixer_stats*);
#ifdef __cplusplus
}
#endif
#endif
