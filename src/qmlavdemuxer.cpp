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
      m_avInputFormat(nullptr),
      m_avFormatOptions(nullptr),
      m_videoDecoder(this),
      m_audioDecoder(this),
      m_playbackState(QMediaPlayer::StoppedState),
      m_status(QMediaPlayer::UnknownMediaStatus),
      m_running(false),
      m_interruptionRequested(false)
{
    logDebug(this, "QmlAVDemuxer()");
}

QmlAVDemuxer::~QmlAVDemuxer()
{
    requestInterruption();

    avformat_close_input(&m_formatCtx);
    av_dict_free(&m_avFormatOptions);

    logDebug(this, "~QmlAVDemuxer()");
}

void QmlAVDemuxer::requestInterruption()
{
    m_interruptCallback.requestInterruption();
    m_interruptionRequested = true;

    logDebug(this, "requestInterruption()");
}

bool QmlAVDemuxer::wait(unsigned long time)
{
    while (m_running) {
        if (time == 0) {
            return false;
        }
        QThread::msleep(1);
        --time;
    }

    return true;
}

void QmlAVDemuxer::load(const QUrl &url, const QVariantMap &formatOptions)
{
    int ret;
    QString source(url.toString());

    if (m_formatCtx) {
        return;
    }

    if (source.isEmpty()) {
        logVerbose(this, QString("Source is emty!"));
        setStatus(QMediaPlayer::NoMedia);
        setPlaybackState(QMediaPlayer::StoppedState);
        return;
    }

#if (LIBAVFORMAT_VERSION_MAJOR < 58)
    av_register_all();
    avformat_network_init();
#endif

    m_formatCtx = avformat_alloc_context();
    if (!m_formatCtx) {
        logError(this, "Could not allocate AVFormatContext");
        return;
    }

    // TODO: Only Unix systems are supported
    if (url.isLocalFile()) {
        avdevice_register_all();
        source = url.toLocalFile();
    }

    parseAVFormatOptions(formatOptions);

    m_realtime = isRealtime(url);
    m_videoDecoder.setAsyncMode(m_realtime);
    m_audioDecoder.setAsyncMode(m_realtime);

    m_formatCtx->interrupt_callback = m_interruptCallback;

    setStatus(QMediaPlayer::LoadingMedia);
    setPlaybackState(QMediaPlayer::StoppedState);

    m_interruptCallback.startTimer();
    ret = avformat_open_input(&m_formatCtx, source.toUtf8(), m_avInputFormat, &m_avFormatOptions);
    if (ret < 0) {
        logError(this, QString("Unable to open input file: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret));
        setStatus(QMediaPlayer::InvalidMedia);
        setPlaybackState(QMediaPlayer::StoppedState);
        return;
    }
    m_interruptCallback.stopTimer();

    m_interruptCallback.startTimer();
    ret = avformat_find_stream_info(m_formatCtx, nullptr);
    if (ret < 0) {
        logError(this, QString("Cannot find stream information: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret));
        setStatus(QMediaPlayer::InvalidMedia);
        setPlaybackState(QMediaPlayer::StoppedState);
        return;
    }
    m_interruptCallback.stopTimer();

    if (QmlAVUtils::verboseLevel() > QmlAVUtils::LogError) {
        av_dump_format(m_formatCtx, 0, source.toUtf8(), 0);
    }

    if (!findStreams()) {
        logError(this, QString("Unable find valid stream"));
        setStatus(QMediaPlayer::InvalidMedia);
        setPlaybackState(QMediaPlayer::StoppedState);
        return;
    }

    if (m_videoStreams.count() > 0) {
        // Open first video stream
        if (m_videoDecoder.openCodec(m_videoStreams.value(0))) {
            m_videoDecoder.setStartTime(av_gettime());

            logDebug(this, QString("m_videoDecoder.openCodec()->true : { m_videoDecoder.startTime()->%1 }").arg(m_videoDecoder.startTime()));
        }
    }
    if (m_audioStreams.count() > 0) {
        // Open first audio stream
        if (m_audioDecoder.openCodec(m_audioStreams.value(0))) {
            m_audioDecoder.setStartTime(av_gettime());

            logDebug(this, QString("m_audioDecoder.openCodec()->true : { m_audioDecoder.startTime()->%1 }").arg(m_audioDecoder.startTime()));
        }
    }
    if (!m_videoDecoder.codecIsOpen() && !m_audioDecoder.codecIsOpen()) {
        logError(this, QString("Unable open any codec"));
        setStatus(QMediaPlayer::InvalidMedia);
        setPlaybackState(QMediaPlayer::StoppedState);
        return;
    }

    if (m_audioDecoder.codecIsOpen()) {
        emit audioFormatChanged(m_audioDecoder.audioFormat());
    }

    setStatus(QMediaPlayer::LoadedMedia);

    logDebug(this, QString("Media loaded successfully!"));
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
    AVPacket *avPacket;
    qint64 clock = 0;
    double timeBase = 0;

    if (m_playbackState != QMediaPlayer::StoppedState || m_status != QMediaPlayer::LoadedMedia) {
        return;
    }

    setPlaybackState(QMediaPlayer::PlayingState);

    // NOTE: We do not use buffering to reduce latency
    setStatus(QMediaPlayer::BufferedMedia);

    if (m_videoDecoder.codecIsOpen()) {
        timeBase = m_videoDecoder.timeBase();
    } else {
        timeBase = m_audioDecoder.timeBase();
    }

    logDebug(this, QString("run() : { timeBase=%1 }").arg(timeBase));

    while (!isInterruptionRequested() && m_playbackState == QMediaPlayer::PlayingState) {
        m_running = true;

        if (!m_formatCtx) {
            break;
        }

        avPacket = av_packet_alloc();
        if (!avPacket) {
            logError(this, "Could not allocate AVPacket");
            break;
        }

        m_interruptCallback.startTimer();
        ret = av_read_frame(m_formatCtx, avPacket);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                logVerbose(this, QString("End of media"));
                setStatus(QMediaPlayer::EndOfMedia);
            } else {
                logError(this, QString("Unable read frame: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret));
                setStatus(QMediaPlayer::StalledMedia);
            }
            setPlaybackState(QMediaPlayer::StoppedState);
            av_packet_free(&avPacket); // Important!
            break;
        }
        m_interruptCallback.stopTimer();

        if (avPacket->stream_index == m_videoDecoder.streamIndex()) {
            m_videoDecoder.decodeAVPacket(avPacket);
            clock = m_videoDecoder.clock();

            logDebug(this,
                     QString("run() : { m_videoDecoder.clock()->%1; Δ=%2 }").arg(clock).arg(clock - av_gettime()));

        } else if (avPacket->stream_index == m_audioDecoder.streamIndex()) {
            m_audioDecoder.decodeAVPacket(avPacket);
            if (!m_videoDecoder.codecIsOpen()) {
                clock = m_audioDecoder.clock();
            }

            logDebug(this,
                     QString("run() : { m_audioDecoder.clock()->%1; Δ=%2 }").arg(clock).arg(clock - av_gettime()));

        } else {
            logDebug(this, QString("run() : { QThread::usleep(1); av_gettime()->%1 }").arg(av_gettime()));
            QThread::usleep(1);
        }

        // Primitive syncing for local playback
        int count = 0;
        if (!m_realtime) {
            while (clock > av_gettime()) {
                if (isInterruptionRequested()) {
                    av_packet_free(&avPacket);
                    break;
                }

                QThread::usleep(timeBase);
                QCoreApplication::processEvents();
                ++count;
            }

            if (count > 0) {
                logDebug(this,
                         QString("run() : { Loop of local playback sync: count=(%1); clock=%2 }").arg(count).arg(clock));
            }
        }

        QCoreApplication::processEvents();
        av_packet_free(&avPacket);
    }

    m_running = false;
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

void QmlAVDemuxer::parseAVFormatOptions(const QVariantMap &formatOptions)
{
    QMapIterator<QString, QVariant> i(formatOptions);

    av_dict_free(&m_avFormatOptions);

    while (i.hasNext()) {
        i.next();

        QString key{i.key()}, value{i.value().toString()};

        if (key == "f") {
            m_avInputFormat = av_find_input_format(value.toUtf8());
            if (!m_avInputFormat) {
                logError(this, QString("Unknown input format: -f %1. Ignore this.").arg(value));
            }

            continue;
        }

        av_dict_set(&m_avFormatOptions, key.toUtf8(), value.toUtf8(), 0);
        logDebug(this, QString("Added AVFormat option: -%1 %2").arg(key, value));
    }
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

    logDebug(this,
                         QString("Found %1 video and %2 audio streams").arg(m_videoStreams.count()).arg(m_audioStreams.count()));

    return true;
}
