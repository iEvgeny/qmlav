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

class QmlAVDecoder;

class QmlAVDecoderWorker : public QObject
{
    Q_OBJECT

public:
    QmlAVDecoderWorker(QmlAVDecoder *decoderCtx, QObject *parent = nullptr);
    virtual ~QmlAVDecoderWorker();

public slots:
    void decodeAVPacket(AVPacket avPacket);

signals:
    void frameFinished(const std::shared_ptr<QmlAVFrame> frame);

private:
    double framePts();
    qint64 frameStartTime();

private:
    QmlAVDecoder *m_decoderCtx;
    AVFrame *m_avFrame;
};

class QmlAVDecoder : public QObject
{
    Q_OBJECT

public:
    QmlAVDecoder(QmlAVDemuxer *parent);
    virtual ~QmlAVDecoder();

    bool openCodec(AVStream *stream);
    bool codecIsOpen() const;
    int streamIndex() const { return m_streamIndex; }
    double timeBase() const;
    qint64 startTime() const { return m_startTime; }
    void setStartTime(qint64 startTime) { m_startTime = startTime; }
    double clock() const;

    void decodeAVPacket(AVPacket &avPacket);

signals:
    void workerDecodeAVPacket(AVPacket avPacket);

protected:
    virtual std::shared_ptr<QmlAVFrame> frame(AVFrame *avFrame, qint64 startTime) const = 0;

private:
    QThread m_thread;
    QmlAVDecoderWorker *m_worker;

    int m_streamIndex;
    AVCodecContext *m_avCodecCtx;  // TODO: Check thread safety

    qint64 m_startTime;
    qint64 m_startPts;
    double m_timeBase;
    std::atomic<double> m_ptsClock; // Equivalent to the PTS of the current frame

    friend class QmlAVDecoderWorker;
    friend class QmlAVVideoDecoder;
    friend class QmlAVAudioDecoder;
};
Q_DECLARE_METATYPE(std::shared_ptr<QmlAVFrame>)

class QmlAVVideoDecoder : public QmlAVDecoder
{
    Q_OBJECT

public:
    QmlAVVideoDecoder(QmlAVDemuxer *parent);

    void setSupportedPixelFormats(const QList<QVideoFrame::PixelFormat> &formats);
    QVideoSurfaceFormat videoFormat() const;

protected:
    QSize pixelAspectRatio() const;
    virtual std::shared_ptr<QmlAVFrame> frame(AVFrame *avFrame, qint64 startTime) const override;

private:
    QVideoFrame::PixelFormat m_surfacePixelFormat;
};

class QmlAVAudioDecoder : public QmlAVDecoder
{
    Q_OBJECT

public:
    QmlAVAudioDecoder(QmlAVDemuxer *parent);

    QAudioFormat audioFormat() const;

protected:
    virtual std::shared_ptr<QmlAVFrame> frame(AVFrame *avFrame, qint64 startTime) const override;
};

#endif // QMLAVDECODER_H
