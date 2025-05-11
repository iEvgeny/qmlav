#ifndef QMLAVRESAMPLER_H
#define QMLAVRESAMPLER_H

extern "C" {
#include <libswresample/swresample.h>
}

#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(57, 24, 100)
using AVChannelLayout = uint64_t;
inline int av_channel_layout_compare(const AVChannelLayout *chl, const AVChannelLayout *chl1) {
    if (*chl == *chl1) {
        return 0;
    }

    return 1;
}
inline int av_channel_layout_copy(AVChannelLayout *dst, const AVChannelLayout *src) {
    *dst = *src;
    return 0;
}
#endif

struct SwrContext;

class QmlAVAudioFrame;

class QmlAVResampler
{
public:
    QmlAVResampler();
    virtual ~QmlAVResampler();

    QmlAVResampler(const QmlAVResampler &other) = delete;
    QmlAVResampler &operator=(const QmlAVResampler &other) = delete;

    size_t convert(uint8_t **dstBuffer, const QmlAVAudioFrame &srcFrame, double compensationFactor = 1.0);

protected:
    bool initCachedContext(const QmlAVAudioFrame &srcFrame);

private:
    SwrContext *m_swrCtx;

    AVChannelLayout m_channelLayout;
    AVSampleFormat m_inSampleFormat;
    int m_inSampleRate;
};

#endif // QMLAVRESAMPLER_H
