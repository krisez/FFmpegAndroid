#include <jni.h>
#include <string>
#include "android_log.h"

extern "C" {
#include "ffprobe_exec.h"

JNIEXPORT jint JNICALL
Java_cn_krisez_vvv_FFmpegUtil_ffprobeExec(JNIEnv *env, jclass, jobjectArray cmd) {
    int leng = env->GetArrayLength(cmd);
    char *argv[leng];
    for (int i = 0; i < leng; ++i) {
        argv[i] = (char *) env->GetStringUTFChars((jstring) env->GetObjectArrayElement(cmd, i),nullptr);
    }
    LOGD("ASDASDADS");
    return ffprobe_exec(leng, argv);
}

}