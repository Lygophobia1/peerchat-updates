// TEST ONLY: the Windows messaging JNI proof has no Android media backend.
// Do not simulate voice success or media packets.
#include "voice_media.hpp"
#include <stdexcept>

namespace peerchat {
struct VoiceMedia::Impl {};
namespace {
[[noreturn]] void unavailable(){throw std::runtime_error("Android voice media unavailable in Windows JNI messaging proof");}
}
VoiceMedia::VoiceMedia(MediaIceConfiguration, int, int, std::uint64_t, std::string){unavailable();}
VoiceMedia::VoiceMedia(MediaIceConfiguration, int, int, std::uint64_t, std::string, ProcessingOptions){unavailable();}
VoiceMedia::VoiceMedia(MediaIceConfiguration, int, int, std::uint64_t, std::string, ProcessingOptions, ReceiveBinding){unavailable();}
VoiceMedia::~VoiceMedia() = default;
bool VoiceMedia::startOffer(std::wstring&){unavailable();}
bool VoiceMedia::startAnswer(const std::string&, std::wstring&){unavailable();}
bool VoiceMedia::setAnswer(const std::string&, std::wstring&){unavailable();}
bool VoiceMedia::addCandidate(const std::string&, const std::string&, std::wstring&){unavailable();}
bool VoiceMedia::setMuted(bool, std::wstring&){unavailable();}
void VoiceMedia::setDeafened(bool){unavailable();}
void VoiceMedia::setVolumes(float,float){unavailable();}
MediaTelemetry VoiceMedia::telemetry(){unavailable();}
VoiceMedia::Route VoiceMedia::route(){unavailable();}
std::wstring VoiceMedia::diagnostics(){unavailable();}
std::vector<VoiceMedia::Event> VoiceMedia::poll(){unavailable();}
void VoiceMedia::stop(){}
}
