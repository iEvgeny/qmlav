#ifndef QMLAVMEDIACONTEXTHOLDER_H
#define QMLAVMEDIACONTEXTHOLDER_H

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/time.h>
}

#include "qmlavdecoder.h"

class QmlAVDemuxer;

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


class QmlAVMediaContextHolder : public std::enable_shared_from_this<QmlAVMediaContextHolder>
{
public:
    QmlAVMediaContextHolder(QmlAVDemuxer *parent) : demuxer(parent) {
        avFormatCtx = avformat_alloc_context();

        avFormatCtx->interrupt_callback = interruptCallback;

        videoDecoder = new QmlAVVideoDecoder(this);
        audioDecoder = new QmlAVAudioDecoder(this);
    }
    virtual ~QmlAVMediaContextHolder() {
        delete videoDecoder;
        delete audioDecoder;

        avformat_close_input(&avFormatCtx);
    }

    AVFormatContext *avFormatCtx = nullptr;
    QmlAVInterruptCallback interruptCallback;
    QmlAVDecoder::Clock clock;

    // NOTE: Be careful! Life time is not directly controlled
    QmlAVDemuxer *demuxer = nullptr;

    QmlAVVideoDecoder *videoDecoder = nullptr;
    QmlAVAudioDecoder *audioDecoder = nullptr;
};

#endif // QMLAVMEDIACONTEXTHOLDER_H
