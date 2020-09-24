#include "qmlavframe.h"
#include "qmlavdecoder.h"

QmlAVFrame::QmlAVFrame(qint64 startTime, QmlAVFrame::Type type)
    : m_type(type),
      m_startTime(startTime)
{
}

QmlAVVideoFrame::QmlAVVideoFrame(qint64 startTime)
    : QmlAVFrame(startTime, QmlAVFrame::TypeVideo),
      m_pixelFormat(QVideoFrame::Format_Invalid)
{
}

bool QmlAVVideoFrame::isValid() const
{
    if (m_type == QmlAVFrame::TypeVideo) {
        return m_videoFrame.isValid();
    }

    return false;
}

#define FFMPEG_ALIGNMENT (64)

void QmlAVVideoFrame::fromAVFrame(AVFrame *avFrame)
{
    SwsContext *swsCtx = nullptr;

    if (!avFrame) {
        return;
    }

    AVPixelFormat srcAVFormat = QmlAVVideoFormat::normalizeFFmpegPixelFormat(static_cast<AVPixelFormat>(avFrame->format));
    AVPixelFormat dstAVFormat = QmlAVVideoFormat::ffmpegFormatFromPixelFormat(m_pixelFormat);

    swsCtx = sws_getContext(avFrame->width, avFrame->height,
                            srcAVFormat,
                            avFrame->width, avFrame->height,
                            dstAVFormat,
                            SWS_POINT,
                            nullptr, nullptr, nullptr);

    int size = av_image_get_buffer_size(dstAVFormat, avFrame->width, avFrame->height, FFMPEG_ALIGNMENT);
    m_videoFrame = QVideoFrame(size,
                               QSize(avFrame->width, avFrame->height),
                               avFrame->linesize[0],
                               m_pixelFormat);
    m_videoFrame.setStartTime(m_startTime);

    if (m_videoFrame.map(QAbstractVideoBuffer::WriteOnly)) {
        if (swsCtx) {
            uint8_t *data[AV_NUM_DATA_POINTERS] = {};
            int linesize[AV_NUM_DATA_POINTERS] = {};

            int i = 0;
            while (m_videoFrame.bits(i)) {
                data[i] = m_videoFrame.bits(i);
                linesize[i] = m_videoFrame.bytesPerLine(i);
                ++i;
            }

            QmlAVUtils::log(QString("QmlAVVideoFrame @ %1").arg(QString().number(reinterpret_cast<quintptr>(this), 16)),
                            QmlAVUtils::LogDebug,
                            QString("fromAVFrame(AVFrame: width=%1; height=%2; format=%3; linesize[0-2]=%4:%5:%6) : { size=%7; m_pixelFormat=%8; linesize[0-2]=%9;%10;%11 }")
                            .arg(avFrame->width).arg(avFrame->height).arg(avFrame->format).arg(avFrame->linesize[0]).arg(avFrame->linesize[1]).arg(avFrame->linesize[2])
                            .arg(size).arg(m_pixelFormat).arg(linesize[0]).arg(linesize[1]).arg(linesize[2]));

            sws_scale(swsCtx, avFrame->data, avFrame->linesize, 0, avFrame->height, data, linesize);
            sws_freeContext(swsCtx);
        }
        m_videoFrame.unmap();
    }
}

QmlAVAudioFrame::QmlAVAudioFrame(qint64 startTime)
    : QmlAVFrame(startTime, QmlAVFrame::TypeAudio),
      m_data(nullptr),
      m_dataSize(0)
{
}

QmlAVAudioFrame::~QmlAVAudioFrame()
{
    delete m_data;
}

bool QmlAVAudioFrame::isValid() const
{
    if (m_data && m_dataSize) {
        return true;
    }

    return false;
}

void QmlAVAudioFrame::fromAVFrame(AVFrame *avFrame)
{
    SwrContext *swrCtx = nullptr;

    if (!avFrame || m_data) {
        return;
    }

    int64_t channelLayout = avFrame->channel_layout != 0 ? avFrame->channel_layout : av_get_default_channel_layout(avFrame->channels);
    AVSampleFormat outSampleFormat = av_get_packed_sample_fmt(static_cast<AVSampleFormat>(avFrame->format));

    swrCtx = swr_alloc_set_opts(nullptr,
                                channelLayout,
                                outSampleFormat,
                                avFrame->sample_rate,
                                channelLayout,
                                static_cast<AVSampleFormat>(avFrame->format),
                                avFrame->sample_rate,
                                0, nullptr);

    if (swr_init(swrCtx) == 0) {
        m_dataSize = av_samples_get_buffer_size(nullptr, avFrame->channels, avFrame->nb_samples, outSampleFormat, 0);
//        m_dataSize = avFrame->channels * avFrame->nb_samples * av_get_bytes_per_sample(outSampleFormat);
        m_data = new uint8_t[m_dataSize];

        swr_convert(swrCtx,
                    &m_data,
                    avFrame->nb_samples,
                    const_cast<const uint8_t**>(avFrame->data),
                    avFrame->nb_samples);
    }

    if (swrCtx) {
        swr_free(&swrCtx);
    }
}
