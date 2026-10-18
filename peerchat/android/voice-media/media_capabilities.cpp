#include "../../portable/android_media_capabilities.hpp"
namespace peerchat {
bool androidVoiceAvailable(){
#if defined(PEERCHAT_ANDROID_STATIC_RECEIVE) && defined(PEERCHAT_ANDROID_VOICE_ENABLED)
    return true;
#else
    return false;
#endif
}
const char* androidVoiceUnavailableReason(){
    if(androidVoiceAvailable())return "";
#if defined(PEERCHAT_ANDROID_STATIC_RECEIVE)
    return "Android voice application integration is not enabled";
#else
    return "Pinned Android WebRTC NetEq receive library has not been supplied";
#endif
}
}
