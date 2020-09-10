#ifndef QMLAVDEMUXER_H
#define QMLAVDEMUXER_H

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/time.h>
    #include <libavdevice/avdevice.h>
}

#include <QtCore>
#include <QVideoFrame>
#include <QMediaPlayer>
#include <QVideoSurfaceFormat>
#include <QAudioOutput>

#include "qmlavdecoder.h"

class QmlAVDemuxer;

class QmlAVInterruptCallback : public AVIOInterruptCB
{
public:
    QmlAVInterruptCallback(qint64 timeout = 30000);

    void startTimer();
    void stopTimer() { m_timer.invalidate(); }
    void requestInterruption() { m_interruptionRequested = true; }

    static int handler(void* obj);

private:
    qint64 m_timeout;
    QElapsedTimer m_timer;

    std::atomic<bool> m_interruptionRequested;
};

class QmlAVDemuxer : public QObject
{
    Q_OBJECT

public:
    explicit QmlAVDemuxer(QObject *parent = nullptr);
    virtual ~QmlAVDemuxer();

    void requestInterruption();

public slots:
    void load(const QUrl &url, const QVariantMap &formatOptions);
    void setSupportedPixelFormats(const QList<QVideoFrame::PixelFormat> &formats);
    void run();
    void setHandledTime(qint64 time) { m_handledTime = time; }

signals:
    void videoFormatChanged(const QVideoSurfaceFormat &format);
    void audioFormatChanged(const QAudioFormat &format);
    void playbackStateChanged(const QMediaPlayer::State state);
    void statusChanged(const QMediaPlayer::MediaStatus status);
    void frameFinished(const std::shared_ptr<QmlAVFrame> frame);

protected:
    bool isRealtime(QUrl url);
    bool isInterruptionRequested() const;
    void setStatus(QMediaPlayer::MediaStatus status);
    void setPlaybackState(const QMediaPlayer::State state);
    bool findStreams();

private:
    bool m_realtime;
    qint64 m_lastTime;
    qint64 m_handledTime;
    AVFormatContext *m_formatCtx;
    QmlAVInterruptCallback m_interruptCallback;
    QList<AVStream*> m_videoStreams, m_audioStreams;
    AVPacket m_packet;
    QmlAVVideoDecoder m_videoDecoder;
    QmlAVAudioDecoder m_audioDecoder;

    QMediaPlayer::State m_playbackState;
    QMediaPlayer::MediaStatus m_status;

    std::atomic<bool> m_interruptionRequested;

    friend class QmlAVUtils;
};

#endif // QMLAVDEMUXER_H
