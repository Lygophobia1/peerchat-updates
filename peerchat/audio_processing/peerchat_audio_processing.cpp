#include "peerchat_audio_processing.h"

#include <cstring>
#include <memory>
#include <mutex>

#include "api/audio/audio_processing.h"
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/environment/environment_factory.h"

namespace {

constexpr char kWebRtcRevision[] =
    "6cf6bcb585b31414aac02377df2bc8fec770d0e8";
constexpr uint32_t kMaximumStreamDelayMs = 2000;

bool IsValidConfig(const pc_apm_config* config) {
  return config != nullptr && config->struct_size == sizeof(pc_apm_config) &&
         config->sample_rate_hz == PC_APM_SAMPLE_RATE_HZ &&
         config->channels == PC_APM_CHANNELS;
}

webrtc::AudioProcessing::Config ToWebRtcConfig(const pc_apm_config& config) {
  webrtc::AudioProcessing::Config result;
  result.high_pass_filter.enabled = true;
  result.noise_suppression.enabled = config.noise_suppression_enabled != 0;
  result.noise_suppression.level =
      webrtc::AudioProcessing::Config::NoiseSuppression::kModerate;
  result.echo_canceller.enabled = config.echo_cancellation_enabled != 0;
  result.gain_controller1.enabled = config.automatic_gain_enabled != 0;
  // PeerChat has no analogue gain-control bridge. Keep any requested AGC
  // entirely inside the processor until such a bridge exists.
  result.gain_controller1.mode =
      webrtc::AudioProcessing::Config::GainController1::kAdaptiveDigital;
  return result;
}

pc_apm_result ToResult(int result) {
  return result == 0 ? PC_APM_OK : PC_APM_PROCESSING_FAILED;
}

}  // namespace

struct pc_apm {
  std::mutex mutex;
  webrtc::scoped_refptr<webrtc::AudioProcessing> processor;
  pc_apm_config config{};
  uint64_t capture_blocks_processed = 0;
  uint64_t render_blocks_processed = 0;
  uint64_t reset_count = 0;
  uint32_t stream_delay_ms = 0;
  uint32_t last_error = PC_APM_OK;
};

extern "C" PC_APM_API pc_apm* pc_apm_create(const pc_apm_config* config) {
  if (!IsValidConfig(config)) {
    return nullptr;
  }

  auto instance = std::make_unique<pc_apm>();
  instance->config = *config;
  const webrtc::Environment environment = webrtc::CreateEnvironment();
  instance->processor =
      webrtc::BuiltinAudioProcessingBuilder(ToWebRtcConfig(*config))
          .Build(environment);
  if (!instance->processor || instance->processor->Initialize() != 0) {
    return nullptr;
  }
  return instance.release();
}

extern "C" PC_APM_API void pc_apm_destroy(pc_apm* instance) {
  delete instance;
}

extern "C" PC_APM_API pc_apm_result pc_apm_configure(
    pc_apm* instance,
    const pc_apm_config* config) {
  if (instance == nullptr || !IsValidConfig(config)) {
    return PC_APM_INVALID_ARGUMENT;
  }

  std::lock_guard<std::mutex> lock(instance->mutex);
  instance->processor->ApplyConfig(ToWebRtcConfig(*config));
  instance->config = *config;
  instance->last_error = PC_APM_OK;
  return PC_APM_OK;
}

extern "C" PC_APM_API pc_apm_result pc_apm_process_capture_10ms(
    pc_apm* instance,
    const int16_t* input,
    int16_t* output,
    uint32_t samples_per_channel) {
  if (instance == nullptr || input == nullptr || output == nullptr ||
      samples_per_channel != PC_APM_SAMPLES_PER_10MS) {
    return PC_APM_INVALID_ARGUMENT;
  }

  std::lock_guard<std::mutex> lock(instance->mutex);
  const webrtc::StreamConfig format(PC_APM_SAMPLE_RATE_HZ, PC_APM_CHANNELS);
  const pc_apm_result result =
      ToResult(instance->processor->ProcessStream(input, format, format, output));
  instance->last_error = result;
  if (result == PC_APM_OK) {
    ++instance->capture_blocks_processed;
  }
  return result;
}

extern "C" PC_APM_API pc_apm_result pc_apm_process_render_10ms(
    pc_apm* instance,
    const int16_t* input,
    int16_t* output,
    uint32_t samples_per_channel) {
  if (instance == nullptr || input == nullptr || output == nullptr ||
      samples_per_channel != PC_APM_SAMPLES_PER_10MS) {
    return PC_APM_INVALID_ARGUMENT;
  }

  std::lock_guard<std::mutex> lock(instance->mutex);
  const webrtc::StreamConfig format(PC_APM_SAMPLE_RATE_HZ, PC_APM_CHANNELS);
  const pc_apm_result result = ToResult(
      instance->processor->ProcessReverseStream(input, format, format, output));
  instance->last_error = result;
  if (result == PC_APM_OK) {
    ++instance->render_blocks_processed;
  }
  return result;
}

extern "C" PC_APM_API pc_apm_result pc_apm_set_stream_delay_ms(
    pc_apm* instance,
    uint32_t delay_ms) {
  if (instance == nullptr || delay_ms > kMaximumStreamDelayMs) {
    return PC_APM_INVALID_ARGUMENT;
  }

  std::lock_guard<std::mutex> lock(instance->mutex);
  const pc_apm_result result =
      ToResult(instance->processor->set_stream_delay_ms(delay_ms));
  instance->last_error = result;
  if (result == PC_APM_OK) {
    instance->stream_delay_ms = delay_ms;
  }
  return result;
}

extern "C" PC_APM_API pc_apm_result pc_apm_reset(pc_apm* instance) {
  if (instance == nullptr) {
    return PC_APM_INVALID_ARGUMENT;
  }

  std::lock_guard<std::mutex> lock(instance->mutex);
  const pc_apm_result result = ToResult(instance->processor->Initialize());
  instance->last_error = result;
  if (result == PC_APM_OK) {
    ++instance->reset_count;
  }
  return result;
}

extern "C" PC_APM_API pc_apm_result pc_apm_get_stats(const pc_apm* instance,
                                                        pc_apm_stats* stats) {
  if (instance == nullptr || stats == nullptr ||
      stats->struct_size != sizeof(pc_apm_stats)) {
    return PC_APM_INVALID_ARGUMENT;
  }

  auto* mutable_instance = const_cast<pc_apm*>(instance);
  std::lock_guard<std::mutex> lock(mutable_instance->mutex);
  std::memset(stats, 0, sizeof(*stats));
  stats->struct_size = sizeof(*stats);
  std::memcpy(stats->webrtc_revision, kWebRtcRevision, sizeof(kWebRtcRevision));
  stats->sample_rate_hz = PC_APM_SAMPLE_RATE_HZ;
  stats->channels = PC_APM_CHANNELS;
  stats->noise_suppression_enabled = instance->config.noise_suppression_enabled;
  stats->echo_cancellation_enabled =
      instance->config.echo_cancellation_enabled;
  stats->automatic_gain_enabled = instance->config.automatic_gain_enabled;
  stats->capture_blocks_processed = instance->capture_blocks_processed;
  stats->render_blocks_processed = instance->render_blocks_processed;
  stats->reset_count = instance->reset_count;
  stats->stream_delay_ms = instance->stream_delay_ms;
  stats->last_error = instance->last_error;
  return PC_APM_OK;
}
