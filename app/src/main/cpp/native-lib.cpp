#include <jni.h>
#include <string>
#include "android/log.h"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG  , "ffmpeg.c", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR  , "ffmpeg.c", __VA_ARGS__)
extern "C" JNIEXPORT jstring JNICALL
Java_com_cmcc_iot_video_lib_util_VideoUtil_FFprobeExec(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "This method is out of use. If you want to call it, please call me.";
    return env->NewStringUTF(hello.c_str());
}

extern "C"{
#include "ffmpeg.h"
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_cmcc_iot_video_lib_util_VideoUtil_FFmpegExec(JNIEnv *env, jobject, jobjectArray cmd) {
    LOGD("AAA");
    int leng = env->GetArrayLength(cmd);
    LOGD("AAA");
    char *argv[leng];
    LOGD("AAA");
    for (int i = 0; i < leng; ++i) {
        argv[i] = (char *) env->GetStringUTFChars((jstring) env->GetObjectArrayElement(cmd, i),nullptr);
        LOGD("%s",argv[i]);
    }
    LOGD("AAA");
    return ffmpeg_exec(leng, argv);
}