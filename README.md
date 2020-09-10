# QmlAV
FFmpeg based real time QML player

### To use in your project, you must follow these steps:

1. Setup your project

```
QT += quick multimedia

HEADERS += \
    qmlav/src/qmlavaudioqueue.h \
    qmlav/src/qmlavdecoder.h \
    qmlav/src/qmlavdemuxer.h \
    qmlav/src/qmlavplayer.h \
    qmlav/src/qmlavformat.h \
    qmlav/src/qmlavframe.h \
    qmlav/src/qmlavutils.h

SOURCES += \
    qmlav/src/qmlavaudioqueue.cpp \
    qmlav/src/qmlavdecoder.cpp \
    qmlav/src/qmlavdemuxer.cpp \
    qmlav/src/qmlavplayer.cpp \
    qmlav/src/qmlavformat.cpp \
    qmlav/src/qmlavframe.cpp \
    qmlav/src/qmlavutils.cpp

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
        source: rtmp://example.ru/stream/name

        ffmpegFormatOptions: {
            'probesize': 500000,  // 500 KB
            'analyzeduration': 0  // 0 µs
        }
    }
}
```
