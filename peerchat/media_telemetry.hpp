#pragma once

namespace peerchat {

// Small route-independent media snapshot. Values are normalized PCM levels in
// [0,1]; no samples, device handles, or transport-specific objects cross this
// boundary.
struct MediaTelemetry {
    float inputLevel = 0.0f;
    float outputLevel = 0.0f;
    bool inputAvailable = false;
    bool outputAvailable = false;
    bool active = false;
};

} // namespace peerchat
