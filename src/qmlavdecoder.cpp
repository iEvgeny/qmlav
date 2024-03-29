#include "qmlavdecoder.h"
#include "qmlavoptions.h"
#include "qmlavframe.h"
#include "qmlavhwoutput.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/time.h>
}

QmlAVDecoder::QmlAVDecoder(QObject *parent, Type type)
    : QObject(parent)
    , m_avCodecCtx(nullptr)
    , m_type(type)
    , m_asyncMode(false)
    , m_clock(0)
    , m_startTime(0)
    , m_avStream(nullptr)
    , m_threadTask(&QmlAVDecoder::worker)
{
    qRegisterMetaType<std::shared_ptr<QmlAVFrame>>();
}

QmlAVDecoder::~QmlAVDecoder()
{
    m_thread.requestInterruption(true);

    avcodec_free_context(&m_avCodecCtx);
}

// NOTE: Not thread safe!
void QmlAVDecoder::setAsyncMode(bool async)
{
    if (m_asyncMode == async) {
        return;
    }

    m_asyncMode = async;

    if (async && !m_thread.isRunning()) {
        m_thread = m_threadTask.getLiveController();
    }
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
        m_startTime = av_gettime();

        return true;
    }

    return false;
}

bool QmlAVDecoder::isOpen() const
{
    if (m_avCodecCtx && avcodec_is_open(m_avCodecCtx) > 0) {
        return true;
    }

    return false;
}

QString QmlAVDecoder::name() const
{
    QString name;

    if (isOpen()) {
        name = m_avCodecCtx->codec->name;
    }

    return name;
}

void QmlAVDecoder::decodeAVPacket(const AVPacketPtr &avPacket)
{
    if (m_asyncMode) {
        m_threadTask(this, avPacket);
    } else {
        worker(avPacket);
    }
}

// TODO: Consider only video frames
void QmlAVDecoder::setSkipFrameFlag()
{
    assert(m_avCodecCtx);

    const int limit = 30; // TODO: Implement dynamically limit
    int length = m_counters.frameQueueLength;

    auto exceeding = length - limit;
    if (exceeding > 0) {
        m_avCodecCtx->skip_frame = AVDISCARD_ALL;
        logDebug() << QString("Exceeded %1 %2 frames queue limit by %3 frame(s)!")
                          .arg(limit)
                          .arg(type() == TypeVideo ? "Video" : "Audio")
                          .arg(exceeding);
    } else {
        m_avCodecCtx->skip_frame = AVDISCARD_DEFAULT;
    }
}

void QmlAVDecoder::worker(const AVPacketPtr &avPacket)
{
    int ret = AVERROR_UNKNOWN;
    AVFramePtr avFrame;

    assert(m_avCodecCtx);

    setSkipFrameFlag();

    // Submit the packet to the decoder
    ret = avcodec_send_packet(m_avCodecCtx, avPacket);
    if (ret < 0) {
        logWarning() << QString("Unable send packet to decoder: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret);
    }

    // Get all the available frames from the decoder
    while (ret >= 0) {
        ret = avcodec_receive_frame(m_avCodecCtx, avFrame);

        if (ret < 0) {
            // Those two return values are special and mean there is no output
            // frame available, but there were no errors during decoding.
            if (ret != AVERROR_EOF && ret != AVERROR(EAGAIN)) {
                logWarning() << QString("Unable to read decoded frame: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret);
            }

            break;
        }

        if (m_avCodecCtx->skip_frame >= AVDISCARD_NONREF) {
            m_counters.framesDiscarded++;
        } else {
            avFrame->opaque = this;

            if (auto f = frame(avFrame)) {
                // NOTE: Not thread safe! Only makes sense in sync mode.
                if (!m_asyncMode) {
                    m_clock = f->pts() - f->startPts();
                }

                m_counters.framesDecoded++;
                emit frameFinished(f);
            }
        }

        avFrame.unref();
    }
}

QmlAVVideoDecoder::QmlAVVideoDecoder(QObject *parent)
    : QmlAVDecoder(parent, TypeVideo)
{
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

        // NOTE: This field should be set before avcodec_open2() is called and must not be written to thereafter.
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

QmlAVAudioDecoder::QmlAVAudioDecoder(QObject *parent)
    : QmlAVDecoder(parent, TypeAudio)
{
}

const std::shared_ptr<QmlAVFrame> QmlAVAudioDecoder::frame(const AVFramePtr &avFrame) const
{
    return std::make_shared<QmlAVAudioFrame>(avFrame);
}
