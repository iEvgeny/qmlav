#include "qmlavdemuxer.h"

extern "C" {
#include <libavutil/time.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
}

QmlAVDemuxer::QmlAVDemuxer(QObject *parent)
    : QObject(parent)
    , m_realtime(false)
    , m_avFormatCtx(nullptr)
    , m_avInterruptionRequested(false)
    , m_videoDecoder(std::make_shared<QmlAVVideoDecoder>())
    , m_audioDecoder(std::make_shared<QmlAVAudioDecoder>())
{
    connect(m_videoDecoder.get(), &QmlAVDecoder::frameFinished, this, &QmlAVDemuxer::frameFinished);
    connect(m_audioDecoder.get(), &QmlAVDecoder::frameFinished, this, &QmlAVDemuxer::frameFinished);
}

QmlAVDemuxer::~QmlAVDemuxer()
{
    requestAVInterruption();

    m_loaderThread.requestInterruption(true);
    m_demuxerThread.requestInterruption(true);

    avformat_close_input(&m_avFormatCtx);
}

void QmlAVDemuxer::load(const QUrl &url, const QmlAVOptions &avOptions)
{
    int ret;
    QString source(url.toString());

    if (m_avFormatCtx) {
        return;
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

    m_avFormatCtx = avformat_alloc_context();
    if (!m_avFormatCtx) {
        logWarning() << "Could not allocate AVFormatContext";
        return;
    }

    m_avFormatCtx->interrupt_callback.opaque = this;
    m_avFormatCtx->interrupt_callback.callback = [](void *obj) -> int {
        auto d = static_cast<QmlAVDemuxer *>(obj);
        return d ? d->isAVInterruptionRequested() : 0;
    };

    // TODO: Only Unix systems are supported
    if (url.isLocalFile()) {
        avdevice_register_all();
        source = url.toLocalFile();
    }

    m_realtime = isRealtime(url);
    m_videoDecoder->setAsyncMode(m_realtime);
    m_audioDecoder->setAsyncMode(m_realtime);

    emit mediaStatusChanged(QMediaPlayer::LoadingMedia);

    m_loaderThread = QmlAVThread::run([=]() mutable {
        AVDictionaryPtr dict = static_cast<AVDictionaryPtr>(avOptions);
        ret = avformat_open_input(&m_avFormatCtx,
                                  source.toUtf8(),
                                  avOptions.avInputFormat(),
                                  dict);
        if (ret < 0) {
            logWarning() << QString("Unable to open input file: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret);
            emit mediaStatusChanged(QMediaPlayer::InvalidMedia);
            return;
        }

        logDebug() << "avformat_open_input() options ignored: " << QmlAV::Quote << dict.getString();

        ret = avformat_find_stream_info(m_avFormatCtx, nullptr);
        if (ret < 0) {
            logWarning() << QString("Cannot find stream information: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret);
            emit mediaStatusChanged(QMediaPlayer::InvalidMedia);
            return;
        }

        logDebug() << "--- DUMP FORMAT BEGIN ---";
        if (QmlAVUtils::loggingCategory().isDebugEnabled()) {
            av_dump_format(m_avFormatCtx, 0, source.toUtf8(), 0);
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

        // Helper for syncing local playback
        if (m_avFormatCtx->start_time_realtime == 0 || m_avFormatCtx->start_time_realtime == AV_NOPTS_VALUE) {
            m_avFormatCtx->start_time_realtime = av_gettime();
        }

        emit mediaStatusChanged(QMediaPlayer::BufferedMedia); // NOTE: We do not use buffering to reduce latency
    });
}

void QmlAVDemuxer::start()
{
    if (!m_avFormatCtx || m_demuxerThread.isRunning()) {
        return;
    }

    emit playbackStateChanged(QMediaPlayer::PlayingState);

    m_demuxerThread = QmlAVThread::loop([=]() mutable -> QmlAVLoopController {
        int ret;
        AVPacketPtr avPacketPtr;

        m_loaderThread.waitForFinished();

        if (!isLoaded()) {
            emit playbackStateChanged(QMediaPlayer::StoppedState);
            return QmlAVLoopController::Interrupt;
        }

        // TODO:
        int64_t clock = 0;

        ret = av_read_frame(m_avFormatCtx, avPacketPtr);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                logInfo() << "End of media";
                emit mediaStatusChanged(QMediaPlayer::EndOfMedia);
            } else {
                if (ret != AVERROR_EXIT) {
                    logWarning() << QString("Unable read frame: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret);
                }

                emit mediaStatusChanged(QMediaPlayer::StalledMedia);
            }

            emit playbackStateChanged(QMediaPlayer::StoppedState);
            return QmlAVLoopController::Interrupt;
        }

        if (avPacketPtr->stream_index == m_videoDecoder->streamIndex()) {
            m_videoDecoder->decodeAVPacket(avPacketPtr);
            clock = m_videoDecoder->clock();
        } else if (avPacketPtr->stream_index == m_audioDecoder->streamIndex()) {
            m_audioDecoder->decodeAVPacket(avPacketPtr);
            if (!m_videoDecoder->isOpen()) {
                clock = m_audioDecoder->clock();
            }
        } else {
            return 1; // Minimal sleep time
        }

        // Primitive syncing for local playback
        if (!m_realtime) {
            // Waiting the frame display time
            return m_avFormatCtx->start_time_realtime + clock - av_gettime();
        }

        return QmlAVLoopController::Continue;
    });
}

QVariantMap QmlAVDemuxer::stat() const
{
    auto &vc = m_videoDecoder->counters();
    QVariantMap data = {
        { "framesDecoded", vc.framesDecoded() },
        { "framesDiscarded", vc.framesDiscarded() },
        { "frameQueueLength", vc.frameQueueLength() }
    };

    return data;
}

bool QmlAVDemuxer::isRealtime(QUrl url) const
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
    int bestVideoStream = av_find_best_stream(m_avFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (bestVideoStream >= 0) {
        if (m_videoDecoder->open(m_avFormatCtx->streams[bestVideoStream], avOptions)) {
            logDebug() << QString("Codec \"%1\" for stream #%2 opened.").arg(m_videoDecoder->name()).arg(bestVideoStream);
        }
    }

    int bestAudioStream = av_find_best_stream(m_avFormatCtx, AVMEDIA_TYPE_AUDIO, -1, bestVideoStream, nullptr, 0);
    if (bestAudioStream >= 0) {
        if (m_audioDecoder->open(m_avFormatCtx->streams[bestAudioStream], avOptions)) {
            logDebug() << QString("Codec \"%1\" for stream #%2 opened.").arg(m_audioDecoder->name()).arg(bestAudioStream);
            emit audioFormatChanged(m_audioDecoder->audioFormat());
        }
    }
}
