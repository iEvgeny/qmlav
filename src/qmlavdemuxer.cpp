#include "qmlavdemuxer.h"

QmlAVInterruptCallback::QmlAVInterruptCallback(qint64 timeout)
{
    callback = handler;
    opaque = this;
    m_timeout = timeout;
    m_interruptionRequested = false;
}

void QmlAVInterruptCallback::startTimer()
{
    m_interruptionRequested = false;
    m_timer.start();
}

int QmlAVInterruptCallback::handler(void *obj)
{
    QmlAVInterruptCallback *cb = reinterpret_cast<QmlAVInterruptCallback*>(obj);
    Q_ASSERT(cb);
    if (!cb) {
        return 0;
    }

    if (cb->m_interruptionRequested || QThread::currentThread()->isInterruptionRequested()) {
        return 1; // Interrupt
    }

    if (!cb->m_timer.isValid()) {
        cb->startTimer();
        return 0;
    }

    if (!cb->m_timer.hasExpired(cb->m_timeout)) {
        return 0;
    }

    return 1; // Interrupt
}

QmlAVDemuxer::QmlAVDemuxer(QObject *parent)
    : QObject(parent),
      m_realtime(false),
      m_formatCtx(nullptr),
      m_videoDecoder(this),
      m_audioDecoder(this),
      m_playbackState(QMediaPlayer::StoppedState),
      m_status(QMediaPlayer::UnknownMediaStatus),
      m_interruptionRequested(false)
{
    connect(&m_videoDecoder, &QmlAVVideoDecoder::frameFinished, this, &QmlAVDemuxer::frameFinished);
    connect(&m_audioDecoder, &QmlAVAudioDecoder::frameFinished, this, &QmlAVDemuxer::frameFinished);

    logDebug(QmlAVUtils::logId(this), "QmlAVDemuxer::QmlAVDemuxer()");
}

QmlAVDemuxer::~QmlAVDemuxer()
{
    requestInterruption();

    if (m_formatCtx) {
        avformat_close_input(&m_formatCtx);
    }

    logDebug(QmlAVUtils::logId(this), "QmlAVDemuxer::~QmlAVDemuxer()");
}

void QmlAVDemuxer::requestInterruption()
{
    m_interruptCallback.requestInterruption();
    m_interruptionRequested = true;

    logDebug(QmlAVUtils::logId(this), "QmlAVDemuxer::requestInterruption()");
}

void QmlAVDemuxer::load(const QUrl &url, const QVariantMap &formatOptions)
{
    int ret;
    QString source(url.toString());
    AVDictionary *avFormatOptions = nullptr;

    if (m_formatCtx) {
        return;
    }

    if (source.isEmpty()) {
        logVerbose(QmlAVUtils::logId(this), QString("Source is emty!"));
        setStatus(QMediaPlayer::NoMedia);
        setPlaybackState(QMediaPlayer::StoppedState);
        return;
    }

#ifndef FF_API_NEXT
    av_register_all();
    avformat_network_init();
#endif

    // TODO: Only Unix systems are supported
    if (url.isLocalFile()) {
        avdevice_register_all();
        source = url.toLocalFile();
    }
    m_realtime = isRealtime(url);

    QMapIterator<QString, QVariant> i(formatOptions);
    while (i.hasNext()) {
        i.next();
        av_dict_set(&avFormatOptions, i.key().toUtf8(), i.value().toString().toUtf8(), 0);
        logDebug(QmlAVUtils::logId(this), QString("Added AVFormat option: -%1 %2").arg(i.key()).arg(i.value().toString()));
    }

    setStatus(QMediaPlayer::LoadingMedia);
    setPlaybackState(QMediaPlayer::StoppedState);

    m_formatCtx = avformat_alloc_context();
    m_formatCtx->interrupt_callback = m_interruptCallback;

    m_interruptCallback.startTimer();
    ret = avformat_open_input(&m_formatCtx, source.toUtf8(), nullptr, &avFormatOptions);
    if (ret < 0) {
        logError(QmlAVUtils::logId(this),
                             QString("Unable to open input file: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret));
        setStatus(QMediaPlayer::InvalidMedia);
        setPlaybackState(QMediaPlayer::StoppedState);
        return;
    }
    m_interruptCallback.stopTimer();

    m_interruptCallback.startTimer();
    ret = avformat_find_stream_info(m_formatCtx, nullptr);
    if (ret < 0) {
        logError(QmlAVUtils::logId(this),
                             QString("Cannot find stream information: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret));
        setStatus(QMediaPlayer::InvalidMedia);
        setPlaybackState(QMediaPlayer::StoppedState);
        return;
    }
    m_interruptCallback.stopTimer();

    if (QmlAVUtils::verboseLevel() > QmlAVUtils::LogError) {
        av_dump_format(m_formatCtx, 0, source.toUtf8(), 0);
    }

    if (!findStreams()) {
        logError(QmlAVUtils::logId(this), QString("Unable find valid stream"));
        setStatus(QMediaPlayer::InvalidMedia);
        setPlaybackState(QMediaPlayer::StoppedState);
        return;
    }

    if (m_videoStreams.count() > 0) {
        // Open first video stream
        m_videoDecoder.openCodec(m_videoStreams.value(0));
    }
    if (m_audioStreams.count() > 0) {
        // Open first audio stream
        m_audioDecoder.openCodec(m_audioStreams.value(0));
    }
    if (!m_videoDecoder.codecIsOpen() && !m_audioDecoder.codecIsOpen()) {
        logError(QmlAVUtils::logId(this), QString("Unable open any codec"));
        setStatus(QMediaPlayer::InvalidMedia);
        setPlaybackState(QMediaPlayer::StoppedState);
        return;
    }

    if (m_audioDecoder.codecIsOpen()) {
        emit audioFormatChanged(m_audioDecoder.audioFormat());
    }

    setStatus(QMediaPlayer::LoadedMedia);

    logDebug(QmlAVUtils::logId(this), QString("Media loaded successfully!"));

    av_dict_free(&avFormatOptions);
}

void QmlAVDemuxer::setSupportedPixelFormats(const QList<QVideoFrame::PixelFormat> &formats)
{
    if (m_videoDecoder.codecIsOpen()) {
        m_videoDecoder.setSupportedPixelFormats(formats);
        emit videoFormatChanged(m_videoDecoder.videoFormat());
    }
}

void QmlAVDemuxer::run()
{
    int ret;
    qint64 clock = 0;
    double timeBase = 0;

    if (m_playbackState != QMediaPlayer::StoppedState || m_status != QMediaPlayer::LoadedMedia) {
        return;
    }

    setPlaybackState(QMediaPlayer::PlayingState);

    // NOTE: We do not use buffering to reduce latency
    setStatus(QMediaPlayer::BufferedMedia);

    qint64 startTime = av_gettime();
    m_videoDecoder.setStartTime(startTime);
    m_audioDecoder.setStartTime(startTime);

    logDebug(QmlAVUtils::logId(this),
                         QString("QmlAVDemuxer::run() : { startTime=%1; m_videoDecoder.timeBase()->%2 }").arg(startTime).arg(m_videoDecoder.timeBase()));

    while (!isInterruptionRequested() || m_playbackState == QMediaPlayer::PlayingState) {
        if (!m_formatCtx) {
            break;
        }

        av_init_packet(&m_packet);
        m_packet.data = nullptr;
        m_packet.size = 0;

        m_interruptCallback.startTimer();
        ret = av_read_frame(m_formatCtx, &m_packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                logVerbose(QmlAVUtils::logId(this), QString("End of media"));
                setStatus(QMediaPlayer::EndOfMedia);
            } else {
                logError(QmlAVUtils::logId(this), QString("Unable read frame: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret));
                setStatus(QMediaPlayer::StalledMedia);
            }
            setPlaybackState(QMediaPlayer::StoppedState);
            av_packet_unref(&m_packet); // Important!
            break;
        }
        m_interruptCallback.stopTimer();

        if (m_packet.stream_index == m_videoDecoder.streamIndex()) {
            m_videoDecoder.decode(m_packet);
            clock = m_videoDecoder.clock();
            timeBase = m_videoDecoder.timeBase();

            logDebug(QmlAVUtils::logId(this),
                     QString("QmlAVDemuxer::run() : { m_videoDecoder.clock()->%1; Δ=%2 }").arg(clock).arg(clock - av_gettime()));

        } else if (m_packet.stream_index == m_audioDecoder.streamIndex()) {
            m_audioDecoder.decode(m_packet);
            if (!m_videoDecoder.codecIsOpen()) {
                clock = m_audioDecoder.clock();
                timeBase = m_audioDecoder.timeBase();
            }

            logDebug(QmlAVUtils::logId(this),
                     QString("QmlAVDemuxer::run() : { m_audioDecoder.clock()->%1; Δ=%2 }").arg(clock).arg(clock - av_gettime()));

        } else {
            logDebug(QmlAVUtils::logId(this), QString("QmlAVDemuxer::run() : { QThread::usleep(1); av_gettime()->%1 }").arg(av_gettime()));
            QThread::usleep(1);
        }

        // Primitive syncing for local playback
        int count = 0;
        if (!m_realtime) {
            while (clock - timeBase > av_gettime()) {
                if (isInterruptionRequested()) {
                    break;
                }

                QThread::usleep(timeBase);
                QCoreApplication::processEvents();
                ++count;
            }

            if (count > 0) {
                logDebug(QmlAVUtils::logId(this),
                         QString("QmlAVDemuxer::run() : { Loop of local playback sync: count=(%1); clock=%2 }").arg(count).arg(clock));
            }
        }

#ifndef Q_OS_ANDROID
        QCoreApplication::processEvents();
#endif

        av_packet_unref(&m_packet);
    }
}

bool QmlAVDemuxer::isRealtime(QUrl url)
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

bool QmlAVDemuxer::isInterruptionRequested() const
{
    return m_interruptionRequested || QThread::currentThread()->isInterruptionRequested();
}

void QmlAVDemuxer::setStatus(QMediaPlayer::MediaStatus status)
{
    if (m_status == status) {
        return;
    }

    m_status = status;

    emit statusChanged(status);
}

void QmlAVDemuxer::setPlaybackState(const QMediaPlayer::State state)
{
    if (m_playbackState == state) {
        return;
    }

    m_playbackState = state;

    emit playbackStateChanged(state);
}

bool QmlAVDemuxer::findStreams()
{
    m_videoStreams.clear();
    m_audioStreams.clear();

    if (!m_formatCtx) {
        return false;
    }

    AVMediaType type = AVMEDIA_TYPE_UNKNOWN;
    for (unsigned int i = 0; i < m_formatCtx->nb_streams; ++i) {
        type = m_formatCtx->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO) {
            m_videoStreams.append(m_formatCtx->streams[i]);
        } else if (type == AVMEDIA_TYPE_AUDIO) {
            m_audioStreams.append(m_formatCtx->streams[i]);
        }
    }

    if (m_videoStreams.isEmpty() && m_audioStreams.isEmpty()) {
        return false;
    }

    logDebug(QmlAVUtils::logId(this),
                         QString("Found %1 video and %2 audio streams").arg(m_videoStreams.count()).arg(m_audioStreams.count()));

    return true;
}
