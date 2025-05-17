#ifndef QMLAVMEDIACONTEXTHOLDER_H
#define QMLAVMEDIACONTEXTHOLDER_H

extern "C" {
#include <libavformat/avformat.h>
}

#include "qmlavdecoder.h"

class QmlAVDemuxer;

class QmlAVMediaContextHolder : public std::enable_shared_from_this<QmlAVMediaContextHolder>
{
public:
    QmlAVMediaContextHolder(QmlAVDemuxer *parent) : demuxer(parent) {
        avFormatCtx = avformat_alloc_context();

        videoDecoder = new QmlAVVideoDecoder(this);
        audioDecoder = new QmlAVAudioDecoder(this);
    }
    virtual ~QmlAVMediaContextHolder() {
        delete videoDecoder;
        delete audioDecoder;

        avformat_close_input(&avFormatCtx);
    }
    
    AVFormatContext *avFormatCtx = nullptr;
    QmlAVDecoder::Clock clock;

    // NOTE: Be careful! Life time is not controlled.
    QmlAVDemuxer *demuxer = nullptr;

    QmlAVVideoDecoder *videoDecoder = nullptr;
    QmlAVAudioDecoder *audioDecoder = nullptr;
};

#endif // QMLAVMEDIACONTEXTHOLDER_H
