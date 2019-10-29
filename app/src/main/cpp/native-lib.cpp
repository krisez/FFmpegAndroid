#include <jni.h>
#include <string>
#include "android_log.h"

extern "C" {
#include "include/libavcodec/avcodec.h"

JNIEXPORT jstring JNICALL
Java_cn_krisez_vvv_FFmpegUtil_exec(JNIEnv *env, jclass, jobjectArray cmd) {
    int leng = env->GetArrayLength(cmd);
    char *argv[leng];
    for (int i = 0; i < leng; ++i) {
        argv[i] = reinterpret_cast<char *>(env->GetObjectArrayElement(cmd, i));
    }
    LOGD("ASDASDADS");
    /*if (ffmpeg_exec(leng, argv) == 1) {
        return env->NewStringUTF("SUCCESS");
    }*/
    return env->NewStringUTF(avcodec_configuration());
}

JNIEXPORT jstring JNICALL
Java_cn_krisez_vvv_FFmpegUtil_stringFromJNI(
        JNIEnv *env,
        jclass /* this */) {
/*    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());*/
    const char *s = "asdad";
    return env->NewStringUTF(s);
}

}
