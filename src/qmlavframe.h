#ifndef QMLAVFRAME_H
#define QMLAVFRAME_H

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavutil/imgutils.h>
    #include <libswscale/swscale.h>
    #include <libswresample/swresample.h>
}

#include <memory>
#include <QtCore>
#include <QVideoFrame>

#include "qmlavformat.h"

class QmlAVFrame
{
public:
    enum Type
    {
        TypeUnknown,
        TypeVideo,
        TypeAudio
    };

    QmlAVFrame(qint64 startTime, QmlAVFrame::Type type = QmlAVFrame::TypeUnknown);
    virtual ~QmlAVFrame() {}

    virtual bool isValid() const = 0;
    virtual void fromAVFrame(AVFrame *avFrame) = 0;
    QmlAVFrame::Type type() const { return m_type; }
    qint64 startTime() const { return m_startTime; }

private:
    QmlAVFrame::Type m_type;
    qint64 m_startTime; // Absolute microsecond timestamp

    friend class QmlAVVideoFrame;
    friend class QmlAVAudioFrame;
};

class QmlAVVideoFrame : public QmlAVFrame
{
public:
    QmlAVVideoFrame(qint64 startTime);

    virtual bool isValid() const override;
    virtual void fromAVFrame(AVFrame *avFrame) override;
    QVideoFrame::PixelFormat pixelFormat() const { return m_pixelFormat; }
    void setPixelFormat(QVideoFrame::PixelFormat format) { m_pixelFormat = format; }
    QVideoFrame& toVideoFrame() { return m_videoFrame; }
    operator QVideoFrame&() { return m_videoFrame; }

private:
    QVideoFrame m_videoFrame;
    QVideoFrame::PixelFormat m_pixelFormat;
};

class QmlAVAudioFrame : public QmlAVFrame
{
public:
    QmlAVAudioFrame(qint64 startTime);
    virtual ~QmlAVAudioFrame();

    virtual bool isValid() const override;
    virtual void fromAVFrame(AVFrame *avFrame) override;
    QAudioFormat audioFormat() const { return m_audioFormat; }
    void setAudioFormat(const QAudioFormat &format) { m_audioFormat = format; }
    char *data() const { return reinterpret_cast<char*>(m_data); }
    int dataSize() const { return m_dataSize; }

private:
    uint8_t *m_data;
    int m_dataSize;
    QAudioFormat m_audioFormat;
};

#endif // QMLAVFRAME_H
