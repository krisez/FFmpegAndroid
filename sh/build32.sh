export TMPDIR=`dirname $0`/tmpdir
NDK=/home/krisez/android-ndk-r21b
API=19
# arm aarch64 i686 x86_64
ARCH=arm
# armv7a aarch64 i686 x86_64
PLATFORM=armv7a
TARGET=$PLATFORM-linux-androideabi
PLATFORMDIR=$NDK/platforms/android-$API/arch-arm
TOOLCHAIN=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
SYSROOT=$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot
PREFIX=`dirname $0`/Android/$PLATFORM

CFLAG="-D__ANDROID_API__=$API -U_FILE_OFFSET_BITS -DBIONIC_IOCTL_NO_SIGNEDNESS_OVERLOAD -Os -fPIC -DANDROID -D__thumb__ -mthumb -Wfatal-errors -Wno-deprecated -mfloat-abi=softfp -marm -Wall -O2 -U_FORTIFY_SOURCE -fstack-protector-all"
 
mkdir -p $TMPDIR

build_one()
{
./configure \
--prefix=$PREFIX \
--cc=$TOOLCHAIN/$TARGET$API-clang \
--cxx=$TOOLCHAIN/$TARGET$API-clang++ \
--ld=$TOOLCHAIN/$TARGET$API-clang \
--target-os=android \
--enable-jni \
--arch=$ARCH \
--cpu=$PLATFORM \
--cross-prefix=$TOOLCHAIN/$ARCH-linux-androideabi- \
--enable-cross-compile \
--disable-shared \
--enable-static \
--enable-runtime-cpudetect \
--disable-doc \
--disable-htmlpages \
--disable-manpages \
--disable-podpages \
--disable-txtpages \
--disable-ffmpeg \
--disable-ffplay \
--disable-ffprobe \
--disable-doc \
--disable-symver \
--enable-small \
--disable-gpl --disable-nonfree \
--enable-neon --disable-hwaccels \
--disable-avdevice  \
--disable-postproc \
--sysroot=$SYSROOT \
--extra-cflags="-Os -fpic -march=arm -Wall -O2 -U_FORTIFY_SOURCE -fstack-protector-all" \
--extra-ldflags="" 

}

builda(){
echo "开始编译ffmpeg so"

$TOOLCHAIN/arm-linux-androideabi-ld \
--rpath-link=$PLATFORMDIR/usr/lib \
-L $PLATFORMDIR/usr/lib \
-L $PREFIX \
-soname libiovffmpeg.so -shared -nostdlib -Bsymbolic --whole-archive --no-undefined \
-o $PREFIX/libiovffmpeg.so \
$PREFIX/lib/libavcodec.a \
$PREFIX/lib/libavfilter.a \
$PREFIX/lib/libswresample.a \
$PREFIX/lib/libavformat.a \
$PREFIX/lib/libavutil.a \
$PREFIX/lib/libswscale.a \
-lc -lm -lz -ldl -llog --dynamic-linker=/system/bin/linker \
$TOOLCHAIN/../lib/gcc/arm-linux-androideabi/4.9.x/libgcc_real.a
#$TOOLCHAIN/arm-linux-androideabi-strip $PREFIX/libffmpeg.so

echo "----end----"
}

build_one

make clean
make -j4
make install

builda

