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
        template <typename T, typename Super = std::atomic<T>>
        struct RelaxedAtomic : Super {
            constexpr static auto order = std::memory_order_relaxed;

            constexpr RelaxedAtomic(const T &other) noexcept : Super(other) { }

            T get() const noexcept { return Super::load(order); }
            operator T() const noexcept { return get(); }

            T operator++(int) noexcept { return Super::fetch_add(1, order); }
            T operator--(int) noexcept { return Super::fetch_sub(1, order); }
            T operator++() = delete;
            T operator--() = delete;
        };

        RelaxedAtomic<int> framesDecoded = 0;
        RelaxedAtomic<int> framesDiscarded = 0;
        RelaxedAtomic<int> frameQueueLength = 0;
    };

public:
    enum Type
    {
        TypeUnknown,
        TypeVideo,
        TypeAudio
    };

    QmlAVDecoder(QObject *parent = nullptr, Type type = TypeUnknown);
    virtual ~QmlAVDecoder();

    Type type() const { return m_type; }

    bool asyncMode() const { return m_asyncMode; }
    void setAsyncMode(bool async);
    bool open(const AVStream *avStream, const QmlAVOptions &avOptions);
    bool isOpen() const;
    QString name() const;
    const AVStream *stream() const { return m_avStream; }
    int streamIndex() const { return m_avStream ? m_avStream->index : -1; }

    int64_t clock() const { return m_clock; }
    int64_t startTime() const { return m_startTime; }

    void decodeAVPacket(const AVPacketPtr &avPacket);

    auto &counters() { return m_counters; }
    const auto &counters() const { return m_counters; }

signals:
    void frameFinished(const std::shared_ptr<QmlAVFrame> frame);

protected:
    void setSkipFrameFlag();
    void worker(const AVPacketPtr &avPacket);

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
    bool m_asyncMode;
    int64_t m_clock;
    int64_t m_startTime;

    const AVStream *m_avStream;

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
    QmlAVAudioDecoder(QObject *parent = nullptr);

protected:
    std::shared_ptr<QmlAVFrame> const frame(const AVFramePtr &avFrame) const override;
};

#endif // QMLAVDECODER_H
