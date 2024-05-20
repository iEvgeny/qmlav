#ifndef QMLAVDECODER_H
#define QMLAVDECODER_H

extern "C" {
#include <libavutil/time.h>
}

#include <QVideoSurfaceFormat>
#include <QAudioOutput>

#include "qmlavthread.h"

struct AVCodecContext;

class QmlAVOptions;
class QmlAVFrame;
class QmlAVHWOutput;

class QmlAVDecoder : public QObject, public std::enable_shared_from_this<QmlAVDecoder>
{
    Q_OBJECT

public:
    struct Clock {
        std::atomic_int64_t startTime = 0;
        QmlAVRelaxedAtomic<bool> realTime = true;
        std::atomic_int64_t currentPts = 0;

        static int64_t now() { return av_gettime_relative(); }
    };

    struct Counters {
        QmlAVRelaxedAtomic<uint32_t> packetsDecoded = 0;
        QmlAVRelaxedAtomic<uint32_t> framesDecoded = 0;
        QmlAVRelaxedAtomic<uint32_t> framesDiscarded = 0;
    };

    enum Type
    {
        TypeUnknown,
        TypeVideo,
        TypeAudio
    };

    QmlAVDecoder(Clock &clock, QObject *parent = nullptr, Type type = TypeUnknown);
    virtual ~QmlAVDecoder();

    Type type() const { return m_type; }
    QString typeName() const { return m_type == TypeVideo ? "Video" : "Audio"; }

    bool open(const AVStream *avStream, const QmlAVOptions &avOptions);
    bool isOpen() const;
    QString name() const;
    const AVStream *stream() const { return m_avStream; }
    int streamIndex() const { return m_avStream ? m_avStream->index : -1; }

    int64_t startTime() const;

    bool decodeAVPacket(const AVPacketPtr &avPacket);

    void waitForEmptyPacketQueue() { m_threadTask.argsQueue()->waitForEmpty(); }

    int packetQueueLength() const { return m_threadTask.argsQueue()->length(); }
    int frameQueueLength() const;

    double frameQueueLimit() const { return m_frameQueueLimit.limit(); }
    void setFrameQueueLimit(double limit) { m_frameQueueLimit.setLimit(limit); }

    auto &counters() { return m_counters; }
    const auto &counters() const { return m_counters; }

signals:
    void frameFinished(const std::shared_ptr<QmlAVFrame> frame);

protected:
    QmlAVLoopController worker(const AVPacketPtr &avPacket);

    virtual bool initVideoDecoder([[maybe_unused]] const QmlAVOptions &avOptions) { return true; }
    virtual const std::shared_ptr<QmlAVFrame> frame([[maybe_unused]] const AVFramePtr &avFrame) const {
        // NOTE: Cannot be pure virtual!
        // A stub method called on an early (static) binding when the destructor is executed.
        return {};
    }

protected:
    AVCodecContext *m_avCodecCtx;

private:
    Type m_type;
    Clock &m_clock;

    const AVStream *m_avStream;

    QmlAVThreadTask<decltype(&QmlAVDecoder::worker)> m_threadTask;
    QmlAVThreadLiveController<QmlAVLoopController> m_thread;

    QMLAVSoftLimit m_frameQueueLimit;

    Counters m_counters;
};
Q_DECLARE_METATYPE(std::shared_ptr<QmlAVFrame>)

class QmlAVVideoDecoder final : public QmlAVDecoder
{
    Q_OBJECT

public:
    QmlAVVideoDecoder(Clock &clock, QObject *parent = nullptr);
    ~QmlAVVideoDecoder() override;

    std::shared_ptr<QmlAVHWOutput> hwOutput() const { return m_hwOutput; }

protected:
    bool initVideoDecoder(const QmlAVOptions &avOptions) override;
    static AVPixelFormat negotiatePixelFormatCb(struct AVCodecContext *avCodecCtx, const AVPixelFormat *avCodecPixelFormats);
    const std::shared_ptr<QmlAVFrame> frame(const AVFramePtr &avFrame) const override;

private:
    std::shared_ptr<QmlAVHWOutput> m_hwOutput;

    friend class QmlAVDecoder;
};

class QmlAVAudioDecoder final : public QmlAVDecoder
{
    Q_OBJECT

public:
    QmlAVAudioDecoder(Clock &clock, QObject *parent = nullptr);

protected:
    std::shared_ptr<QmlAVFrame> const frame(const AVFramePtr &avFrame) const override;
};

#endif // QMLAVDECODER_H
