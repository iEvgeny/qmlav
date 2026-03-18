#ifndef QMLAVFORMAT_H
#define QMLAVFORMAT_H

extern "C" {
#include <libavformat/avformat.h>
}

#include "qmlavcompat.h"

class QmlAVPixelFormat
{
    struct PixelFormatMap {
        AVPixelFormat avFormat;
        QmlAVPixelFormatEnum format;
        bool qtNative = false;
    };

public:
    QmlAVPixelFormat(AVPixelFormat avPixelFormat = AV_PIX_FMT_NONE);
    QmlAVPixelFormat(int avPixelFormat);
    QmlAVPixelFormat(QmlAVPixelFormatEnum pixelFormat);

    bool isValid() const { return m_avPixelFormat != AV_PIX_FMT_NONE; }
    bool isHWAccel() const;
    bool isQtNative() const;
    QmlAVPixelFormat nearestQtNative() const;

    operator int() const { return m_avPixelFormat; }
    operator AVPixelFormat() const { return m_avPixelFormat; }
    operator QmlAVPixelFormatEnum() const { return pixelFormatFromAVFormat(m_avPixelFormat); }
    bool operator==(const QmlAVPixelFormat &other) const { return m_avPixelFormat == other.m_avPixelFormat; }
    bool operator!=(const QmlAVPixelFormat &other) const { return m_avPixelFormat != other.m_avPixelFormat; }

protected:
    AVPixelFormat normalize(AVPixelFormat avPixelFormat) const;
    QmlAVPixelFormatEnum pixelFormatFromAVFormat(AVPixelFormat avPixelFormat) const;
    AVPixelFormat avFormatFromPixelFormat(QmlAVPixelFormatEnum pixelFormat) const;

private:
    static const std::vector<QmlAVPixelFormat::PixelFormatMap> m_pixelFormatMap;
    AVPixelFormat m_avPixelFormat;
};

class QmlAVColorSpace
{
public:
    QmlAVColorSpace(AVColorSpace avColorSpace = AVCOL_SPC_UNSPECIFIED);

    operator AVColorSpace() const { return m_avColorSpace; }
    operator QmlAVColorSpaceEnum() const;

private:
    AVColorSpace m_avColorSpace;
};

namespace QmlAVSampleFormat
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
QAudioFormat::SampleFormat audioFormatFromAVFormat(AVSampleFormat sampleFormat);
#else
QAudioFormat::SampleType audioFormatFromAVFormat(AVSampleFormat sampleFormat);
#endif
}

#ifndef QT_NO_DEBUG_STREAM
QDebug operator<<(QDebug dbg, const QmlAVPixelFormat &pixelFormat);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
QDebug operator<<(QDebug dbg, const QmlAVColorSpace &colorSpace);
#endif
#endif

#endif // QMLAVFORMAT_H
