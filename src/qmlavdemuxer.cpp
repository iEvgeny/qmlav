#include "qmlavdemuxer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
}

QmlAVDemuxer::QmlAVDemuxer(QObject *parent)
    : QObject(parent)
    , m_context(std::make_shared<QmlAVMediaContextHolder>(this))
{
}

QmlAVDemuxer::~QmlAVDemuxer()
{
    m_interruptCallback.requestAVInterrupt();

    m_loaderThread.requestInterrupt(true);
    m_demuxerThread.requestInterrupt(true);

    // Important! Serialization point for decoders dtor's
    m_context->videoDecoder->requestInterrupt(true);
    m_context->audioDecoder->requestInterrupt(true);
}

void QmlAVDemuxer::load(const QUrl &url, const QmlAVOptions &avOptions)
{
    int ret = AVERROR_UNKNOWN;
    QString source(url.toString());

    if (m_context->avFormatCtx->iformat) {
        return;
    }

    // TODO: Only Unix systems are supported
    if (url.isLocalFile()) {
        avdevice_register_all();
        source = url.toLocalFile();
    }

    if (source.isEmpty()) {
        logInfo() << "Source is emty!";
        emit mediaStatusChanged(QMediaPlayer::NoMedia);
        return;
    }

#if (LIBAVFORMAT_VERSION_MAJOR < 58)
    av_register_all();
    avformat_network_init();
#endif

    m_context->clock.realTime = avOptions.realTime().value_or(isRealTime(url));

    m_interruptCallback.setTimeout(avOptions.demuxerTimeout());
    m_context->avFormatCtx->interrupt_callback = m_interruptCallback;

    emit mediaStatusChanged(QMediaPlayer::LoadingMedia);

    m_loaderThread = QmlAVThread::run([=]() mutable {
        AVDictionaryPtr dict = static_cast<AVDictionaryPtr>(avOptions);
        ret = avformat_open_input(&m_context->avFormatCtx,
                                  source.toUtf8(),
                                  avOptions.avInputFormat(),
                                  dict);
        if (ret < 0) {
            logWarning() << QString("Unable to open input file: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret);
            emit mediaStatusChanged(QMediaPlayer::InvalidMedia);
            return;
        }

        logDebug() << "avformat_open_input() options ignored: " << QmlAV::Quote << dict.toString();

        ret = avformat_find_stream_info(m_context->avFormatCtx, nullptr);
        if (ret < 0) {
            logWarning() << QString("Cannot find stream information: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret);
            emit mediaStatusChanged(QMediaPlayer::InvalidMedia);
            return;
        }

        logDebug() << "--- DUMP FORMAT BEGIN ---";
        if (QmlAVUtils::loggingCategory().isDebugEnabled()) {
            av_dump_format(m_context->avFormatCtx, 0, source.toUtf8(), 0);
        }
        logDebug() << "--- DUMP FORMAT END ---";

        initDecoders(avOptions);

        if (!isLoaded()) {
            logWarning() << "Unable to open any decoder";
            emit mediaStatusChanged(QMediaPlayer::InvalidMedia);
            return;
        }

        emit mediaStatusChanged(QMediaPlayer::LoadedMedia);
        logDebug() << "Media loaded successfully!";

        emit mediaStatusChanged(QMediaPlayer::BufferedMedia); // NOTE: In common case, we don't use buffering to reduce latency
    });
}

void QmlAVDemuxer::start()
{
    if (m_demuxerThread.isRunning()) {
        return;
    }

    emit playbackStateChanged(QMediaPlayer::PlayingState);

    auto stop = [this](QMediaPlayer::MediaStatus status = QMediaPlayer::InvalidMedia) {
        emit mediaStatusChanged(status);
        emit playbackStateChanged(QMediaPlayer::StoppedState);
        return QmlAVLoopController::Break;
    };

    m_demuxerThread = QmlAVThread::loop([=]() mutable -> QmlAVLoopController {
        int ret;
        AVPacketPtr avPacket;

        m_loaderThread.waitForFinished();

        if (!isLoaded()) {
            return stop();
        }

        m_interruptCallback.resetTimer();

        ret = av_read_frame(m_context->avFormatCtx, avPacket);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                m_context->videoDecoder->waitForEmptyPacketQueue();
                m_context->audioDecoder->waitForEmptyPacketQueue();

                logDebug() << "End of media";
                return stop(QMediaPlayer::EndOfMedia);
            }

            if (ret != AVERROR_EXIT) {
                logWarning() << QString("Unable read frame: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret);
            }

            return stop();
        }

        if (avPacket->stream_index == m_context->videoDecoder->streamIndex()) {
            m_context->videoDecoder->decodeAVPacket(avPacket) || stop();
        } else if (avPacket->stream_index == m_context->audioDecoder->streamIndex()) {
            m_context->audioDecoder->decodeAVPacket(avPacket) || stop();
        } else {
            return 1; // Minimal sleep time
        }

        return QmlAVLoopController::Continue;
    });
}

int64_t QmlAVDemuxer::startTime() const
{
    if (m_context->clock.startTime == 0) {
        m_context->clock.startTime = QmlAVDecoder::Clock::now();
    }

    return m_context->clock.startTime;
}

QVariantMap QmlAVDemuxer::stat() const
{
    auto &vc = m_context->videoDecoder->counters();
    auto &ac = m_context->audioDecoder->counters();
    return {
        { "videoPacketsDecoded", vc.packetsDecoded.get() },
        { "videoFramesDecoded", vc.framesDecoded.get() },
        { "videoFramesDiscarded", vc.framesDiscarded.get() },
        { "audioPacketsDecoded", ac.packetsDecoded.get() },
        { "audioBuffersDecoded", ac.framesDecoded.get() },
        { "audioBuffersDiscarded", ac.framesDiscarded.get() }
    };
}

bool QmlAVDemuxer::isRealTime(QUrl url) const
{
    if (url.scheme() == "rtp"
            || url.scheme() == "srtp"
            || url.scheme().startsWith("rtmp") // rtmp{, e, s, t, te, ts}
            || url.scheme() == "rtsp"
            || url.scheme() == "udp") {
        return true;
    }

    return false;
}

void QmlAVDemuxer::initDecoders(const QmlAVOptions &avOptions)
{
    int bestVideoStream = av_find_best_stream(m_context->avFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (bestVideoStream >= 0 && !avOptions.videoDisable()) {
        if (m_context->videoDecoder->open(m_context->avFormatCtx->streams[bestVideoStream], avOptions)) {
            logDebug() << QString("Codec \"%1\" for stream #%2 opened.").arg(m_context->videoDecoder->name()).arg(bestVideoStream);
        }
    }

    int bestAudioStream = av_find_best_stream(m_context->avFormatCtx, AVMEDIA_TYPE_AUDIO, -1, bestVideoStream, nullptr, 0);
    if (bestAudioStream >= 0 && !avOptions.audioDisable()) {
        if (m_context->audioDecoder->open(m_context->avFormatCtx->streams[bestAudioStream], avOptions)) {
            logDebug() << QString("Codec \"%1\" for stream #%2 opened.").arg(m_context->audioDecoder->name()).arg(bestAudioStream);
        }
    }
}

void QmlAVDemuxer::frameHandler(const std::shared_ptr<QmlAVFrame> frame)
{
    emit frameFinished(frame);
}
