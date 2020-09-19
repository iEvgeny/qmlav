#include "qmlavdecoder.h"
#include "qmlavdemuxer.h"

QmlAVDecoderWorker::QmlAVDecoderWorker(QmlAVDecoder *decoderCtx, QObject *parent)
    : QObject(parent),
      m_decoderCtx(nullptr),
      m_avFrame(nullptr)
{
    qRegisterMetaType<std::shared_ptr<QmlAVFrame>>();

    m_decoderCtx = decoderCtx;
    m_avFrame = av_frame_alloc();
}

QmlAVDecoderWorker::~QmlAVDecoderWorker()
{
    if (m_avFrame) {
        av_frame_free(&m_avFrame);
    }
}

void QmlAVDecoderWorker::decodeAVPacket(AVPacket avPacket)
{
    int ret;

    if (m_decoderCtx) {
        // Submit the packet to the decoder
        ret = avcodec_send_packet(m_decoderCtx->m_avCodecCtx, &avPacket);
        if (ret < 0) {
            logError(QmlAVUtils::logId(reinterpret_cast<QmlAVDemuxer*>(m_decoderCtx->parent())),
                     QString("Unable send packet to decoder: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret));  // TODO:
        }

        // Get all the available frames from the decoder
        while (ret >= 0) {
            ret = avcodec_receive_frame(m_decoderCtx->m_avCodecCtx, m_avFrame);
            if (ret < 0) {
                // Those two return values are special and mean there is no output
                // frame available, but there were no errors during decoding
                if (ret != AVERROR_EOF && ret != AVERROR(EAGAIN)) {
                    logError(QmlAVUtils::logId(reinterpret_cast<QmlAVDemuxer*>(m_decoderCtx->parent())),
                             QString("Unable to read decoded frame: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret));  // TODO:
                }

                av_packet_unref(&avPacket);
                return;
            }

            if (!QThread::currentThread()->isInterruptionRequested()) {
                emit frameFinished(m_decoderCtx->frame(m_avFrame, frameStartTime()));
            }

            av_frame_unref(m_avFrame);
        }
    }

    av_packet_unref(&avPacket);
    return;
}

double QmlAVDecoderWorker::framePts()
{
    if (!m_decoderCtx) {
        return 0;
    }

    double pts = m_avFrame->pkt_dts;
    double timeBase = m_decoderCtx->m_timeBase;

    if (pts == AV_NOPTS_VALUE) {
        pts = m_avFrame->pts;
    }
    if (pts == AV_NOPTS_VALUE) {
        pts = m_decoderCtx->m_ptsClock + timeBase;
    }

    pts += m_avFrame->repeat_pict * (timeBase * 0.5);
    m_decoderCtx->m_ptsClock = pts;

    return pts;
}

qint64 QmlAVDecoderWorker::frameStartTime()
{
    if (!m_decoderCtx) {
        return 0;
    }

    double pts = framePts() - m_decoderCtx->m_startPts;
    return m_decoderCtx->m_startTime + pts * m_decoderCtx->m_timeBase;  // Уже не тот стартрайм
}

QmlAVDecoder::QmlAVDecoder(QmlAVDemuxer *parent)
    : QObject(parent),
      m_async(false),
      m_worker(nullptr),
      m_streamIndex(-1),
      m_avCodecCtx(nullptr),
      m_startTime(0),
      m_startPts(0),
      m_timeBase(0),
      m_ptsClock(0)
{
    qRegisterMetaType<AVPacket>("AVPacket");

    m_worker = new QmlAVDecoderWorker(this, nullptr);
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(this, &QmlAVDecoder::workerDecodeAVPacket, m_worker, &QmlAVDecoderWorker::decodeAVPacket);

    connect(m_worker, &QmlAVDecoderWorker::frameFinished, parent, &QmlAVDemuxer::frameFinished);
}

QmlAVDecoder::~QmlAVDecoder()
{
    m_thread.requestInterruption();
    m_thread.quit();
    m_thread.wait();

    if (m_avCodecCtx) {
        avcodec_free_context(&m_avCodecCtx);
    }
}

void QmlAVDecoder::setAsync(bool async)
{
    if (m_async == async) {
        return;
    }

    m_async = async;

    if (m_async) {
        if (m_worker->thread() != &m_thread) {
            m_worker->moveToThread(&m_thread);
        }
    } else {
        if (m_worker->thread() != &m_thread) {
            m_worker->moveToThread(thread());
        }
    }
}

bool QmlAVDecoder::openCodec(AVStream *stream)
{
    int ret;

    if (m_avCodecCtx || !stream) {
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
        return false;
    }

    ret = avcodec_open2(m_avCodecCtx, codec, NULL);
    if (ret  < 0) {
        logError(QmlAVUtils::logId(reinterpret_cast<QmlAVDemuxer*>(parent())),
                             QString("Unable initialize codec context: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret));
        return false;
    }

    m_streamIndex = stream->index;
    m_startPts = (stream->start_time != AV_NOPTS_VALUE) ? stream->start_time : 0;
    m_timeBase = av_q2d(stream->time_base);
    m_ptsClock = m_startPts;

    m_thread.start();

    return true;
}

bool QmlAVDecoder::codecIsOpen() const
{
    if (m_avCodecCtx && avcodec_is_open(m_avCodecCtx) > 0) {
        return true;
    }

    return false;
}

double QmlAVDecoder::timeBase() const
{
    return m_timeBase * 1000000;
}

double QmlAVDecoder::clock() const
{
    double pts = m_ptsClock - m_startPts;
    return m_startTime + pts * timeBase();
}

void QmlAVDecoder::decodeAVPacket(AVPacket &avPacket)
{
    if (m_async) {
        emit workerDecodeAVPacket(avPacket);
    } else {
        m_worker->decodeAVPacket(avPacket);
    }

    av_init_packet(&avPacket);
    avPacket.data = nullptr;
    avPacket.size = 0;
}

QmlAVVideoDecoder::QmlAVVideoDecoder(QmlAVDemuxer *parent)
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

std::shared_ptr<QmlAVFrame> QmlAVVideoDecoder::frame(AVFrame *avFrame, qint64 startTime) const
{
    std::shared_ptr<QmlAVVideoFrame> vf(new QmlAVVideoFrame(startTime));
    vf->setPixelFormat(m_surfacePixelFormat);
    vf->fromAVFrame(avFrame);
    return vf;
}

QmlAVAudioDecoder::QmlAVAudioDecoder(QmlAVDemuxer *parent)
    : QmlAVDecoder(parent)
{
}

QAudioFormat QmlAVAudioDecoder::audioFormat() const
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

std::shared_ptr<QmlAVFrame> QmlAVAudioDecoder::frame(AVFrame *avFrame, qint64 startTime) const
{
    std::shared_ptr<QmlAVAudioFrame> af(new QmlAVAudioFrame(startTime));
    af->setAudioFormat(audioFormat());
    af->fromAVFrame(avFrame);
    return af;
}
