#include "peerchat_audio_processing.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

pc_apm_config Config(bool ns, bool aec, bool agc) {
  pc_apm_config config{};
  config.struct_size = sizeof(config);
  config.sample_rate_hz = PC_APM_SAMPLE_RATE_HZ;
  config.channels = PC_APM_CHANNELS;
  config.noise_suppression_enabled = ns;
  config.echo_cancellation_enabled = aec;
  config.automatic_gain_enabled = agc;
  return config;
}

bool RunLifecycle() {
  const auto config = Config(true, true, true);
  pc_apm* apm = pc_apm_create(&config);
  if (!Check(apm != nullptr, "create")) {
    return false;
  }

  std::array<int16_t, PC_APM_SAMPLES_PER_10MS> input{};
  std::array<int16_t, PC_APM_SAMPLES_PER_10MS> output{};
  for (size_t index = 0; index < input.size(); ++index) {
    input[index] = static_cast<int16_t>((index * 197) % 28000 - 14000);
  }

  bool ok = true;
  ok &= Check(pc_apm_set_stream_delay_ms(apm, 40) == PC_APM_OK,
              "delay update");
  for (int iteration = 0; iteration < 120; ++iteration) {
    ok &= Check(pc_apm_process_render_10ms(apm, input.data(), output.data(),
                                           output.size()) == PC_APM_OK,
                "render block");
    ok &= Check(pc_apm_process_capture_10ms(apm, input.data(), output.data(),
                                            output.size()) == PC_APM_OK,
                "capture block");
  }
  ok &= Check(pc_apm_reset(apm) == PC_APM_OK, "reset");
  for (int iteration = 0; iteration < 40; ++iteration) {
    ok &= Check(pc_apm_process_capture_10ms(apm, input.data(), output.data(),
                                            output.size()) == PC_APM_OK,
                "capture after reset");
  }

  pc_apm_stats stats{};
  stats.struct_size = sizeof(stats);
  ok &= Check(pc_apm_get_stats(apm, &stats) == PC_APM_OK, "get stats");
  ok &= Check(std::strcmp(stats.webrtc_revision,
                          "6cf6bcb585b31414aac02377df2bc8fec770d0e8") == 0,
              "pinned revision");
  ok &= Check(stats.capture_blocks_processed == 160, "capture count");
  ok &= Check(stats.render_blocks_processed == 120, "render count");
  ok &= Check(stats.reset_count == 1, "reset count");
  ok &= Check(stats.stream_delay_ms == 40, "stored stream delay");
  ok &= Check(stats.noise_suppression_enabled == 1 &&
                  stats.echo_cancellation_enabled == 1 &&
                  stats.automatic_gain_enabled == 1,
              "configuration diagnostics");

  ok &= Check(pc_apm_process_capture_10ms(apm, input.data(), output.data(),
                                           159) == PC_APM_INVALID_ARGUMENT,
              "invalid block size");
  ok &= Check(pc_apm_process_capture_10ms(nullptr, input.data(), output.data(),
                                           output.size()) == PC_APM_INVALID_ARGUMENT,
              "null instance");
  ok &= Check(pc_apm_set_stream_delay_ms(apm, 2001) == PC_APM_INVALID_ARGUMENT,
              "invalid delay");

  pc_apm_destroy(apm);
  return ok;
}

}  // namespace

int main() {
  bool ok = true;
  auto invalid = Config(false, false, false);
  invalid.sample_rate_hz = 48000;
  ok &= Check(pc_apm_create(&invalid) == nullptr, "reject unsupported format");

  for (int iteration = 0; iteration < 25; ++iteration) {
    ok &= RunLifecycle();
  }

  if (!ok) {
    return 1;
  }
  std::cout << "PASS: PeerChat WebRTC APM C ABI lifecycle validation.\n";
  return 0;
}
