#include "qmlavplayer.h"

QmlAVPlayer::QmlAVPlayer(QObject *parent)
    : QObject(parent),
      m_complete(false),
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
}

QmlAVPlayer::~QmlAVPlayer()
{
    stop();
}

void QmlAVPlayer::componentComplete()
{
    if (m_autoPlay) {
        play();
    } else if (m_autoLoad) {
        load();
    }

    m_complete = true;
}

void QmlAVPlayer::play()
{
    if (load()) {
        m_demuxer->start();
    }
}

void QmlAVPlayer::stop()
{
    if (m_demuxer) {
        disconnect(m_demuxer, nullptr, this, nullptr);
        delete m_demuxer;
        m_demuxer = nullptr;
    }

    if (m_videoSurface && m_videoSurface->isActive()) {
        m_videoSurface->stop();
    }

    if (m_audioOutput) {
        m_audioFormat = {};

        m_audioOutput->stop();
        delete m_audioOutput;
        m_audioOutput = nullptr;
    }

    setPlaybackState(QMediaPlayer::StoppedState);
    setHasVideo(false);
    setHasAudio(false);
}

void QmlAVPlayer::setVideoSurface(QAbstractVideoSurface *surface)
{
    if (m_videoSurface != surface) {
        stop();
    }

    m_videoSurface = surface;
}

void QmlAVPlayer::frameHandler(const std::shared_ptr<QmlAVFrame> frame)
{
    if (m_playbackState == QMediaPlayer::PlayingState) {
        if (frame->type() == QmlAVFrame::TypeVideo) {
            auto vf = std::static_pointer_cast<QmlAVVideoFrame>(frame);
            QVideoFrame qvf = *vf;

            if (m_videoSurface && frame->isValid()) {
                if (!m_videoSurface->isActive()) {
                    QVideoSurfaceFormat sf(qvf.size(), qvf.pixelFormat(), qvf.handleType());
                    sf.setPixelAspectRatio(vf->sampleAspectRatio());
                    sf.setYCbCrColorSpace(vf->colorSpace());
                    logDebug() << "Starting with: "
                              << "QVideoSurfaceFormat(" << sf.pixelFormat() << ", " << sf.frameSize()
                              << ", viewport=" << sf.viewport() << ", pixelAspectRatio=" << sf.pixelAspectRatio()
                              << ", handleType=" << sf.handleType() <<  ", yCbCrColorSpace=" << sf.yCbCrColorSpace()
                              << ')';
                    if (!m_videoSurface->start(sf)) {
                        logCritical() << "Error starting the video surface presenting frames.";
                    }
                }
                if (m_videoSurface->isActive()) {
                    if (!m_videoSurface->present(qvf)) {
                        stop();
                    }
                }
            }
        } else if (frame->type() == QmlAVFrame::TypeAudio) {
            if (m_audioOutput && frame->isValid()) {
                m_audioQueue.push(std::static_pointer_cast<QmlAVAudioFrame>(frame));
            }
        }
    }
}

void QmlAVPlayer::setAudioFormat(const QAudioFormat &format)
{
    if (m_audioFormat == format) {
        return;
    }

    m_audioFormat = format;
}

void QmlAVPlayer::setAVOptions(const QVariantMap &avOptions)
{
    if (m_avOptions == avOptions) {
        return;
    }

    m_avOptions = avOptions;

    reset();

    emit avOptionsChanged(avOptions);
}

void QmlAVPlayer::setAutoLoad(bool autoLoad)
{
    if (m_autoLoad == autoLoad)
        return;

    m_autoLoad = autoLoad;

    if (m_complete && autoLoad) {
        load();
    }

    emit autoLoadChanged(autoLoad);
}

void QmlAVPlayer::setAutoPlay(bool autoPlay)
{
    if (m_autoPlay == autoPlay)
        return;

    m_autoPlay = autoPlay;

    if (m_complete && autoPlay) {
        play();
    }

    emit autoPlayChanged(autoPlay);
}

void QmlAVPlayer::setSource(QUrl source)
{
    if (m_source == source)
        return;

    m_source = source;

    reset();

    emit sourceChanged(source);
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

    emit playbackStateChanged(state);
}

void QmlAVPlayer::setStatus(const QMediaPlayer::MediaStatus status)
{
    if (m_status == status) {
        return;
    }

    m_status = status;

    stateMachine();

    emit statusChanged(status);
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

    emit volumeChanged(volume);
}

void QmlAVPlayer::setHasVideo(bool hasVideo)
{
    if (m_hasVideo == hasVideo) {
        return;
    }

    m_hasVideo = hasVideo;

    logDebug() << QString("setHasVideo(%1)").arg(hasVideo);

    emit hasVideoChanged(hasVideo);
}

void QmlAVPlayer::setHasAudio(bool hasAudio)
{
    if (m_hasAudio == hasAudio) {
        return;
    }

    m_hasAudio = hasAudio;

    logDebug() << QString("setHasAudio(%1)").arg(m_hasAudio);

    emit hasAudioChanged(hasAudio);
}

bool QmlAVPlayer::load()
{
    if (!m_demuxer && m_source.isValid()) {
        m_demuxer = new QmlAVDemuxer();

        connect(m_demuxer, &QmlAVDemuxer::frameFinished, this, &QmlAVPlayer::frameHandler);
        connect(m_demuxer, &QmlAVDemuxer::audioFormatChanged, this, &QmlAVPlayer::setAudioFormat);
        connect(m_demuxer, &QmlAVDemuxer::playbackStateChanged, this, &QmlAVPlayer::setPlaybackState);
        connect(m_demuxer, &QmlAVDemuxer::mediaStatusChanged, this, &QmlAVPlayer::setStatus);

        m_demuxer->load(m_source, m_avOptions);

        return true;
    }

    return false;
}

void QmlAVPlayer::stateMachine()
{
    logDebug() << QString("stateMachine[m_status=%1; m_playbackState=%2]()").arg(m_status).arg(m_playbackState);

    if (m_playbackState == QMediaPlayer::PlayingState && m_status == QMediaPlayer::BufferedMedia) {
        if (m_videoSurface && !m_videoSurface->isActive()) {
            setHasVideo(true);
        }
        if (!m_audioOutput && m_audioFormat.isValid()) {
            m_audioOutput = new QAudioOutput(m_audioDeviceInfo, m_audioFormat);
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
        logInfo() << QString("%1:%2 Not implemented!").arg(__FILE__).arg(__LINE__);
    } else if (m_playbackState == QMediaPlayer::StoppedState) {
        switch (m_status) {
        case QMediaPlayer::NoMedia:
        case QMediaPlayer::StalledMedia:
        case QMediaPlayer::EndOfMedia:
        case QMediaPlayer::InvalidMedia: {
            // Internal demuxer interrupt
            if (m_demuxer) {
                stop();

                if (m_loops == -1 /*MediaPlayer.Infinite*/) {
                    m_playTimer.start(1000);
                }
            }

            break;
        }
        default:
            break;
        }
    }
}

void QmlAVPlayer::reset()
{
    if (m_complete) {
        stop();

        if (m_autoPlay) {
            play();
        } else if (m_autoLoad) {
            load();
        }
    }
}
