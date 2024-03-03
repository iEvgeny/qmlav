#ifndef QMLAVFORMAT_H
#define QMLAVFORMAT_H

extern "C" {
#include <libavformat/avformat.h>
}

#include <QtCore>
#include <QVideoFrame>
#include <QVideoSurfaceFormat>
#include <QAudioOutput>

class QmlAVPixelFormat
{
    struct PixelFormatMap {
        AVPixelFormat avFormat;
        QVideoFrame::PixelFormat format;
        bool qtNative = false;
    };

public:
    QmlAVPixelFormat(AVPixelFormat avPixelFormat = AV_PIX_FMT_NONE);
    QmlAVPixelFormat(int avPixelFormat);
    QmlAVPixelFormat(QVideoFrame::PixelFormat pixelFormat);

    bool isValid() const { return m_avPixelFormat != AV_PIX_FMT_NONE; }
    bool isHWAccel() const;
    bool isQtNative() const;
    QmlAVPixelFormat nearestQtNative() const;

    operator int() const { return m_avPixelFormat; }
    operator AVPixelFormat() const { return m_avPixelFormat; }
    operator QVideoFrame::PixelFormat() const { return pixelFormatFromAVFormat(m_avPixelFormat); }
    bool operator==(const QmlAVPixelFormat &other) const { return m_avPixelFormat == other.m_avPixelFormat; }
    bool operator!=(const QmlAVPixelFormat &other) const { return m_avPixelFormat != other.m_avPixelFormat; }

protected:
    AVPixelFormat normalize(AVPixelFormat avPixelFormat) const;
    QVideoFrame::PixelFormat pixelFormatFromAVFormat(AVPixelFormat avPixelFormat) const;
    AVPixelFormat avFormatFromPixelFormat(QVideoFrame::PixelFormat pixelFormat) const;

private:
    static const std::vector<QmlAVPixelFormat::PixelFormatMap> m_pixelFormatMap;
    AVPixelFormat m_avPixelFormat;
};

class QmlAVColorSpace
{
public:
    QmlAVColorSpace(AVColorSpace avColorSpace = AVCOL_SPC_UNSPECIFIED);

    operator AVColorSpace() const { return m_avColorSpace; }
    operator QVideoSurfaceFormat::YCbCrColorSpace() const;

private:
    AVColorSpace m_avColorSpace;
};

namespace QmlAVSampleFormat
{
QAudioFormat::SampleType audioFormatFromAVFormat(AVSampleFormat sampleFormat);
}

#ifndef QT_NO_DEBUG_STREAM
QDebug operator<<(QDebug dbg, const QmlAVPixelFormat &pixelFormat);
QDebug operator<<(QDebug dbg, const QmlAVColorSpace &colorSpace);
#endif

#endif // QMLAVFORMAT_H
