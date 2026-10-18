// Test-host adaptation only: QEMU user mode has no Android log daemon. Preserve
// WebRTC diagnostic/fatal log text on stderr; fatal checks still abort in the
// unchanged upstream code. No audio, clock or codec functions are substituted.
#include <android/log.h>
#include <cstdarg>
#include <cstdio>
extern "C" int __android_log_print(int priority,const char* tag,const char* format,...) {
    std::fprintf(stderr,"android-log priority=%d tag=%s: ",priority,tag?tag:"");
    va_list args;va_start(args,format);int result=std::vfprintf(stderr,format,args);va_end(args);
    std::fputc('\n',stderr);return result;
}
