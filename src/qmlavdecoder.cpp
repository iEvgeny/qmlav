#include "qmlavdecoder.h"
#include "qmlavoptions.h"
#include "qmlavframe.h"
#include "qmlavhwoutput.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
}

#define MAX_PACKETS 64
#define MAX_VIDEO_FRAMES 8
#define MAX_AUDIO_FRAMES 32

QmlAVDecoder::QmlAVDecoder(Clock &clock, QObject *parent, Type type)
    : QObject(parent)
    , m_avCodecCtx(nullptr)
    , m_type(type)
    , m_clock(clock)
    , m_avStream(nullptr)
    , m_threadTask(&QmlAVDecoder::worker)
{
    qRegisterMetaType<std::shared_ptr<QmlAVFrame>>();

    m_thread = m_threadTask.getLiveController();
    m_threadTask.argsQueue()->resetWaitLimits(1, MAX_PACKETS);
}

QmlAVDecoder::~QmlAVDecoder()
{
    m_thread.requestInterruption(true);

    avcodec_free_context(&m_avCodecCtx);
}

bool QmlAVDecoder::open(const AVStream *avStream, const QmlAVOptions &avOptions)
{
    int ret;

    if (m_avCodecCtx) {
        return false;
    }

    const AVCodec *codec = avOptions.avCodec(avStream);
    if (codec) {
        m_avCodecCtx = avcodec_alloc_context3(codec);
        if (!m_avCodecCtx) {
            logWarning() << "Unable allocate codec context";
            return false;
        }

        ret = avcodec_parameters_to_context(m_avCodecCtx, avStream->codecpar);
        if (ret < 0) {
            logWarning() << QString("Unable fill codec context: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret);
            return false;
        }

        if (!initVideoDecoder(avOptions)) {
            return false;
        }

        AVDictionaryPtr opts = static_cast<AVDictionaryPtr>(avOptions);
        ret = avcodec_open2(m_avCodecCtx, codec, opts);
        if (ret  < 0) {
            logWarning() << QString("Unable initialize codec context: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret);
            return false;
        }

        logDebug() << "avcodec_open2() options ignored: " << QmlAV::Quote << opts.getString();

        m_avStream = avStream;

        return true;
    }

    return false;
}

bool QmlAVDecoder::isOpen() const
{
    return m_avCodecCtx && avcodec_is_open(m_avCodecCtx) && m_thread.isRunning();
}

QString QmlAVDecoder::name() const
{
    return isOpen() ? m_avCodecCtx->codec->name : "";
}

int64_t QmlAVDecoder::startTime() const
{
    if (m_clock.startTime == 0) {
        m_clock.startTime = Clock::now();
    }

    return m_clock.startTime;
}

bool QmlAVDecoder::decodeAVPacket(const AVPacketPtr &avPacket)
{
    if (isOpen()) {
        m_threadTask(this, avPacket);
        return true;
    }

    return false;
}

int QmlAVDecoder::frameQueueLength() const
{
    // Each frame contains a decoder instance during its lifetime
    auto length = weak_from_this().use_count() - 1;
    return std::max<int>(0, length);
}

QmlAVLoopController QmlAVDecoder::worker(const AVPacketPtr &avPacket)
{
    AVFramePtr avFrame;

    assert(m_avCodecCtx);

    // Get available frame from the decoder
    int ret = avcodec_receive_frame(m_avCodecCtx, avFrame);
    if (ret < 0) {
        // Those two return values are special and mean there is no output
        // frame available, but there were no errors during decoding.
        if (ret != AVERROR_EOF && ret != AVERROR(EAGAIN)) {
            logWarning() << QString("Unable to read decoded frame: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret);
        }

        // Submit the packet to the decoder
        ret = avcodec_send_packet(m_avCodecCtx, avPacket);
        if (ret < 0) {
            logWarning() << QString("Unable send packet to decoder: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret);
        } else {
            m_counters.packetsDecoded++;
        }

        return QmlAVLoopController::Continue;
    } else {
        if (m_frameQueueLimit.addValue(frameQueueLength())) {

            avFrame->opaque = this;

            auto f = frame(avFrame);
            if (f && f->isValid()) {
                m_clock.lastPts.store(f->pts(), std::memory_order_release);
                m_counters.framesDecoded++;
                emit frameFinished(f);

                if (!m_clock.realTime.load(std::memory_order_relaxed)) {
                    // Primitive syncing for local playback
                    auto presentTime = startTime() + f->pts() - f->startPts();
                    return QmlAVLoopController(QmlAVLoopController::Retry, presentTime - Clock::now());
                }
            }
        } else {
            m_counters.framesDiscarded++;
            logDebug() << QString("Exceeding %1 frame queue limit: ").arg(typeName()) << m_frameQueueLimit;
        }
    }

    return QmlAVLoopController::Retry;
}

QmlAVVideoDecoder::QmlAVVideoDecoder(Clock &clock, QObject *parent)
    : QmlAVDecoder(clock, parent, TypeVideo)
{   
    m_frameQueueLimit.setLimit(MAX_VIDEO_FRAMES);
}

QmlAVVideoDecoder::~QmlAVVideoDecoder()
{
    if (m_avCodecCtx) {
        av_buffer_unref(&m_avCodecCtx->hw_device_ctx);
    }
}

bool QmlAVVideoDecoder::initVideoDecoder(const QmlAVOptions &avOptions)
{
    if (!m_avCodecCtx) {
        return false;
    }

    m_avCodecCtx->get_format = negotiatePixelFormatCb;

    AVHWDeviceType avHWDeviceType = avOptions.avHWDeviceType();
    if (avHWDeviceType != AV_HWDEVICE_TYPE_NONE) {
        AVDictionaryPtr opts;
        AVBufferRef *avHWDeviceCtx = nullptr;

        m_hwOutput = avOptions.hwOutput();
        if (m_hwOutput && m_hwOutput->type() == QmlAVHWOutput::TypeVAAPI_GLX) {
            // NOTE: The X11 windowing subsystem can also be initialized in the "QmlAVHWOutput_VAAPI_GLX" module manually
            opts.set("connection_type", "x11");
        }

        // TODO: Use parameters to initialize the HW device. See ffmpeg_hw.c (-hwaccel_device, -init_hw_device options)
        if (av_hwdevice_ctx_create(&avHWDeviceCtx, avHWDeviceType, nullptr, opts, 0) < 0) {
            logWarning() << "Failed to create specified HW device";
            return false;
        }

        // NOTE: This field should be set before avcodec_open2() is called and must not be written to thereafter
        m_avCodecCtx->hw_device_ctx = av_buffer_ref(avHWDeviceCtx);

        av_buffer_unref(&avHWDeviceCtx);
    }

    return true;
}

// NOTE: The default FFmpeg implementation for this callback can be seen as equal or even superior.
// See libavcodec/decode.c: avcodec_default_get_format()
AVPixelFormat QmlAVVideoDecoder::negotiatePixelFormatCb(AVCodecContext *avCodecCtx, const AVPixelFormat *avCodecPixelFormats)
{
    if (avCodecCtx->hw_device_ctx) {
        AVHWDeviceContext *hwDeviceCtx = reinterpret_cast<AVHWDeviceContext *>(avCodecCtx->hw_device_ctx->data);

        for (int i = 0; const AVCodecHWConfig *config = avcodec_get_hw_config(avCodecCtx->codec, i); ++i) {
            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == hwDeviceCtx->type) {

                for (int i = 0; avCodecPixelFormats[i] != AV_PIX_FMT_NONE; ++i) {
                    if (avCodecPixelFormats[i] == config->pix_fmt) {
                        return avCodecPixelFormats[i];
                    }
                }
            }
        }
    }

    // Choose the first suitable entry if the HW format matching failed or the HW device context was not provided
    for (int i = 0; avCodecPixelFormats[i] != AV_PIX_FMT_NONE; ++i) {
        if (QmlAVPixelFormat(avCodecPixelFormats[i]).isQtNative()) {
            return avCodecPixelFormats[i];
        }
    }

    // NOTE: If we do reach this point, the codec will modify "avCodecPixelFormats[]" until it is satisfied
    return *avCodecPixelFormats;
}

const std::shared_ptr<QmlAVFrame> QmlAVVideoDecoder::frame(const AVFramePtr &avFrame) const
{
    return std::make_shared<QmlAVVideoFrame>(avFrame);
}

QmlAVAudioDecoder::QmlAVAudioDecoder(Clock &clock, QObject *parent)
    : QmlAVDecoder(clock, parent, TypeAudio)
{
    m_frameQueueLimit.setLimit(MAX_AUDIO_FRAMES);
}

const std::shared_ptr<QmlAVFrame> QmlAVAudioDecoder::frame(const AVFramePtr &avFrame) const
{
    return std::make_shared<QmlAVAudioFrame>(avFrame);
}
