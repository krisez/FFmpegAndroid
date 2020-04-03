# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)

include_directories(
        ${CMAKE_SOURCE_DIR}/include/
        ${CMAKE_SOURCE_DIR}/
#        ${CMAKE_SOURCE_DIR}/ffprobe/
        ${CMAKE_SOURCE_DIR}/ffmpeg/
        D:/technology/ffmpeg-422
)
#定义全局 my_source_path 变量
file(GLOB my_source_ffmpeg
        ${CMAKE_SOURCE_DIR}/ffmpeg/*.cpp
        ${CMAKE_SOURCE_DIR}/ffmpeg/*.c
        )

# Creates and names a library, sets it as either STATICw
# or SHARED, and provides the relative paths to its source code.
# You can define multiple libraries, and CMake builds them for you.
# Gradle automatically packages shared libraries with your APK.

add_library( # Sets the name of the library.
        native-cmd
        # Sets the library as a shared library.
        SHARED
        # Provides a relative path to your source file(s).
        ${my_source_ffmpeg})

add_library(avcodec SHARED IMPORTED)
add_library(avdevice SHARED IMPORTED)
add_library(avfilter SHARED IMPORTED)
add_library(avformat SHARED IMPORTED)
add_library(avutil SHARED IMPORTED)
add_library(swresample SHARED IMPORTED)
add_library(swscale SHARED IMPORTED)
add_library(postproc SHARED IMPORTED)

set_target_properties(avcodec PROPERTIES IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libavcodec.so)
set_target_properties(avdevice PROPERTIES IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libavdevice.so)
set_target_properties(avfilter PROPERTIES IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libavfilter.so)
set_target_properties(avformat PROPERTIES IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libavformat.so)
set_target_properties(avutil PROPERTIES IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libavutil.so)
set_target_properties(swresample PROPERTIES IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libswresample.so)
set_target_properties(swscale PROPERTIES IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libswscale.so)
set_target_properties(postproc PROPERTIES IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libpostproc.so)

# Searches for a specified prebuilt library and stores the path as a
# variable. Because CMake includes system libraries in the search path by
# default, you only need to specify the name of the public NDK library
# you want to add. CMake verifies that the library exists before
# completing its build.

find_library( # Sets the name of the path variable.
        log-lib

        # Specifies the name of the NDK library that
        # you want CMake to locate.
        log)

# Specifies libraries CMake should link to your target library. You
# can link multiple libraries, such as libraries you define in this
# build script, prebuilt third-party libraries, or system libraries.

target_link_libraries( # Specifies the target library.
        native-cmd
        avcodec
        avdevice
        avfilter
        avformat
        avutil
        swresample
        swscale
        postproc
        # Links the target library to the log library
        # included in the NDK.
        ${log-lib})