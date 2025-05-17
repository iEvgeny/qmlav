#include "qmlavframe.h"
#include "qmlavdecoder.h"
#include "qmlavutils.h"
#include "qmlavvideobuffer.h"

extern "C" {
#include <libavutil/imgutils.h>
}

#define AUDIO_LATENCY_LIMIT 300000 // 300 ms

QmlAVFrame::QmlAVFrame(const AVFramePtr &avFrame, const std::shared_ptr<QmlAVMediaContextHolder> &context, Type type)
    : m_type(type)
    , m_avFrame(avFrame)
    , m_context(context)
{
    assert(m_avFrame && m_context);
}

QmlAVFrame::~QmlAVFrame()
{
    if (m_context) {
        m_context->clock.leftPts = pts();
    }
}

double QmlAVFrame::timeBaseUs() const
{
    return av_q2d(decoder()->stream()->time_base) * AV_TIME_BASE;
}

// PTS of the first frame of the stream in presentation order
int64_t QmlAVFrame::startPts() const
{
    auto startPts = decoder()->stream()->start_time;
    if (startPts != AV_NOPTS_VALUE) {
        return startPts * timeBaseUs();
    }

    return 0;
}

int64_t QmlAVFrame::pts() const
{
    int64_t pts = 0;
    if (m_avFrame->pts != AV_NOPTS_VALUE) {
        pts = m_avFrame->pts;
    } else {
        pts = m_avFrame->best_effort_timestamp;
    }

    return pts * timeBaseUs();
}

QmlAVVideoFrame::QmlAVVideoFrame(const AVFramePtr &avFrame, const std::shared_ptr<QmlAVMediaContextHolder> &context)
    : QmlAVFrame(avFrame, context, TypeVideo)
{
}

bool QmlAVVideoFrame::isValid() const
{
    return context() && (avFrame()->data[0] || avFrame()->data[1] || avFrame()->data[2] || avFrame()->data[3]);
}

AVRational QmlAVVideoFrame::sampleAspectRatio() const
{
    AVRational sar = {1, 1};

    if (isValid()) {
        auto codecpar = decoder()->stream()->codecpar;

        if (avFrame()->sample_aspect_ratio.num) {
            sar = avFrame()->sample_aspect_ratio;
        } else if (codecpar->sample_aspect_ratio.num) {
            sar = codecpar->sample_aspect_ratio;
        }
    }

    return sar;
}

QmlAVPixelFormat QmlAVVideoFrame::swPixelFormat() const
{
    if (isHWDecoded()) {
        AVHWFramesContext *hwFramesCtx = reinterpret_cast<AVHWFramesContext *>(avFrame()->hw_frames_ctx->data);
        return hwFramesCtx->sw_format;
    }

    return pixelFormat();
}

// YUV colormodel/YCbCr colorspace
QmlAVColorSpace QmlAVVideoFrame::colorSpace() const
{
    // NOTE: Color space support in Qt5 is in its infancy
    if (avFrame()->color_range == AVCOL_RANGE_JPEG) {
        // QVideoSurfaceFormat::YCbCr_xvYCC601 and ::YCbCr_xvYCC709 do NOT implement color space with extended value range.
        // This is the same as ::YCbCr_BT601 and ::YCbCr_BT709 respectively.
        // ::YCbCr_JPEG is a modified BT.601 with a full 8-bit range [0...255] and gives expected result in all cases.
        return AVCOL_SPC_RGB;
    }

    return avFrame()->colorspace;
}

QmlAVVideoFrame::operator QVideoFrame() const
{
    QmlAVVideoBuffer *buffer;

    if (isValid())  {
        if (isHWDecoded()) {
            buffer = new QmlAVVideoBuffer_GPU(*this, decoder<QmlAVVideoDecoder>()->hwOutput());
        } else {
            buffer = new QmlAVVideoBuffer_CPU(*this);
        }

        return QVideoFrame(buffer, size(), buffer->pixelFormat());
    } else {
        return QVideoFrame();
    }
}

QmlAVAudioFrame::QmlAVAudioFrame(const AVFramePtr &avFrame, const std::shared_ptr<QmlAVMediaContextHolder> &context)
    : QmlAVFrame(avFrame, context, TypeAudio)
    , m_buffer(nullptr)
    , m_dataBegin(0)
    , m_dataSize(0)
{
    if (isValid()) {
        double compensationFactor = 1.0;

        int64_t leftPts = context->clock.leftPts;
        if (leftPts) {
            auto delta = pts() - leftPts;
            if (delta > AUDIO_LATENCY_LIMIT) {
                compensationFactor = 0.98; // -2%
            }
        }

        auto &resampler = QmlAVFrame::decoder<QmlAVAudioDecoder>()->resampler();
        m_dataSize =  resampler.convert(&m_buffer, *this, compensationFactor);
    }
}

QmlAVAudioFrame::~QmlAVAudioFrame()
{
    av_freep(&m_buffer);
}

bool QmlAVAudioFrame::isValid() const
{
    return context() && avFrame()->extended_data;
}

size_t QmlAVAudioFrame::readData(uint8_t *data, size_t maxSize)
{
    auto size = std::min(dataSize(), maxSize);
    memcpy(data, m_buffer + m_dataBegin, size);
    m_dataBegin += size;
    return size;
}

AVSampleFormat QmlAVAudioFrame::sampleFormat() const
{
    return av_get_packed_sample_fmt(static_cast<AVSampleFormat>(avFrame()->format));
}

int QmlAVAudioFrame::channelCount() const
{
    return
#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(57, 24, 100)
    avFrame()->channels;
#else
    avFrame()->ch_layout.nb_channels;
#endif
}

QAudioFormat QmlAVAudioFrame::audioFormat() const
{
    QAudioFormat format;

    if (isValid()) {
        AVSampleFormat outSampleFormat = sampleFormat();

        format.setSampleRate(sampleRate());
        format.setChannelCount(channelCount());
        format.setCodec("audio/pcm");
        format.setByteOrder(AV_NE(QAudioFormat::BigEndian, QAudioFormat::LittleEndian));
        format.setSampleType(QmlAVSampleFormat::audioFormatFromAVFormat(outSampleFormat));
        format.setSampleSize(av_get_bytes_per_sample(outSampleFormat) * 8);
    }

    return  format;
}
