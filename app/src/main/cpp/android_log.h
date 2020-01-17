#ifdef ANDROID

#include <android/log.h>

#define LOGS 1 //1打开，0关闭

#ifndef LOG_TAG
#define  MY_TAG   "MYTAG"
#endif
#if LOGS
#define LOGE(format, ...)  __android_log_print(ANDROID_LOG_ERROR, MY_TAG, format, ##__VA_ARGS__)
#define LOGD(format, ...)  __android_log_print(ANDROID_LOG_DEBUG,  MY_TAG, format, ##__VA_ARGS__)
#else
#define LOGE(format, ...)
#define LOGD(format, ...)
#endif
#endif
