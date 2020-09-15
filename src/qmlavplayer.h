#ifndef QMLAVPLAYER_H
#define QMLAVPLAYER_H

#include <QtCore>
#include <QMediaPlayer>
#include <QAbstractVideoSurface>
#include <QVideoSurfaceFormat>
#include <QAudioOutput>

#include "qmlavdemuxer.h"
#include "qmlavaudioqueue.h"

#define Q_PROPERTY_WRITE_IMPL(type, name, write, notify) \
    void write(const type &var) { \
        if (m_##name == var) \
            return; \
        m_##name = var; \
        emit notify(var); \
    }

class QmlAVPlayer : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractVideoSurface *videoSurface READ videoSurface WRITE setVideoSurface)

    Q_PROPERTY(QVariantMap avFormatOptions READ avFormatOptions WRITE setAVFormatOptions NOTIFY avFormatOptionsChanged)
    Q_PROPERTY(bool autoLoad READ autoLoad WRITE setAutoLoad NOTIFY autoLoadChanged)
    Q_PROPERTY(bool autoPlay READ autoPlay WRITE setAutoPlay NOTIFY autoPlayChanged)
    Q_PROPERTY(int loops READ loops WRITE setLoops NOTIFY loopsChanged) // NOTE: Implemented partially (Once playing and infinite loop behavior)
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QMediaPlayer::State playbackState READ playbackState NOTIFY playbackStateChanged)
    Q_PROPERTY(QMediaPlayer::MediaStatus status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariant bufferProgress READ bufferProgress NOTIFY bufferProgressChanged) // TODO:
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged) // TODO:
    Q_PROPERTY(QVariant volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged)
    Q_PROPERTY(bool hasAudio READ hasAudio NOTIFY hasAudioChanged)

public:
    explicit QmlAVPlayer(QObject *parent = nullptr);
    ~QmlAVPlayer();

    QAbstractVideoSurface *videoSurface() const { return m_videoSurface; }

    QVariantMap avFormatOptions() const { return m_avFormatOptions; }
    bool autoLoad() const { return m_autoLoad; }
    bool autoPlay() const { return m_autoPlay; }
    int loops() const { return m_loops; }
    QUrl source() const { return m_source; }
    QMediaPlayer::State playbackState() const { return m_playbackState; }
    QMediaPlayer::MediaStatus status() const { return m_status; }
    QVariant bufferProgress() const { return 1.0; }
    bool muted() const { return m_muted; }
    QVariant volume() const { return m_volume; }
    bool hasVideo() const { return m_hasVideo; }
    bool hasAudio() const { return m_hasAudio; }

public slots:
    void play();
    void stop();
    void setVideoSurface(QAbstractVideoSurface *surface);

    void setAVFormatOptions(const QVariantMap &options);
    void setAutoLoad(bool autoLoad);
    void setAutoPlay(bool autoPlay);
    Q_PROPERTY_WRITE_IMPL(int, loops, setLoops, loopsChanged)
    void setSource(QUrl source);
    Q_PROPERTY_WRITE_IMPL(bool, muted, setMuted, mutedChanged)
    void setVolume(const QVariant &volume);

signals:
    void demuxerLoad(const QUrl &source, const QVariantMap &formatOptions = QVariantMap());
    void demuxerSetSupportedPixelFormats(const QList<QVideoFrame::PixelFormat> &formats);
    void demuxerStart();

    void autoLoadChanged(bool autoLoad);
    void autoPlayChanged(bool autoPlay);
    void loopsChanged(int loops);
    void sourceChanged(QUrl source);
    void playbackStateChanged(QMediaPlayer::State playbackState);
    void statusChanged(QMediaPlayer::MediaStatus status);
    void bufferProgressChanged(QVariant bufferProgress);
    void mutedChanged(bool muted);
    void volumeChanged(QVariant volume);
    void hasVideoChanged(bool hasVideo);
    void hasAudioChanged(bool hasAudio);
    void avFormatOptionsChanged(QVariantMap avFormatOptions);

protected:
    bool load();
    void stateMachine();

protected slots:
    void frameHandler(const std::shared_ptr<QmlAVFrame> frame);
    void setVideoFormat(const QVideoSurfaceFormat &format);
    void setAudioFormat(const QAudioFormat &format);
    void setPlaybackState(const QMediaPlayer::State state);
    void setStatus(const QMediaPlayer::MediaStatus status);
    void setHasVideo(bool hasVideo);
    void setHasAudio(bool hasAudio);

private:
    QThread m_thread;
    QmlAVDemuxer *m_demuxer;
    QVideoSurfaceFormat m_videoFormat;
    QAbstractVideoSurface *m_videoSurface;
    QmlAVAudioQueue m_audioQueue;
    QAudioDeviceInfo m_audioDeviceInfo;
    QAudioFormat m_audioFormat;
    QAudioOutput *m_audioOutput;
    QTimer m_playTimer;

    QVariantMap m_avFormatOptions;
    bool m_autoLoad;
    bool m_autoPlay;
    int m_loops;
    QUrl m_source;
    QMediaPlayer::State m_playbackState;
    QMediaPlayer::MediaStatus m_status;
    bool m_muted;
    QVariant m_volume;
    bool m_hasVideo;
    bool m_hasAudio;
};

#endif // QMLAVPLAYER_H
