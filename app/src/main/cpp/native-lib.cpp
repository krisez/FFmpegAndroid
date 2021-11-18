#include <jni.h>
#include <string>
#include "android/log.h"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG  , "ffmpeg.c", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR  , "ffmpeg.c", __VA_ARGS__)
extern "C"{
#include "ffmpeg.h"
}
extern "C"
JNIEXPORT jint JNICALL
Java_cn_krisez_video_MainActivity_fff(JNIEnv *env, jobject, jobjectArray cmd) {
    int leng = env->GetArrayLength(cmd);
    char *argv[leng];
    for (int i = 0; i < leng; ++i) {
        argv[i] = (char *) env->GetStringUTFChars((jstring) env->GetObjectArrayElement(cmd, i),nullptr);
    }
    return ffmpeg_exec(leng, argv);
}

//cn.krisez.video
extern "C"
JNIEXPORT jstring JNICALL
Java_cn_krisez_video_MainActivity_stringFromJNI(JNIEnv *env, jobject thiz) {
    std::string hello = "Hello World";
    return env->NewStringUTF(hello.c_str());
}