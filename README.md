[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

# QmlAV
FFmpeg-based real-time QML stream player

### To use in your project, you must follow these steps:

1. Get dependencies:

* For Debian (Qt5)

```
# apt-get install qtbase5-dev qtdeclarative5-dev qtmultimedia5-dev libva-dev libglx-dev libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev libavdevice-dev libgtest-dev
```

* For Debian (Qt6)

```
# apt-get install qt6-base-dev qt6-declarative-dev qt6-multimedia-dev libva-dev libglx-dev libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev libavdevice-dev libgtest-dev
```

* or for Android

```
$ cd 3rd/FFmpeg && git submodule update --init
$ export ANDROID_ABI=armeabi-v7a ANDROID_NDK_ROOT=~/Android/Sdk/ndk/21.1.6352462 && ../prebuild_ffmpeg.sh # Supported architectures: armeabi-v7a, arm64-v8a, x86, x86_64
```

2. Setup your project (Roughly. For details, see https://github.com/iEvgeny/cctv-viewer):

```
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Quick Multimedia)

add_subdirectory(qmlav)

if (ANDROID)
    add_library(${TARGET} SHARED ${PROJ_FILES} ${QMLAV_FILES})

    target_include_directories(${TARGET} PRIVATE "${CMAKE_SOURCE_DIR}/qmlav/3rd/FFmpeg/ffbuild/${ANDROID_ABI}/include")
    target_link_directories(${TARGET} PRIVATE "${CMAKE_SOURCE_DIR}/qmlav/3rd/FFmpeg/ffbuild/${ANDROID_ABI}/lib")
else()
    add_executable(${TARGET} ${PROJ_FILES} ${QMLAV_FILES})
endif()

target_include_directories(${TARGET} PRIVATE ${QMLAV_INCLUDE})

target_link_libraries(${TARGET} PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Quick Qt${QT_VERSION_MAJOR}::Multimedia avformat avcodec avutil swscale swresample avdevice)
```

3. Register the QML type before using:

```
#include "qmlavplayer.h"

qmlRegisterType<QmlAVPlayer>("QmlAV.Multimedia", 1, 0, "QmlAVPlayer");

...
```

4. And use this in your QML code:

* Qt5

```
import QtQuick 2.0
import QtMultimedia 5.0
import QmlAV.Multimedia 1.0

Item {
    VideoOutput {
        id: videoOutput

        source: qmlAvPlayer
        anchors.fill: parent
    }

    QmlAVPlayer {
        id: qmlAvPlayer

        autoPlay: true
        loops: MediaPlayer.Infinite
        source: rtmp://example.ru/stream/name

        avOptions: {
            'probesize': 500000,  // 500 KB
            'analyzeduration': 0  // 0 µs
        }
    }
}
```

* Qt6

```
import QtQuick
import QtMultimedia
import QmlAV.Multimedia 1.0

Item {
    VideoOutput {
        id: videoOutput

        anchors.fill: parent
    }

    QmlAVPlayer {
        id: qmlAvPlayer

        videoSink: videoOutput.videoSink
        autoPlay: true
        loops: MediaPlayer.Infinite
        source: rtmp://example.ru/stream/name

        avOptions: {
            'probesize': 500000,  // 500 KB
            'analyzeduration': 0  // 0 µs
        }
    }
}
```

For a detailed guide on migrating an existing Qt5 consumer project (using cctv-viewer as the reference), see [cctv-viewer-migration.md](cctv-viewer-migration.md).
