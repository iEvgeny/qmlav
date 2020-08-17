#ifndef QMLAVFORMAT_H
#define QMLAVFORMAT_H

#include <QtCore>
#include <QVideoFrame>
#include <QAudioOutput>

extern "C" {
    #include <libavformat/avformat.h>
}


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
