#include <jni.h>
#include <string>
#include "android_log.h"

extern "C" {
#include "include/libavcodec/avcodec.h"
#include "ffmpeg.h"

JNIEXPORT jint JNICALL
Java_cn_krisez_vvv_FFmpegUtil_exec(JNIEnv *env, jclass, jobjectArray cmd) {
    int leng = env->GetArrayLength(cmd);
    char *argv[leng];
    for (int i = 0; i < leng; ++i) {
        argv[i] = (char*) env->GetStringUTFChars((jstring) env->GetObjectArrayElement(cmd, i), nullptr);
    }
    LOGD("ASDASDADS");
    return ffmpeg_exec(leng,argv);
}

JNIEXPORT jstring JNICALL
Java_cn_krisez_vvv_FFmpegUtil_stringFromJNI(
        JNIEnv *env,
        jclass /* this */) {
    const char *s = "asdad";
    return env->NewStringUTF(s);
}

}
