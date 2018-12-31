
export NDK=/Users/knox/Documents/envOrTool/android/android-ndk-r14b

export PLATFORM=$NDK/platforms/android-21/arch-arm

export TOOLCHAIN=$NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/darwin-x86_64

export CPU=armv7-a

export PREFIX=./android/$CPU

./configure \
    --prefix=$PREFIX \
    --target-os=android \
    --cross-prefix=$TOOLCHAIN/bin/arm-linux-androideabi- \
    --arch=arm \
    --cpu=armv7-a \
    --sysroot=$PLATFORM \
    --extra-cflags="-I$PLATFORM/use/include -fPIC -DANDROID -mfpu=neon -mfloat-abi=softfp " \
    --cc=$TOOLCHAIN/bin/arm-linux-androideabi-gcc \
    --nm=$TOOLCHAIN/bin/arm-linux-androideabi-nm \
    --enable-shared \
    --enable-runtime-cpudetect \
    --enable-gpl \
    --enable-small \
    --enable-cross-compile \
    --enable-asm \
    --enable-neon \
    --enable-jni \
    --enable-mediacodec \
    --enable-decoder=h264_mediacodec \
    --enable-hwaccel=h264_mediacodec \
    --disable-debug \
    --disable-static \
    --disable-doc \
    --disable-ffmpeg \
    --disable-ffplay \
    --disable-ffprobe \
    --disable-ffserver \
    --disable-postproc \
    --disable-avdevice \
    --disable-symver \
    --disable-stripping

