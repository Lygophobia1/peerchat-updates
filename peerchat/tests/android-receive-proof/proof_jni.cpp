#include <jni.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <fcntl.h>
#include <unistd.h>

int pc_receive_proof_main(int argc, char** argv);
extern "C" JNIEXPORT jint JNICALL
Java_com_peerchat_voiceproof_ProofActivity_runProof(JNIEnv* env, jclass, jstring path) {
    if (!path) return 90;
    const char* name = env->GetStringUTFChars(path, nullptr);
    if (!name) return 91;
    const std::string fixturePath=std::string(name)+".opus";
    int fd = open(name, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    env->ReleaseStringUTFChars(path, name);
    if (fd < 0) return 92;
    bool redirected = dup2(fd, STDOUT_FILENO) >= 0 && dup2(fd, STDERR_FILENO) >= 0;
    close(fd);
    if (!redirected) return 93;
    if(setenv("PC_RX_CODEC_OUTPUT",fixturePath.c_str(),1)!=0)return 94;
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    return pc_receive_proof_main(1, nullptr);
}
