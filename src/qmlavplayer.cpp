#include "qmlavplayer.h"

QmlAVPlayer::QmlAVPlayer(QObject *parent)
    : QObject(parent),
      m_demuxer(nullptr),
      m_videoSurface(nullptr),
      m_audioOutput(nullptr),
      m_autoLoad(true),
      m_autoPlay(false),
      m_loops(1),
      m_playbackState(QMediaPlayer::StoppedState),
      m_status(QMediaPlayer::UnknownMediaStatus),
      m_muted(false),
      m_hasVideo(false),
      m_hasAudio(false)
{
    qRegisterMetaType<QList<QVideoFrame::PixelFormat>>();

    m_audioDeviceInfo = QAudioDeviceInfo::defaultOutputDevice();

    m_playTimer.setSingleShot(true);
    connect(&m_playTimer, &QTimer::timeout, this, &QmlAVPlayer::play);

    m_thread.start();
}

QmlAVPlayer::~QmlAVPlayer()
{
    stop();
    m_thread.requestInterruption();
    m_thread.quit();
    m_thread.wait();
}

void QmlAVPlayer::play()
{
    if (load()) {
        emit demuxerStart();
    }
}

void QmlAVPlayer::stop()
{
    if (m_demuxer) {
        m_demuxer->requestInterruption();
        m_demuxer->disconnect();
        m_demuxer->deleteLater();
        m_demuxer = nullptr;
    }

    setPlaybackState(QMediaPlayer::StoppedState);
    setHasVideo(false);
    setHasAudio(false);

    if (m_videoSurface && m_videoSurface->isActive()) {
        m_videoSurface->stop();
    }

    if (m_audioOutput) {
        m_audioOutput->stop();
        delete m_audioOutput;
        m_audioOutput = nullptr;
    }
}

void QmlAVPlayer::setVideoSurface(QAbstractVideoSurface *surface)
{
    if (m_videoSurface != surface) {
        stop();
    }

    m_videoSurface = surface;

    if (m_videoSurface && m_autoPlay) {
        play();
    }
}

void QmlAVPlayer::setAVFormatOptions(const QVariantMap &options)
{
    if (m_avFormatOptions == options) {
        return;
    }

    stop();

    m_avFormatOptions = options;
    if (m_autoPlay) {
        play();
    } else if (m_autoLoad) {
        load();
    }

    emit avFormatOptionsChanged(m_avFormatOptions);
}

void QmlAVPlayer::setAutoLoad(bool autoLoad)
{
    if (m_autoLoad == autoLoad)
        return;

    m_autoLoad = autoLoad;
    if (m_autoLoad) {
        load();
    }

    emit autoLoadChanged(m_autoLoad);
}

void QmlAVPlayer::setAutoPlay(bool autoPlay)
{
    if (m_autoPlay == autoPlay)
        return;

    m_autoPlay = autoPlay;
    if (m_autoPlay) {
        play();
    }

    emit autoPlayChanged(m_autoPlay);
}

void QmlAVPlayer::setSource(QUrl source)
{
    if (m_source == source)
        return;

    stop();

    m_source = source;
    if (m_autoPlay) {
        play();
    } else if (m_autoLoad) {
        load();
    }

    emit sourceChanged(m_source);
}

void QmlAVPlayer::setVolume(const QVariant &volume)
{
    if (m_volume == volume)
        return;

    m_volume = volume;

    if (m_audioOutput) {
        m_audioOutput->setVolume(QAudio::convertVolume(m_volume.toReal(),
                                                       QAudio::LogarithmicVolumeScale,
                                                       QAudio::LinearVolumeScale));
    }

    emit volumeChanged(m_volume);
}

bool QmlAVPlayer::load()
{
    if (!m_demuxer && m_source.isValid()) {
        m_demuxer = new QmlAVDemuxer();
        m_demuxer->moveToThread(&m_thread);
        connect(&m_thread, &QThread::finished, m_demuxer, &QObject::deleteLater);

        connect(this, &QmlAVPlayer::demuxerLoad, m_demuxer, &QmlAVDemuxer::load);
        connect(this, &QmlAVPlayer::demuxerSetSupportedPixelFormats, m_demuxer, &QmlAVDemuxer::setSupportedPixelFormats);
        connect(this, &QmlAVPlayer::demuxerStart, m_demuxer, &QmlAVDemuxer::run);
        connect(this, &QmlAVPlayer::demuxerSetHandledTime, m_demuxer, &QmlAVDemuxer::setHandledTime);

        connect(m_demuxer, &QmlAVDemuxer::frameFinished, this, &QmlAVPlayer::frameHandler);
        connect(m_demuxer, &QmlAVDemuxer::videoFormatChanged, this, &QmlAVPlayer::setVideoFormat);
        connect(m_demuxer, &QmlAVDemuxer::audioFormatChanged, this, &QmlAVPlayer::setAudioFormat);
        connect(m_demuxer, &QmlAVDemuxer::playbackStateChanged, this, &QmlAVPlayer::setPlaybackState);
        connect(m_demuxer, &QmlAVDemuxer::statusChanged, this, &QmlAVPlayer::setStatus);

        emit demuxerLoad(m_source, m_avFormatOptions);
        if (m_videoSurface) {
            emit demuxerSetSupportedPixelFormats(m_videoSurface->supportedPixelFormats());
        }

        return true;
    }

    return false;
}

void QmlAVPlayer::stateMachine()
{
    QmlAVUtils::logDebug(QmlAVUtils::logId(m_demuxer),
                         QString("QmlAVPlayer::stateMachine() => %2:%3").arg(m_status).arg(m_playbackState));

    if (m_playbackState == QMediaPlayer::PlayingState && m_status == QMediaPlayer::BufferedMedia) {
        if (m_videoSurface && !m_videoSurface->isActive() && m_videoFormat.isValid()) {
            m_videoFormat = m_videoSurface->nearestFormat(m_videoFormat);
            if (m_videoFormat.isValid()) {
                m_videoSurface->start(m_videoFormat);
                setHasVideo(true);
            } else {
                QmlAVUtils::logError(QmlAVUtils::logId(m_demuxer), "Invalid surface format!");
                stop();
            }
        }
        if (!m_audioOutput && m_audioFormat.isValid()) {
            m_audioFormat = m_audioDeviceInfo.nearestFormat(m_audioFormat);
            m_audioOutput = new QAudioOutput(m_audioDeviceInfo, m_audioFormat, this);
            m_audioOutput->setVolume(QAudio::convertVolume(m_volume.toReal(),
                                                           QAudio::LogarithmicVolumeScale,
                                                           QAudio::LinearVolumeScale));
            // NOTE: When use start() with a internal pointer to QIODevice we have a bug https://bugreports.qt.io/browse/QTBUG-60575 "infinite loop"
            // at a volume other than 1.0f. In addition, the use of a buffer (as queue) improves sound quality.
            m_audioOutput->start(&m_audioQueue);
            setHasAudio(true);
        }
    } else if (m_playbackState == QMediaPlayer::PausedState) {
        // TODO: Implement it
        QmlAVUtils::logDebug(QmlAVUtils::logId(m_demuxer), QString("%1:%2 Not implemented!").arg(__FILE__).arg(__LINE__));
    } else if (m_playbackState == QMediaPlayer::StoppedState) {
        switch (m_status) {
        case QMediaPlayer::NoMedia:
        case QMediaPlayer::StalledMedia:
        case QMediaPlayer::EndOfMedia:
        case QMediaPlayer::InvalidMedia:
            stop();
            if (m_loops == -1 /*MediaPlayer.Infinite*/) {
                m_playTimer.start(1000);
            }
            break;
        default:
            break;
        }
    }
}

void QmlAVPlayer::frameHandler(const std::shared_ptr<QmlAVFrame> frame)
{
    emit demuxerSetHandledTime(frame->startTime());

    if (m_playbackState == QMediaPlayer::PlayingState) {
        if (frame->type() == QmlAVFrame::TypeVideo) {
            if (m_videoSurface && frame->isValid()) {
                m_videoSurface->present(std::static_pointer_cast<QmlAVVideoFrame>(frame)->toVideoFrame());
            }
        } else if (frame->type() == QmlAVFrame::TypeAudio) {
            if (m_audioOutput && frame->isValid()) {
                m_audioQueue.push(std::static_pointer_cast<QmlAVAudioFrame>(frame));
            }
        }
    }
}

void QmlAVPlayer::setVideoFormat(const QVideoSurfaceFormat &format)
{
    if (m_videoFormat == format) {
        return;
    }

    m_videoFormat = format;

    stateMachine();
}

void QmlAVPlayer::setAudioFormat(const QAudioFormat &format)
{
    if (m_audioFormat == format) {
        return;
    }

    m_audioFormat = format;
}

void QmlAVPlayer::setPlaybackState(const QMediaPlayer::State state)
{
    if (m_playbackState == state) {
        return;
    }

    m_playbackState = state;

    if (sender()) {
        stateMachine();
    }

    emit playbackStateChanged(m_playbackState);
}

void QmlAVPlayer::setStatus(const QMediaPlayer::MediaStatus status)
{
    if (m_status == status) {
        return;
    }

    m_status = status;

    stateMachine();

    emit statusChanged(m_status);
}

void QmlAVPlayer::setHasVideo(bool hasVideo)
{
    if (m_hasVideo == hasVideo) {
        return;
    }

    m_hasVideo = hasVideo;

    emit hasVideoChanged(m_hasVideo);
}

void QmlAVPlayer::setHasAudio(bool hasAudio)
{
    if (m_hasAudio == hasAudio) {
        return;
    }

    m_hasAudio = hasAudio;

    emit hasAudioChanged(m_hasAudio);
}
