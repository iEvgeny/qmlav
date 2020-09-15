#include "qmlavdecoder.h"

QmlAVDecoder::QmlAVDecoder(QObject *parent)
    : QObject(parent),
      m_ptsClock(0),
      m_startTime(0),
      m_avStream(nullptr),
      m_avCodecCtx(nullptr),
      m_avFrame(nullptr)
{
    qRegisterMetaType<std::shared_ptr<QmlAVFrame>>();
    m_avFrame = av_frame_alloc();
}

QmlAVDecoder::~QmlAVDecoder()
{
    if (m_avFrame) {
        av_frame_free(&m_avFrame);
    }

    closeCodec();
}

bool QmlAVDecoder::openCodec(AVStream *stream)
{
    int ret;

    if (m_avCodecCtx || stream == nullptr) {
        return false;
    }

    AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == NULL) {
        logError(QmlAVUtils::logId(reinterpret_cast<QmlAVDemuxer*>(parent())), "Unable find decoder");
        return false;
    }

    m_avCodecCtx = avcodec_alloc_context3(codec);
    if (!m_avCodecCtx) {
        logError(QmlAVUtils::logId(reinterpret_cast<QmlAVDemuxer*>(parent())), "Unable allocate codec context");
        return false;
    }

    ret = avcodec_parameters_to_context(m_avCodecCtx, stream->codecpar);
    if (ret < 0) {
        logError(QmlAVUtils::logId(reinterpret_cast<QmlAVDemuxer*>(parent())),
                             QString("Unable fill codec context: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret));
        closeCodec();
        return false;
    }

    ret = avcodec_open2(m_avCodecCtx, codec, NULL);
    if (ret  < 0) {
        logError(QmlAVUtils::logId(reinterpret_cast<QmlAVDemuxer*>(parent())),
                             QString("Unable initialize codec context: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret));
        closeCodec();
        return false;
    }

    m_avStream = stream;
    m_ptsClock = startPts();

    return true;
}

void QmlAVDecoder::closeCodec()
{
    m_avStream = nullptr;

    if (m_avCodecCtx) {
        avcodec_free_context(&m_avCodecCtx);
    }
}

bool QmlAVDecoder::codecIsOpen() const
{
    if (m_avCodecCtx && avcodec_is_open(m_avCodecCtx) > 0) {
        return true;
    }

    return false;
}

int QmlAVDecoder::streamIndex() const
{
    if (m_avStream) {
        return m_avStream->index;
    }

    return -1;
}

qint64 QmlAVDecoder::clock() const
{
    double pts = m_ptsClock - startPts();
    return m_startTime + pts * timeBase();
}

double QmlAVDecoder::timeBase() const
{
    if (!m_avStream) {
        return 0;
    }

    return av_q2d(m_avStream->time_base) * 1000000;
}

qint64 QmlAVDecoder::frameStartTime()
{
    double pts = framePts() - startPts();
    return m_startTime + pts * timeBase();
}

int QmlAVDecoder::decode(const AVPacket &packet)
{
    int ret;
    int count = 0;

    if (!m_avCodecCtx) {
        return 0;
    }

    // Submit the packet to the decoder
    ret = avcodec_send_packet(m_avCodecCtx, &packet);
    if (ret < 0) {
        logError(QmlAVUtils::logId(reinterpret_cast<QmlAVDemuxer*>(parent())),
                             QString("Unable send packet to decoder: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret));
        return 0;
    }

    // Get all the available frames from the decoder
    while (ret >= 0) {
        ret = avcodec_receive_frame(m_avCodecCtx, m_avFrame);
        if (ret < 0) {
            // Those two return values are special and mean there is no output
            // frame available, but there were no errors during decoding
            if (ret != AVERROR_EOF && ret != AVERROR(EAGAIN)) {
                logError(QmlAVUtils::logId(reinterpret_cast<QmlAVDemuxer*>(parent())),
                                     QString("Unable to read decoded frame: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret));
            }

            return count;
        }

        emit frameFinished(frame());
        av_frame_unref(m_avFrame);
        ++count;
    }

    return count;
}

qint64 QmlAVDecoder::startPts() const
{
    if (!m_avStream || m_avStream->start_time == AV_NOPTS_VALUE) {
        return 0;
    }

    return m_avStream->start_time;
}

double QmlAVDecoder::framePts()
{
    if (!m_avFrame || !m_avStream) {
        return 0;
    }

    double pts = m_avFrame->pkt_dts;
    double timeBase = av_q2d(m_avStream->time_base);

    if (pts == AV_NOPTS_VALUE) {
        pts = m_avFrame->pts;
    }
    if (pts == AV_NOPTS_VALUE) {
        pts = m_ptsClock + timeBase;
    }

    pts += m_avFrame->repeat_pict * (timeBase * 0.5);
    m_ptsClock = pts;

    return pts;
}

QmlAVVideoDecoder::QmlAVVideoDecoder(QObject *parent)
    : QmlAVDecoder(parent),
      m_surfacePixelFormat(QVideoFrame::Format_Invalid)
{
}

void QmlAVVideoDecoder::setSupportedPixelFormats(const QList<QVideoFrame::PixelFormat> &formats)
{
    if (codecIsOpen()) {
        m_surfacePixelFormat = QmlAVVideoFormat::pixelFormatFromFFmpegFormat(m_avCodecCtx->pix_fmt);
        if (!formats.contains(m_surfacePixelFormat)) {
            m_surfacePixelFormat = QVideoFrame::Format_Invalid;

            // By default, we will need to convert an unsupported pixel format to first surface supported format
            for (int i = 0; i < formats.count(); ++i) {
                // This pixel format should also successfully match the ffmpeg format
                QVideoFrame::PixelFormat f = formats.value(i, QVideoFrame::Format_Invalid);
                if (QmlAVVideoFormat::ffmpegFormatFromPixelFormat(f) != AV_PIX_FMT_NONE)  {
                    m_surfacePixelFormat = f;
                    break;
                }
            }
        }
    }
}

QVideoSurfaceFormat QmlAVVideoDecoder::videoFormat() const
{
    QSize size(0, 0);

    if (codecIsOpen()) {
        size.setWidth(m_avCodecCtx->width);
        size.setHeight(m_avCodecCtx->height);
    }

    QVideoSurfaceFormat format(size, m_surfacePixelFormat);
    format.setPixelAspectRatio(pixelAspectRatio());
    return format;
}

QSize QmlAVVideoDecoder::pixelAspectRatio() const
{
    if (codecIsOpen()) {
        if (m_avCodecCtx->sample_aspect_ratio.num) {
            return QSize(m_avCodecCtx->sample_aspect_ratio.num,
                         m_avCodecCtx->sample_aspect_ratio.den);
        }
    }

    return QSize(1, 1);
}

std::shared_ptr<QmlAVFrame> QmlAVVideoDecoder::frame()
{
    std::shared_ptr<QmlAVVideoFrame> vf(new QmlAVVideoFrame(frameStartTime()));
    vf->setPixelFormat(m_surfacePixelFormat);
    vf->fromAVFrame(m_avFrame);
    return vf;
}

QmlAVAudioDecoder::QmlAVAudioDecoder(QObject *parent)
    : QmlAVDecoder(parent)
{
}

QAudioFormat QmlAVAudioDecoder::audioFormat()
{
    QAudioFormat format;

    if (codecIsOpen()) {
        format.setSampleRate(m_avCodecCtx->sample_rate);
        format.setChannelCount(m_avCodecCtx->channels);
        format.setCodec("audio/pcm");
        format.setByteOrder(AV_NE(QAudioFormat::BigEndian, QAudioFormat::LittleEndian));
        format.setSampleType(QmlAVAudioFormat::audioFormatFromFFmpegFormat(m_avCodecCtx->sample_fmt));
        format.setSampleSize(av_get_bytes_per_sample(m_avCodecCtx->sample_fmt) * 8);
    }

    return  format;
}

std::shared_ptr<QmlAVFrame> QmlAVAudioDecoder::frame()
{
    std::shared_ptr<QmlAVAudioFrame> af(new QmlAVAudioFrame(frameStartTime()));
    af->setAudioFormat(audioFormat());
    af->fromAVFrame(m_avFrame);
    return af;
}
