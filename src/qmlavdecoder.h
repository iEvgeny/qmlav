#ifndef QMLAVDECODER_H
#define QMLAVDECODER_H

extern "C" {
#include <libavutil/time.h>
}

#include <QVideoSurfaceFormat>
#include <QAudioOutput>

#include "qmlavthread.h"
#include "qmlavresampler.h"

struct AVCodecContext;

class QmlAVDemuxer;
class QmlAVOptions;
class QmlAVFrame;
class QmlAVHWOutput;

class QmlAVDecoder : public std::enable_shared_from_this<QmlAVDecoder>
{
public:
    struct Clock {
        QmlAVReleaseAcquireAtomic<int64_t> startTime = 0;
        QmlAVRelaxedAtomic<bool> realTime = true;
        QmlAVReleaseAcquireAtomic<int64_t> leftPts = 0; // PTS of the last destructed frame

        static int64_t now() { return av_gettime_relative(); }
    };

    struct Counters {
        QmlAVRelaxedAtomic<uint32_t> packetsDecoded = 0;
        QmlAVRelaxedAtomic<uint32_t> framesDecoded = 0;
        QmlAVRelaxedAtomic<uint32_t> framesDiscarded = 0;
    };

    enum Type {
        TypeUnknown,
        TypeVideo,
        TypeAudio
    };

    QmlAVDecoder(const std::shared_ptr<QmlAVDemuxer> &demuxer, Type type = TypeUnknown);
    virtual ~QmlAVDecoder();

    QmlAVDemuxer *demuxer() const { return m_demuxer.get(); }

    Type type() const { return m_type; }
    QString typeName() const { return m_type == TypeVideo ? "Video" : "Audio"; }

    bool open(const AVStream *avStream, const QmlAVOptions &avOptions);
    bool isOpen() const;
    QString name() const;
    const AVStream *stream() const { return m_avStream; }
    int streamIndex() const { return m_avStream ? m_avStream->index : -1; }

    bool decodeAVPacket(const AVPacketPtr &avPacket);

    void requestInterruption(bool wait = false) { m_thread.requestInterruption(wait); }
    void waitForEmptyPacketQueue() { m_threadTask.argsQueue()->waitForEmpty(); }

    int packetQueueLength() const { return m_threadTask.argsQueue()->length(); }
    int frameQueueLength() const;

    const auto &counters() const { return m_counters; }

signals:
    void frameFinished(const std::shared_ptr<QmlAVFrame> frame);

protected:
    QmlAVLoopController worker(const AVPacketPtr &avPacket);

    virtual bool initVideoDecoder([[maybe_unused]] const QmlAVOptions &avOptions) { return true; }
    virtual const std::shared_ptr<QmlAVFrame> makeFrame([[maybe_unused]] const AVFramePtr &avFrame,
                                                        [[maybe_unused]] const std::shared_ptr<QmlAVDecoder> &decoder) const {
        // NOTE: Cannot be pure virtual!
        // A stub method called on an early (static) binding when the destructor is executed.
        return {};
    }

protected:
    AVCodecContext *m_avCodecCtx;
    QMLAVSoftLimit<double> m_frameQueueLimit;

private:
    Type m_type;
    std::shared_ptr<QmlAVDemuxer> m_demuxer;

    const AVStream *m_avStream;

    QmlAVThreadTask<decltype(&QmlAVDecoder::worker)> m_threadTask;
    QmlAVThreadLiveController<QmlAVLoopController> m_thread;

    Counters m_counters;
};

class QmlAVVideoDecoder final : public QmlAVDecoder
{
public:
    QmlAVVideoDecoder(const std::shared_ptr<QmlAVDemuxer> &demuxer);
    ~QmlAVVideoDecoder() override;

    std::shared_ptr<QmlAVHWOutput> hwOutput() const { return m_hwOutput; }

protected:
    bool initVideoDecoder(const QmlAVOptions &avOptions) override;
    static AVPixelFormat negotiatePixelFormatCb(struct AVCodecContext *avCodecCtx, const AVPixelFormat *avCodecPixelFormats);
    const std::shared_ptr<QmlAVFrame> makeFrame(const AVFramePtr &avFrame, const std::shared_ptr<QmlAVDecoder> &decoder) const override;

private:
    std::shared_ptr<QmlAVHWOutput> m_hwOutput;
};

class QmlAVAudioDecoder final : public QmlAVDecoder
{
public:
    QmlAVAudioDecoder(const std::shared_ptr<QmlAVDemuxer> &demuxer);

    auto &resampler() { return m_resampler; }

protected:
    std::shared_ptr<QmlAVFrame> const makeFrame(const AVFramePtr &avFrame, const std::shared_ptr<QmlAVDecoder> &decoder) const override;

private:
    QmlAVResampler m_resampler;
};

#endif // QMLAVDECODER_H
