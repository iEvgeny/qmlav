#ifndef QMLAVPLAYER_H
#define QMLAVPLAYER_H

#include <QQmlParserStatus>
#include <QTimer>

#include "qmlavcompat.h"
#include "qmlavframe.h"
#include "qmlavdemuxer.h"
#include "qmlavaudioiodevice.h"
#include "qmlavpropertyhelpers.h"

class QmlAVPlayer : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    Q_PROPERTY(QVideoSink *videoSink READ videoSink WRITE setVideoSink)
#else
    Q_PROPERTY(QAbstractVideoSurface *videoSurface READ videoSurface WRITE setVideoSurface)
#endif

    QMLAV_PROPERTY_DECL(QVariantMap, avOptions, setAVOptions, avOptionsChanged);
    QMLAV_PROPERTY_DECL(bool, autoLoad, setAutoLoad, autoLoadChanged) = true;
    QMLAV_PROPERTY_DECL(bool, autoPlay, setAutoPlay, autoPlayChanged) = false;
    QMLAV_PROPERTY(int, loops, setLoops, loopsChanged) = 1; // NOTE: Implemented partially (Once playing and infinite loop behavior)
    QMLAV_PROPERTY_DECL(QUrl, source, setSource, sourceChanged);
    QMLAV_PROPERTY_READONLY(QmlAVPlaybackState, playbackState, playbackStateChanged) = QMediaPlayer::StoppedState;
    QMLAV_PROPERTY_READONLY(QMediaPlayer::MediaStatus, status, statusChanged) = QMediaPlayer::NoMedia;
    QMLAV_PROPERTY_READONLY(QVariant, bufferProgress, bufferProgressChanged) = 1.0; // TODO:
    QMLAV_PROPERTY(bool, muted, setMuted, mutedChanged) = false; // TODO:
    QMLAV_PROPERTY_DECL(double, volume, setVolume, volumeChanged) = 0.0;
    QMLAV_PROPERTY_READONLY(bool, hasVideo, hasVideoChanged) = false;
    QMLAV_PROPERTY_READONLY(bool, hasAudio, hasAudioChanged) = false;

public:
    QmlAVPlayer(QObject *parent = nullptr);
    ~QmlAVPlayer() override;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QVideoSink *videoSink() const { return m_videoSink; }
#else
    QAbstractVideoSurface *videoSurface() const { return m_videoSurface; }
#endif
    virtual void classBegin() override {}
    virtual void componentComplete() override;

signals:
    void videoFramePresented();

public slots:
    void play();
    void stop();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void setVideoSink(QVideoSink *sink);
#else
    void setVideoSurface(QAbstractVideoSurface *surface);
#endif
    void frameHandler(const std::shared_ptr<QmlAVFrame> frame);

protected:
    bool load();
    void stateMachine();
    void reset();

    void setPlaybackState(const QmlAVPlaybackState state);
    void setStatus(const QMediaPlayer::MediaStatus status);
    void setHasVideo(bool hasVideo);
    void setHasAudio(bool hasAudio);

private:
    bool m_complete;
    QmlAVDemuxer *m_demuxer;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QVideoSink *m_videoSink;
#else
    QAbstractVideoSurface *m_videoSurface;
#endif
    QTimer m_playTimer;

    QmlAVAudioIODevice m_audioIODevice;
    QmlAVAudioOutput *m_audioOutput;
};

#endif // QMLAVPLAYER_H
