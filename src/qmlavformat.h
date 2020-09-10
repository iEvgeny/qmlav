#ifndef QMLAVFORMAT_H
#define QMLAVFORMAT_H

extern "C" {
    #include <libavformat/avformat.h>
}

#include <QtCore>
#include <QVideoFrame>
#include <QAudioOutput>

class QmlAVVideoFormat
{
public:
    static AVPixelFormat normalizeFFmpegPixelFormat(AVPixelFormat avPixelFormat);
    static QVideoFrame::PixelFormat pixelFormatFromFFmpegFormat(AVPixelFormat avPixelFormat);
    static AVPixelFormat ffmpegFormatFromPixelFormat(QVideoFrame::PixelFormat pixelFormat);
};

class QmlAVAudioFormat
{
public:
    static QAudioFormat::SampleType audioFormatFromFFmpegFormat(AVSampleFormat sampleFormat);
};


#endif // QMLAVFORMAT_H
