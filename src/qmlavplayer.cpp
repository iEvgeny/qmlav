#include "qmlavplayer.h"

QmlAVPlayer::QmlAVPlayer(QObject *parent)
    : QObject(parent)
    , m_complete(false)
    , m_demuxer(nullptr)
    , m_videoSurface(nullptr)
    , m_audioOutput(nullptr)
    , m_autoLoad(true)
    , m_autoPlay(false)
    , m_loops(1)
    , m_playbackState(QMediaPlayer::StoppedState)
    , m_status(QMediaPlayer::NoMedia)
    , m_muted(false)
    , m_volume(0.0)
    , m_hasVideo(false)
    , m_hasAudio(false)
{
    qRegisterMetaType<QList<QVideoFrame::PixelFormat>>();

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
    logDebug() << "play()";

    if (load()) {
        m_demuxer->start();
    }
}

void QmlAVPlayer::stop()
{
    logDebug() << "stop()";

    if (m_demuxer) {
        disconnect(m_demuxer, nullptr, this, nullptr);
        delete m_demuxer;
        m_demuxer = nullptr;
    }

    if (m_videoSurface && m_videoSurface->isActive()) {
        m_videoSurface->stop();
    }

    if (m_audioOutput) {
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

            if (m_videoSurface) {
                if (!m_videoSurface->isActive()) {
                    QVideoSurfaceFormat f(qvf.size(), qvf.pixelFormat(), qvf.handleType());
                    f.setPixelAspectRatio(vf->sampleAspectRatio());
                    f.setYCbCrColorSpace(vf->colorSpace());
                    logDebug() << "Starting with: "
                               << "QVideoSurfaceFormat(" << f.pixelFormat() << ", " << f.frameSize()
                               << ", viewport=" << f.viewport() << ", pixelAspectRatio=" << f.pixelAspectRatio()
                               << ", handleType=" << f.handleType() <<  ", yCbCrColorSpace=" << f.yCbCrColorSpace()
                               << ')';
                    if (!m_videoSurface->start(f)) {
                        logCritical() << "Error starting the video surface presenting frames.";
                        return;
                    }

                    setHasVideo(true);
                }

                if (m_videoSurface->isActive()) {
                    if (!m_videoSurface->present(qvf)) {
                        stop();
                    }
                }
            }
        } else if (frame->type() == QmlAVFrame::TypeAudio) {
            auto af = std::static_pointer_cast<QmlAVAudioFrame>(frame);

            m_audioDevice.enqueue(af);

            if (!m_audioOutput) {
                if (af->audioFormat().isValid()) {
                    auto f = af->audioFormat();
                    logDebug() << "Starting with: " << f;
                    auto outputDevice = QAudioDeviceInfo::defaultOutputDevice();
                    m_audioOutput = new QAudioOutput(outputDevice, f);
                    m_audioOutput->setVolume(QAudio::convertVolume(m_volume,
                                                                   QAudio::LogarithmicVolumeScale,
                                                                   QAudio::LinearVolumeScale));
                    // NOTE: When use start() with a internal pointer to QIODevice we have a bug https://bugreports.qt.io/browse/QTBUG-60575 "infinite loop"
                    // at a volume other than 1.0f. In addition, the use of a buffer (as queue) improves sound quality.
                    m_audioOutput->start(&m_audioDevice);
                    setHasAudio(true);
                }
            }
        }
    }
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
    if (m_autoLoad == autoLoad) {
        return;
}

    m_autoLoad = autoLoad;

    if (m_complete && autoLoad) {
        load();
    }

    emit autoLoadChanged(autoLoad);
}

void QmlAVPlayer::setAutoPlay(bool autoPlay)
{
    if (m_autoPlay == autoPlay) {
        return;
}

    m_autoPlay = autoPlay;

    if (m_complete && autoPlay) {
        play();
    }

    emit autoPlayChanged(autoPlay);
}

void QmlAVPlayer::setSource(QUrl source)
{
    if (m_source == source) {
        return;
    }

    logDebug() << QString("setSource(source=%1)").arg(source.toDisplayString());

    m_source = source;

    reset();

    emit sourceChanged(source);
}

void QmlAVPlayer::setPlaybackState(const QMediaPlayer::State state)
{
    if (m_playbackState == state) {
        return;
    }

    logDebug() << QString("setPlaybackState(state=%1)").arg(state);

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

    logDebug() << QString("setStatus(status=%1)").arg(status);

    m_status = status;

    stateMachine();

    emit statusChanged(status);
}

void QmlAVPlayer::setVolume(const QVariant &volume)
{
    if (qFuzzyCompare(m_volume, volume.toDouble())) {
        return;
    }

    m_volume = volume.toDouble();

    if (m_audioOutput) {
        m_audioOutput->setVolume(QAudio::convertVolume(m_volume,
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

    logDebug() << QString("setHasVideo(hasVideo=%1)").arg(hasVideo);

    m_hasVideo = hasVideo;

    emit hasVideoChanged(hasVideo);
}

void QmlAVPlayer::setHasAudio(bool hasAudio)
{
    if (m_hasAudio == hasAudio) {
        return;
    }

    logDebug() << QString("setHasAudio(hasAudio=%1)").arg(hasAudio);

    m_hasAudio = hasAudio;

    emit hasAudioChanged(hasAudio);
}

bool QmlAVPlayer::load()
{
    if (!m_demuxer && m_source.isValid()) {
        m_demuxer = new QmlAVDemuxer();

        connect(m_demuxer, &QmlAVDemuxer::frameFinished, this, &QmlAVPlayer::frameHandler);
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

    if (m_playbackState == QMediaPlayer::PausedState) {
        // TODO: Implement it
        logInfo() << QString("%1:%2 Not implemented!").arg(__FILE__).arg(__LINE__);
    } else if (m_playbackState == QMediaPlayer::StoppedState) {
        switch (m_status) {
        case QMediaPlayer::NoMedia:
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
