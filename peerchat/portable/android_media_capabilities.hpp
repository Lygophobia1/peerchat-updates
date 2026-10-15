#pragma once
namespace peerchat {
// A capability query does not acquire devices, permission, focus, or sockets.
bool androidVoiceAvailable();
const char* androidVoiceUnavailableReason();
}
