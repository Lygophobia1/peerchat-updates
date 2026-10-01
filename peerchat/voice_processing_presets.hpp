#pragma once

#include "audio_processing/peerchat_audio_processing.h"

namespace peerchat {

// Public-facing preset identities are stable; their WebRTC-specific tuning is
// centralized in audio_processing/peerchat_audio_processing.cpp.
enum class NoiseSuppressionStrength : unsigned {
    Low = PC_APM_NS_LOW,
    Medium = PC_APM_NS_MEDIUM,
    High = PC_APM_NS_HIGH,
};

enum class AutomaticGainStrength : unsigned {
    Gentle = PC_APM_AGC_GENTLE,
    Balanced = PC_APM_AGC_BALANCED,
    Strong = PC_APM_AGC_STRONG,
};

inline const wchar_t* noiseSuppressionStrengthName(NoiseSuppressionStrength value) {
    switch (value) {
        case NoiseSuppressionStrength::Low: return L"Low";
        case NoiseSuppressionStrength::Medium: return L"Balanced";
        case NoiseSuppressionStrength::High: return L"High";
    }
    return L"Balanced";
}

inline const wchar_t* automaticGainStrengthName(AutomaticGainStrength value) {
    switch (value) {
        case AutomaticGainStrength::Gentle: return L"Gentle";
        case AutomaticGainStrength::Balanced: return L"Balanced";
        case AutomaticGainStrength::Strong: return L"Strong";
    }
    return L"Balanced";
}

} // namespace peerchat
