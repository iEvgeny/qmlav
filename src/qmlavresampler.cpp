#include "qmlavresampler.h"
#include "qmlavframe.h"

QmlAVResampler::QmlAVResampler()
    : m_swrCtx(nullptr)
    , m_channelLayout{}
    , m_inSampleFormat(AV_SAMPLE_FMT_NONE)
    , m_inSampleRate(0)
{
}

QmlAVResampler::~QmlAVResampler()
{
    swr_free(&m_swrCtx);
}

/*
 * For  "compensationFactor":
 *   0.9 - reduce by 10%
 *   1.0 - nothing to do
 *   1.1 - increase by 10%
 *
 * Compensation will remain in effect after setting "compensationFactor" to 1.0 during:
 *   previous "compensationFactor" * 1 sec.
 */
size_t QmlAVResampler::convert(uint8_t **dstBuffer, const QmlAVAudioFrame &srcFrame, double compensationFactor)
{
    int channels = srcFrame.channelCount();
    AVSampleFormat outSampleFormat = srcFrame.sampleFormat();

    if (initCachedContext(srcFrame)) {
        int srcDistance = srcFrame.sampleRate() * 1; // 1 sec.
        int tgtDistance = srcDistance * compensationFactor;

        int delta = tgtDistance - srcDistance;
        if (delta != 0) {
            /*
             * sample_delta - delta between the number of input and output samples
             * compensation_distance - distance for output samples
             *
             * Example:
             * Reduce the input buffer by 1000 samples, distributing the compensation over 10000 output samples
             *   swr_set_compensation(m_swrCtx, -1000, 10000);
             */
            if (swr_set_compensation(m_swrCtx, delta, tgtDistance) < 0) {
                logWarning() << "swr_set_compensation() failed";
            }
        }

        // NOTE: The Swr context must be set with swr_set_compensation() before calling this method,
        // especially when compensating with increase.
        int outSamples = swr_get_out_samples(m_swrCtx, srcFrame.avFrame()->nb_samples);
        av_samples_alloc(dstBuffer, NULL, channels, outSamples, outSampleFormat, 0);

        auto samples = swr_convert(m_swrCtx,
                                   dstBuffer,
                                   outSamples,
                                   const_cast<const uint8_t**>(srcFrame.avFrame()->extended_data),
                                   srcFrame.avFrame()->nb_samples);
        if (samples > 0) {
            return samples * channels * av_get_bytes_per_sample(outSampleFormat);
        }
    }

    return 0;
}

bool QmlAVResampler::initCachedContext(const QmlAVAudioFrame &srcFrame)
{
    AVChannelLayout channelLayout =
#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(57, 24, 100)
        srcFrame.avFrame()->channel_layout != 0
            ? srcFrame.avFrame()->channel_layout
            : av_get_default_channel_layout(srcFrame.avFrame()->channels);
#else
        srcFrame.avFrame()->ch_layout;
#endif

    AVSampleFormat inSampleFormat = static_cast<AVSampleFormat>(srcFrame.avFrame()->format);
    AVSampleFormat outSampleFormat = srcFrame.sampleFormat();
    int inSampleRate = srcFrame.avFrame()->sample_rate;
    int outSampleRate = srcFrame.sampleRate();

    if (!m_swrCtx ||
        av_channel_layout_compare(&m_channelLayout, &channelLayout) ||
        m_inSampleFormat != inSampleFormat ||
        m_inSampleRate != inSampleRate) {
        swr_free(&m_swrCtx);
#if LIBSWRESAMPLE_VERSION_INT < AV_VERSION_INT(4, 5, 100)
        m_swrCtx = swr_alloc_set_opts(nullptr,
                                      channelLayout,
                                      outSampleFormat,
                                      outSampleRate,
                                      channelLayout,
                                      inSampleFormat,
                                      inSampleRate,
                                      0, nullptr);
#else
        swr_alloc_set_opts2(&m_swrCtx,
                            &channelLayout,
                            outSampleFormat,
                            outSampleRate,
                            &channelLayout,
                            inSampleFormat,
                            inSampleRate,
                            0, nullptr);
#endif
        if (!m_swrCtx) {
            logWarning() << QString("Unable allocate SwrContext context");
            return false;
        }

        int ret = swr_init(m_swrCtx);
        if (ret < 0) {
            logWarning() << QString("Unable initialize SwrContext context: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret);
            return false;
        }

        if (av_channel_layout_copy(&m_channelLayout, &channelLayout) < 0) {
            return false;
        }
        m_inSampleFormat = inSampleFormat;
        m_inSampleRate = inSampleRate;
    }

    return true;
}
