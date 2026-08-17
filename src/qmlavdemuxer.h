#ifndef QMLAVDEMUXER_H
#define QMLAVDEMUXER_H

#include <QVideoFrame>
#include <QMediaPlayer>
#include <QVideoSurfaceFormat>
#include <QAudioOutput>

#include "qmlavmediacontextholder.h"
#include "qmlavoptions.h"
#include "qmlavthread.h"
#include "qmlavdecoder.h"

// NOTE: Public API for GUI thread only!
class QmlAVDemuxer : public QObject
{
    Q_OBJECT

public:
    QmlAVDemuxer(QObject *parent = nullptr);
    virtual ~QmlAVDemuxer();

    void load(const QUrl &url, const QmlAVOptions &avOptions);
    void start();

    QVariantMap stat() const;

signals:
    void playbackStateChanged(QMediaPlayer::State state);
    void mediaStatusChanged(QMediaPlayer::MediaStatus status);
    void frameFinished(const std::shared_ptr<QmlAVFrame> frame);

protected:
    auto &context() { return m_context; }

    bool isRealTime(QUrl url) const;
    bool isLoaded() const { return m_context->videoDecoder->isOpen() || m_context->audioDecoder->isOpen(); }
    void initDecoders(const QmlAVOptions &avOptions);

    void frameHandler(const std::shared_ptr<QmlAVFrame> frame);
    
private:
    QmlAVThreadLiveController<void> m_loaderThread;
    QmlAVThreadLiveController<QmlAVLoopController> m_demuxerThread;

    std::shared_ptr<QmlAVMediaContextHolder> m_context;

    friend class QmlAVDecoder;
};
Q_DECLARE_METATYPE(std::shared_ptr<QmlAVFrame>)

#endif // QMLAVDEMUXER_H
