#ifndef QMLAVDEMUXER_H
#define QMLAVDEMUXER_H

extern "C" {
#include <libavutil/time.h>
}

#include <QVideoFrame>
#include <QMediaPlayer>
#include <QVideoSurfaceFormat>
#include <QAudioOutput>

#include "qmlavmediacontextholder.h"
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
    QmlAVDemuxer(QObject *parent = nullptr);
    virtual ~QmlAVDemuxer();

    void load(const QUrl &url, const QmlAVOptions &avOptions);
    void start();

    const auto &clock() const { return m_context->clock; }
    int64_t startTime() const { return m_context->clock.startTime; }

    QVariantMap stat() const;

signals:
    void playbackStateChanged(QMediaPlayer::State state);
    void mediaStatusChanged(QMediaPlayer::MediaStatus status);
    void frameFinished(const std::shared_ptr<QmlAVFrame> frame);

protected:
    auto &context() { return m_context; }

    bool isRealTime(QUrl url) const;
    bool isLoaded() const { return m_context->videoDecoder->isOpen() || m_context->audioDecoder->isOpen(); }
    void initDecoders(const QmlAVOptions &avOptions);

    void frameHandler(const std::shared_ptr<QmlAVFrame> frame);
    
private:
    QmlAVInterruptCallback m_interruptCallback;

    QmlAVThreadLiveController<void> m_loaderThread;
    QmlAVThreadLiveController<QmlAVLoopController> m_demuxerThread;

    std::shared_ptr<QmlAVMediaContextHolder> m_context;

    friend class QmlAVDecoder;
    friend class QmlAVFrame;
};
Q_DECLARE_METATYPE(std::shared_ptr<QmlAVFrame>)

#endif // QMLAVDEMUXER_H
