#include "portable/android_media_capabilities.hpp"
#include <cstring>
#include <cstdio>
int main(){
#if defined(PEERCHAT_ANDROID_STATIC_RECEIVE) && defined(PEERCHAT_ANDROID_VOICE_ENABLED)
    if(!peerchat::androidVoiceAvailable()||std::strlen(peerchat::androidVoiceUnavailableReason()))return 1;
#else
    if(peerchat::androidVoiceAvailable()||!std::strlen(peerchat::androidVoiceUnavailableReason()))return 2;
#endif
    std::puts("PASS: capability requires genuine receive linkage and explicit application enablement");
}
