#ifndef QMLAVDECODER_H
#define QMLAVDECODER_H

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

    struct Counters {
        int framesDecoded() const { return m_framesDecoded.load(std::memory_order_relaxed); }
        void framesDecodedAdd() { m_framesDecoded.fetch_add(1, std::memory_order_relaxed); }

        int framesDiscarded() const { return m_framesDiscarded.load(std::memory_order_relaxed); }
        void framesDiscardedAdd() { m_framesDiscarded.fetch_add(1, std::memory_order_relaxed); }

        int frameQueueLength() const { return m_frameQueueLength.load(std::memory_order_relaxed); }
        void frameQueueLengthAdd() { m_frameQueueLength.fetch_add(1, std::memory_order_relaxed); }
        void frameQueueLengthSub() { m_frameQueueLength.fetch_sub(1, std::memory_order_relaxed); }

    private:
        std::atomic<int> m_framesDecoded = 0;
        std::atomic<int> m_framesDiscarded = 0;
        std::atomic<int> m_frameQueueLength = 0;
    };

public:
    QmlAVDecoder(QObject *parent = nullptr);
    virtual ~QmlAVDecoder();

    bool asyncMode() const { return m_asyncMode; }
    void setAsyncMode(bool async);
    bool open(const AVStream *avStream, const QmlAVOptions &avOptions);
    bool isOpen() const;
    QString name() const;
    const AVStream *stream() const { return m_avStream; }
    int streamIndex() const { return m_avStream ? m_avStream->index : -1; }

    double timeBaseUs() const;
    int64_t startPts() const;
    int64_t clock() const { return m_clock; }

    void decodeAVPacket(const AVPacketPtr &avPacketPtr);

    auto &counters() { return m_counters; }
    const auto &counters() const { return m_counters; }

signals:
    void frameFinished(const std::shared_ptr<QmlAVFrame> frame);

protected:
    void setSkipFrameFlag();
    void worker(const AVPacketPtr &avPacketPtr);

    AVCodecContext *avCodecCtx() const { return m_avCodecCtx; }
    virtual bool initHWAccel(AVCodecContext *avCodecCtx, const QmlAVOptions &avOptions) { return true; }
    virtual const std::shared_ptr<QmlAVFrame> frame(const AVFramePtr &avFramePtr) const {
        // NOTE: Cannot be pure virtual!
        // A stub method called on an early (static) binding when the destructor is executed.
        return {};
    };

private:
    bool m_asyncMode;
    int64_t m_clock;

    const AVStream *m_avStream;
    AVCodecContext *m_avCodecCtx;

    QmlAVThreadTask<decltype(&QmlAVDecoder::worker)> m_threadTask;
    QmlAVThreadLiveController<void> m_thread;

    Counters m_counters;
};
Q_DECLARE_METATYPE(std::shared_ptr<QmlAVFrame>)

class QmlAVVideoDecoder final : public QmlAVDecoder
{
    Q_OBJECT

public:
    QmlAVVideoDecoder(QObject *parent = nullptr);
    ~QmlAVVideoDecoder() override;

    std::shared_ptr<QmlAVHWOutput> hwOutput() const { return m_hwOutput; }

protected:
    bool initHWAccel(AVCodecContext *avCodecCtx, const QmlAVOptions &avOptions) override;
    static AVPixelFormat negotiatePixelFormatCb(struct AVCodecContext *avCodecCtx, const AVPixelFormat *avCodecPixelFormats);
    const std::shared_ptr<QmlAVFrame> frame(const AVFramePtr &avFramePtr) const override;

private:
    std::shared_ptr<QmlAVHWOutput> m_hwOutput;

    friend class QmlAVDecoder;
};

class QmlAVAudioDecoder final : public QmlAVDecoder
{
    Q_OBJECT

public:
    QmlAVAudioDecoder(QObject *parent = nullptr);

    QAudioFormat audioFormat() const;

protected:
    std::shared_ptr<QmlAVFrame> const frame(const AVFramePtr &avFramePtr) const override;
};

#endif // QMLAVDECODER_H
