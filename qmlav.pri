#DEFINES += NO_DEBUG

INCLUDEPATH += $$PWD/src

HEADERS += \
    $$PWD/src/qmlavaudioqueue.h \
    $$PWD/src/qmlavdecoder.h \
    $$PWD/src/qmlavdemuxer.h \
    $$PWD/src/qmlavformat.h \
    $$PWD/src/qmlavframe.h \
    $$PWD/src/qmlavplayer.h \
    $$PWD/src/qmlavutils.h

SOURCES += \
    $$PWD/src/qmlavaudioqueue.cpp \
    $$PWD/src/qmlavdecoder.cpp \
    $$PWD/src/qmlavdemuxer.cpp \
    $$PWD/src/qmlavformat.cpp \
    $$PWD/src/qmlavframe.cpp \
    $$PWD/src/qmlavplayer.cpp \
    $$PWD/src/qmlavutils.cpp
