#!/bin/bash
#

SOURCE=./FFmpeg

TARGET_OS=android

if [[ -z ${ANDROID_API_VERSION} ]]; then
    ANDROID_API_VERSION=28
fi
if [[ -z ${ANDROID_ABI} ]]; then
    ANDROID_ABI=armeabi-v7a
fi

# Available in QMake runtime variables
if [[ -z ${ANDROID_NDK_HOST} ]]; then
    ANDROID_NDK_HOST=linux-x86_64
fi
if [[ -z ${ANDROID_NDK_ROOT} ]]; then
    ANDROID_NDK_ROOT=~/Android/Sdk/ndk/21.3.6528147
fi

case ${ANDROID_ABI} in
    armeabi-v7a)
        ARCH=arm
        ;;
    arm64-v8a)
        ARCH=aarch64
        ;;
    x86)
        ARCH=i686
        ;;
    x86_64)
        ARCH=x86_64
        ;;
esac

CPU=${ARCH}
if [[ ${ARCH} == arm ]]; then
    EABI=eabi
    CPU=armv7a
fi

SYSROOT=${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/${ANDROID_NDK_HOST}/sysroot
CROSS_PREFIX=${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/${ANDROID_NDK_HOST}/bin/${ARCH}-linux-android${EABI}-
PREFIX=./ffbuild/${ANDROID_ABI}

if [[ -d "${SOURCE}" ]]; then
    cd "${SOURCE}"
fi

if [[ ! -d "${PREFIX}" ]]; then
    mkdir "${PREFIX}"
fi

./configure \
    --arch=${ARCH} \
    --cc=${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/${ANDROID_NDK_HOST}/bin/${CPU}-linux-android${EABI}${ANDROID_API_VERSION}-clang \
    --target-os=${TARGET_OS} \
    --sysroot="${SYSROOT}" \
    --cross-prefix="${CROSS_PREFIX}" \
    --prefix="${PREFIX}" \
    --enable-static \
    --enable-shared \
    --enable-cross-compile \
    --disable-asm \
    --disable-programs \
    --disable-doc

make clean
make -j$(nproc)
make install
