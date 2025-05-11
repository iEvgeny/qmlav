#ifndef QMLAVRESAMPLER_H
#define QMLAVRESAMPLER_H

extern "C" {
#include <libswresample/swresample.h>
}

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
