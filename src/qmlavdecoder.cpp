#include "qmlavdecoder.h"
#include "qmlavoptions.h"
#include "qmlavframe.h"
#include "qmlavhwoutput.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
}

QmlAVDecoder::QmlAVDecoder(QObject *parent)
    : QObject(parent)
    , m_avCodecCtx(nullptr)
    , m_asyncMode(false)
    , m_clock(0)
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

double QmlAVDecoder::timeBaseUs() const
{
    assert(m_avStream);
    return av_q2d(m_avStream->time_base) * 1000000;
}

int64_t QmlAVDecoder::startPts() const
{
    assert(m_avStream);
    if (m_avStream->start_time != AV_NOPTS_VALUE) {
        return m_avStream->start_time * timeBaseUs();
    }

    return 0;
}

void QmlAVDecoder::decodeAVPacket(const AVPacketPtr &avPacketPtr)
{
    if (m_asyncMode) {
        m_threadTask(this, avPacketPtr);
    } else {
        worker(avPacketPtr);
    }
}

void QmlAVDecoder::setSkipFrameFlag()
{
    assert(m_avCodecCtx);

    const int limit = 30; // TODO: Implement dynamically limit
    auto length = m_counters.frameQueueLength();

    auto exceeding = length - limit;
    if (exceeding > 0) {
        m_avCodecCtx->skip_frame = m_avCodecCtx->skip_frame = AVDISCARD_ALL;
        m_counters.framesDiscardedAdd();
        logDebug() << QString("Exceeded %1 frames queue limit by %2 frame(s)!").arg(limit).arg(exceeding);
    } else {
        m_avCodecCtx->skip_frame = m_avCodecCtx->skip_frame = AVDISCARD_DEFAULT;
    }
}

void QmlAVDecoder::worker(const AVPacketPtr &avPacketPtr)
{
    int ret;
    AVFramePtr avFramePtr;

    assert(m_avCodecCtx);

    setSkipFrameFlag();

    // Submit the packet to the decoder
    ret = avcodec_send_packet(m_avCodecCtx, avPacketPtr);
    if (ret < 0) {
        logWarning() << QString("Unable send packet to decoder: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret);
    }

    // Get all the available frames from the decoder
    while (ret >= 0) {
        ret = avcodec_receive_frame(m_avCodecCtx, avFramePtr);

        if (ret < 0) {
            // Those two return values are special and mean there is no output
            // frame available, but there were no errors during decoding.
            if (ret != AVERROR_EOF && ret != AVERROR(EAGAIN)) {
                logWarning() << QString("Unable to read decoded frame: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret);
            }

            break;
        }

        avFramePtr->opaque = this;

        if (auto f = frame(avFramePtr)) {
            // NOTE: Not thread safe! Only makes sense in sync mode.
            if (!m_asyncMode) {
                m_clock = f->pts() - startPts();
            }

            m_counters.framesDecodedAdd();
            emit frameFinished(f);
        }

        avFramePtr.unref();
    }
}

QmlAVVideoDecoder::QmlAVVideoDecoder(QObject *parent)
    : QmlAVDecoder(parent)
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

const std::shared_ptr<QmlAVFrame> QmlAVVideoDecoder::frame(const AVFramePtr &avFramePtr) const
{
    return std::make_shared<QmlAVVideoFrame>(avFramePtr);
}

QmlAVAudioDecoder::QmlAVAudioDecoder(QObject *parent)
    : QmlAVDecoder(parent)
{
}

QAudioFormat QmlAVAudioDecoder::audioFormat() const
{
    QAudioFormat format;

    if (isOpen()) {
        format.setSampleRate(m_avCodecCtx->sample_rate);
        format.setChannelCount(m_avCodecCtx->channels);
        format.setCodec("audio/pcm");
        format.setByteOrder(AV_NE(QAudioFormat::BigEndian, QAudioFormat::LittleEndian));
        format.setSampleType(QmlAVSampleFormat::audioFormatFromAVFormat(m_avCodecCtx->sample_fmt));
        format.setSampleSize(av_get_bytes_per_sample(m_avCodecCtx->sample_fmt) * 8);
    }

    return  format;
}

const std::shared_ptr<QmlAVFrame> QmlAVAudioDecoder::frame(const AVFramePtr &avFramePtr) const
{
    auto af = std::make_shared<QmlAVAudioFrame>(avFramePtr);
    af->setAudioFormat(audioFormat());
    return af;
}
