#ifndef QMLAVDECODER_H
#define QMLAVDECODER_H

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
}

#include <QtCore>
#include <QVideoSurfaceFormat>
#include <QAudioOutput>

#include "qmlavutils.h"
#include "qmlavframe.h"

class QmlAVDecoder : public QObject
{
    Q_OBJECT

public:
    QmlAVDecoder(QObject *parent = nullptr);
    virtual ~QmlAVDecoder();

    bool openCodec(AVStream *stream);
    void closeCodec();
    bool codecIsOpen() const;
    int streamIndex() const;
    qint64 clock() const;
    double timeBase() const;
    void setStartTime(qint64 startTime) { m_startTime = startTime; }
    qint64 frameStartTime();

    int decode(const AVPacket &packet);

signals:
    void frameFinished(const std::shared_ptr<QmlAVFrame> frame);

protected:
    qint64 startPts() const;
    double framePts();
    virtual std::shared_ptr<QmlAVFrame> frame() = 0;

private:
    double m_ptsClock; // Equivalent to the PTS of the current frame
    qint64 m_startTime;
    AVStream *m_avStream;
    AVCodecContext *m_avCodecCtx;
    AVFrame *m_avFrame;

    friend class QmlAVVideoDecoder;
    friend class QmlAVAudioDecoder;
};
Q_DECLARE_METATYPE(std::shared_ptr<QmlAVFrame>)

class QmlAVVideoDecoder : public QmlAVDecoder
{
    Q_OBJECT

public:
    QmlAVVideoDecoder(QObject *parent = nullptr);

    void setSupportedPixelFormats(const QList<QVideoFrame::PixelFormat> &formats);
    QVideoSurfaceFormat videoFormat() const;

protected:
    QSize pixelAspectRatio() const;
    virtual std::shared_ptr<QmlAVFrame> frame() override;

private:
    QVideoFrame::PixelFormat m_surfacePixelFormat;
};

class QmlAVAudioDecoder : public QmlAVDecoder
{
    Q_OBJECT

public:
    QmlAVAudioDecoder(QObject *parent = nullptr);

    QAudioFormat audioFormat();

protected:
    virtual std::shared_ptr<QmlAVFrame> frame() override;
};

#endif // QMLAVDECODER_H
