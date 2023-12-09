#ifndef QMLAVDEMUXER_H
#define QMLAVDEMUXER_H

extern "C" {
#include <libavutil/time.h>
}

#include <QVideoFrame>
#include <QMediaPlayer>
#include <QVideoSurfaceFormat>
#include <QAudioOutput>

#include "qmlavoptions.h"
#include "qmlavthread.h"
#include "qmlavdecoder.h"

class QmlAVInterruptCallback : public AVIOInterruptCB
{
public:
    QmlAVInterruptCallback() {
        opaque = this;
        callback = [](void *opaque) -> int {
            assert(opaque);
            auto cb = static_cast<QmlAVInterruptCallback *>(opaque);
            return cb->isAVInterruptRequested() || (cb->m_expireTime > 0 && av_gettime_relative() > cb->m_expireTime);
        };
    }

    void requestAVInterrupt() { m_avInterruptRequested.store(true, std::memory_order_relaxed); }
    bool isAVInterruptRequested() const { return m_avInterruptRequested.load(std::memory_order_relaxed); }

    // NOTE: Not thread safe!
    void setTimeout(int64_t timeout) {
        m_timeout = timeout;
        resetTimer();
    }
    void resetTimer() {
        m_expireTime = av_gettime_relative() + m_timeout;
    }

private:
    int64_t m_timeout = 0;
    int64_t m_expireTime = 0;
    std::atomic<bool> m_avInterruptRequested = false;
};

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
    bool isRealtime(QUrl url) const;
    bool isLoaded() const { return m_videoDecoder->isOpen() || m_audioDecoder->isOpen(); }
    void initDecoders(const QmlAVOptions &avOptions);
    
private:
    bool m_realtime;
    AVFormatContext *m_avFormatCtx;
    QmlAVInterruptCallback m_interruptCallback;

    QmlAVThreadLiveController<void> m_loaderThread;
    QmlAVThreadLiveController<QmlAVLoopController> m_demuxerThread;

    // We need more control over the decoder's lifetime since it can be used in another thread
    std::shared_ptr<QmlAVVideoDecoder> m_videoDecoder;
    std::shared_ptr<QmlAVAudioDecoder> m_audioDecoder;
};

#endif // QMLAVDEMUXER_H
