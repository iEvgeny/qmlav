#include "qmlavvideobuffer.h"
#include "qmlavformat.h"

extern "C" {
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

QmlAVVideoBuffer::QmlAVVideoBuffer(const QmlAVVideoFrame &videoFrame, QAbstractVideoBuffer::HandleType type)
    : QAbstractPlanarVideoBuffer(type)
    , m_videoFrame(videoFrame)
    , m_mapMode(NotMapped)
    , m_swsCtx(nullptr)
{
}

int QmlAVVideoBuffer::map(QAbstractVideoBuffer::MapMode mode, int *numBytes, int bytesPerLine[], uchar *data[])
{
    if (mode == QAbstractVideoBuffer::NotMapped) {
        return 0;
    }

    auto mapData = map(mode);

    int i = 0;
    for (*numBytes = 0; i < QMLAV_NUM_DATA_POINTERS; ++i) {
        *numBytes += mapData.size[i];
        bytesPerLine[i] = mapData.bytesPerLine[i];
        data[i] = mapData.data[i];
    }

    return i;
}

bool QmlAVVideoBuffer::planeSizes(int size[]) const
{
#if (LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(56, 56, 100))
    ptrdiff_t lineSizes[QMLAV_NUM_DATA_POINTERS];
    size_t planeSizes[QMLAV_NUM_DATA_POINTERS];

    for (int i = 0; i < QMLAV_NUM_DATA_POINTERS; ++i) {
        lineSizes[i] = m_videoFrame.avFrame()->linesize[i];
    }

    if (av_image_fill_plane_sizes(planeSizes, m_videoFrame.pixelFormat(), m_videoFrame.avFrame()->height, lineSizes) >= 0) {
        for (int i = 0; i < QMLAV_NUM_DATA_POINTERS; ++i) {
            size[i] = planeSizes[i];
        }

        return true;
    }

#else
    // Calculating plane sizes from plane pointers
    uint8_t *data[QMLAV_NUM_DATA_POINTERS];
    int dataSize = av_image_fill_pointers(data, m_videoFrame.pixelFormat(),
                                          m_videoFrame.avFrame()->height,
                                          0, // Initial value for data[0]
                                          m_videoFrame.avFrame()->linesize);
    if (dataSize > 0) {
        for (int i = 1;; ++i) {
            if (i < QMLAV_NUM_DATA_POINTERS && data[i]) {
                size[i - 1] = data[i] - data[i - 1];
            } else {
                size[i - 1] = reinterpret_cast<uint8_t *>(dataSize) - data[i - 1];
                break;
            }
        }

        return true;
    }
#endif

    return false;
}

AVFramePtr QmlAVVideoBuffer::swsScale(const QmlAVPixelFormat &dstFormat)
{
    AVFramePtr avFrameSws;
    AVPixelFormat srcAVFormat = QmlAVPixelFormat(m_videoFrame.avFrame()->format); // Normalize

    // TODO: Test different "flags" for performance improvement
    m_swsCtx = sws_getCachedContext(m_swsCtx,
                                    m_videoFrame.avFrame()->width, m_videoFrame.avFrame()->height,
                                    srcAVFormat,
                                    m_videoFrame.avFrame()->width, m_videoFrame.avFrame()->height,
                                    dstFormat,
                                    SWS_BILINEAR,
                                    nullptr, nullptr, nullptr);

    if (m_swsCtx) {
        avFrameSws->width = m_videoFrame.avFrame()->width;
        avFrameSws->height = m_videoFrame.avFrame()->height;
        avFrameSws->format = dstFormat;
        av_frame_get_buffer(avFrameSws, FFMPEG_ALIGNMENT);

        sws_scale(m_swsCtx, m_videoFrame.avFrame()->data, m_videoFrame.avFrame()->linesize, 0, m_videoFrame.avFrame()->height, avFrameSws->data, avFrameSws->linesize);
        sws_freeContext(m_swsCtx);
    }

    return avFrameSws;
}

QmlAVVideoBuffer_CPU::QmlAVVideoBuffer_CPU(const QmlAVVideoFrame &videoFrame)
    : QmlAVVideoBuffer(videoFrame, QAbstractVideoBuffer::NoHandle)
{
}

QmlAVVideoBuffer::MapData QmlAVVideoBuffer_CPU::map(QAbstractVideoBuffer::MapMode mode)
{
    MapData mapData;

    if (mode != QAbstractVideoBuffer::NotMapped) {
        auto srcFormat = m_videoFrame.pixelFormat();
        auto dstFormat = pixelFormat();

        if (srcFormat != dstFormat) {
            AVFramePtr avFrameSws = swsScale(dstFormat);
            if (av_frame_copy_props(avFrameSws, m_videoFrame.avFrame()) == 0) {
                m_videoFrame.avFrame() = avFrameSws;
            }
        }

        if (planeSizes(mapData.size)) {
            for (int i = 0; i < QMLAV_NUM_DATA_POINTERS; ++i) {
                mapData.bytesPerLine[i] = m_videoFrame.avFrame()->linesize[i];
                mapData.data[i] = m_videoFrame.avFrame()->data[i];
            }
        }
    }

    setMapMode(mode);

    return mapData;
}

QmlAVPixelFormat QmlAVVideoBuffer_CPU::pixelFormat() const
{
    return m_videoFrame.pixelFormat().nearestQtNative();
}

QmlAVVideoBuffer_GPU::QmlAVVideoBuffer_GPU(const QmlAVVideoFrame &videoFrame, std::shared_ptr<QmlAVHWOutput> hwOutput)
    : QmlAVVideoBuffer_CPU(videoFrame)
    , m_hwOutput(hwOutput)
{
    if (m_hwOutput) {
        m_type = m_hwOutput->handleType();
    }
}

QmlAVVideoBuffer::MapData QmlAVVideoBuffer_GPU::map(QAbstractVideoBuffer::MapMode mode)
{
    int ret;
    AVFramePtr avFrameSw;

    if (mapMode() == QAbstractVideoBuffer::NotMapped) {

        avFrameSw->format = m_videoFrame.swPixelFormat(); // Important!

        ret = av_hwframe_transfer_data(avFrameSw, m_videoFrame.avFrame(), 0);
        if (ret == 0) {
            if (av_frame_copy_props(avFrameSw, m_videoFrame.avFrame()) == 0) {
                m_videoFrame.avFrame() = avFrameSw;
            }
        } else {
            logCritical() << QString("Failed to transfer data to system memory: \"%1\" (%2)").arg(av_err2str(ret)).arg(ret);
            return QmlAVVideoBuffer_CPU::map(QAbstractVideoBuffer::NotMapped);
        }
    }

    return QmlAVVideoBuffer_CPU::map(mode);
}

QVariant QmlAVVideoBuffer_GPU::handle() const
{
    assert(m_hwOutput);
    return m_hwOutput->handle(m_videoFrame.avFrame());
}

QmlAVPixelFormat QmlAVVideoBuffer_GPU::pixelFormat() const
{
    if (m_hwOutput) {
        return m_hwOutput->pixelFormat();
    }

    return m_videoFrame.swPixelFormat().nearestQtNative();
}
