#ifndef QMLAVDEMUXER_H
#define QMLAVDEMUXER_H

#include <QVideoFrame>
#include <QMediaPlayer>
#include <QVideoSurfaceFormat>
#include <QAudioOutput>

#include "qmlavoptions.h"
#include "qmlavthread.h"
#include "qmlavdecoder.h"

// NOTE: Public API for GUI thread only!
class QmlAVDemuxer : public QObject
{
    Q_OBJECT

public:
    explicit QmlAVDemuxer(QObject *parent = nullptr);
    virtual ~QmlAVDemuxer();

    void load(const QUrl &url, const QmlAVOptions &avOptions);
    void start();

    QVariantMap stat() const;

signals:
    void audioFormatChanged(const QAudioFormat &format);
    void playbackStateChanged(QMediaPlayer::State state);
    void mediaStatusChanged(QMediaPlayer::MediaStatus status);
    void frameFinished(const std::shared_ptr<QmlAVFrame> frame);

protected:
    void requestAVInterruption() { m_avInterruptionRequested.store(true, std::memory_order_relaxed); }
    bool isAVInterruptionRequested() const { return m_avInterruptionRequested.load(std::memory_order_relaxed); }

    bool isRealtime(QUrl url) const;
    bool isLoaded() const { return m_videoDecoder.isOpen() || m_audioDecoder.isOpen(); }
    void initDecoders(const QmlAVOptions &avOptions);
    
private:
    bool m_realtime;
    AVFormatContext *m_avFormatCtx;

    std::atomic<bool> m_avInterruptionRequested;
    QmlAVThreadLiveController<void> m_loaderThread;
    QmlAVThreadLiveController<QmlAVLoopController> m_demuxerThread;

    QmlAVVideoDecoder m_videoDecoder;
    QmlAVAudioDecoder m_audioDecoder;
};

#endif // QMLAVDEMUXER_H
