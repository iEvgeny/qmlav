# QmlAV
FFmpeg based real time QML player

### To use in your project, you must follow these steps:

1. Setup your project for CMake

```
find_package(Qt5 Multimedia REQUIRED)

add_subdirectory(qmlav)

find_library(AVFORMAT_LIBRARY avformat)
find_library(AVCODEC_LIBRARY avcodec)
find_library(AVUTIL_LIBRARY avutil)
find_library(AVSWSCALE_LIBRARY swscale)
find_library(AVSWRESAMPLE_LIBRARY swresample)
find_library(AVDEVICE_LIBRARY avdevice)

target_link_libraries(cctv-viewer
  PRIVATE ${AVFORMAT_LIBRARY} ${AVCODEC_LIBRARY} ${AVUTIL_LIBRARY} ${AVSWSCALE_LIBRARY} ${AVSWRESAMPLE_LIBRARY} ${AVDEVICE_LIBRARY})
```

or for QMake

```
QT += quick multimedia

include(qmlav/qmlav.pri)

LIBS += -lavcodec -lavdevice -lavformat -lavutil -lswresample -lswscale
```

2. Register the QML type before using

```
#include "qmlav/src/qmlavplayer.h"

qmlRegisterType<FFPlayer>("QmlAV.Multimedia", 1, 0, "QmlAVPlayer");

...
```

3. And use this in your QML code

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

        avFormatOptions: {
            'probesize': 500000,  // 500 KB
            'analyzeduration': 0  // 0 µs
        }
    }
}
```
