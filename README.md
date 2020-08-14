# QmlAV
FFmpeg based real time QML player

### To use in your project, you must follow these steps:

1. Setup your project

```
QT += quick multimedia

HEADERS += \
    qmlav/src/audioqueue.h \
    qmlav/src/decoder.h \
    qmlav/src/demuxer.h \
    qmlav/src/ffplayer.h \
    qmlav/src/format.h \
    qmlav/src/frame.h

SOURCES += \
    qmlav/src/audioqueue.cpp \
    qmlav/src/decoder.cpp \
    qmlav/src/demuxer.cpp \
    qmlav/src/ffplayer.cpp \
    qmlav/src/format.cpp \
    qmlav/src/frame.cpp

LIBS += -lavcodec -lavdevice -lavformat -lavutil -lswresample -lswscale
```

2. Register the QML type before using

```
#include "ffplayer.h"

qmlRegisterType<FFPlayer>("QmlAV.Multimedia", 1, 0, "FFPlayer");

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

        source: ffPlayer
        anchors.fill: parent
    }

    FFPlayer {
        id: ffPlayer

        autoPlay: true
        source: rtmp://example.ru/stream/name

        ffmpegFormatOptions: {
            'probesize': 500000,  // 500 KB
            'analyzeduration': 0  // 0 µs
        }
    }
}
```
