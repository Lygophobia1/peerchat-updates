#include "../../portable/android_media_capabilities.hpp"
namespace peerchat {
bool androidVoiceAvailable(){
#if defined(PEERCHAT_ANDROID_STATIC_RECEIVE)
    return true;
#else
    return false;
#endif
}
const char* androidVoiceUnavailableReason(){
    return androidVoiceAvailable()?"":"Pinned Android WebRTC NetEq receive library has not been supplied";
}
}
