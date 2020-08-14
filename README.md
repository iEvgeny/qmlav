# QmlAV
FFmpeg based real time QML player

### To use in your project, you must follow these steps:

1. Setup your project

```
QT += quick multimedia

HEADERS += \
    audioqueue.h \
    decoder.h \
    demuxer.h \
    ffplayer.h \
    format.h \
    frame.h

SOURCES += \
    audioqueue.cpp \
    decoder.cpp \
    demuxer.cpp \
    ffplayer.cpp \
    format.cpp \
    frame.cpp
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
