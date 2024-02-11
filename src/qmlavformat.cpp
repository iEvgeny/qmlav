#include "qmlavformat.h"

#include <unordered_map>

extern "C" {
#include <libavutil/pixdesc.h>
}

// AV_PIX_FMT_RGB32/Format_ARGB32 family formats are 32-bit formats, they are the same regardless of endian when read sequentially as 32-bit unsigned integers.
// When reading formats such as a sequence of 8 bit numbers, endian must be considered.
// See <libavutil/pixfmt.h> for details.
const std::vector<QmlAVPixelFormat::PixelFormatMap> QmlAVPixelFormat::m_pixelFormatMap = {
    {AV_PIX_FMT_NONE, QVideoFrame::Format_Invalid},
    {AV_PIX_FMT_RGB32, QVideoFrame::Format_ARGB32, true},
//    {, QVideoFrame::Format_ARGB32_Premultiplied},
    {AV_PIX_FMT_0RGB32, QVideoFrame::Format_RGB32, true},
    {AV_PIX_FMT_RGB24, QVideoFrame::Format_RGB24},
    {AV_PIX_FMT_RGB565, QVideoFrame::Format_RGB565, true},
    {AV_PIX_FMT_RGB555, QVideoFrame::Format_RGB555},
//    {, QVideoFrame::Format_ARGB8565_Premultiplied},
    {AV_PIX_FMT_BGR32, QVideoFrame::Format_BGRA32, true}, // NOTE: According to the Qt documentation QVideoFrame::Format_BGRA32 should be interpreted as 0xBBGGRRAA in BE,
//    {, QVideoFrame::Format_BGRA32_Premultiplied},       // but seems to be interpreted as 0xAABBGGRR. See https://lists.qt-project.org/pipermail/interest/2019-November/034134.html
    {AV_PIX_FMT_0BGR32, QVideoFrame::Format_BGR32, true}, // In the first case there may be variations, but according to the Qt sources for QVideoFrame::Format_BGR32 there are no ambiguities.
    {AV_PIX_FMT_BGR24, QVideoFrame::Format_BGR24},
    {AV_PIX_FMT_BGR565, QVideoFrame::Format_BGR565},
    {AV_PIX_FMT_BGR555, QVideoFrame::Format_BGR555},
//    {, QVideoFrame::Format_BGRA5658_Premultiplied},
//    {, QVideoFrame::Format_AYUV444},
//    {, QVideoFrame::Format_AYUV444_Premultiplied},
//    {, QVideoFrame::Format_YUV444},
    {AV_PIX_FMT_YUV420P, QVideoFrame::Format_YUV420P, true},
//    {, QVideoFrame::Format_YV12, true},
    {AV_PIX_FMT_UYVY422, QVideoFrame::Format_UYVY, true},
    {AV_PIX_FMT_YUYV422, QVideoFrame::Format_YUYV, true},
    {AV_PIX_FMT_NV12, QVideoFrame::Format_NV12, true},
    {AV_PIX_FMT_NV21, QVideoFrame::Format_NV21, true},
//    {, QVideoFrame::Format_IMC1},
//    {, QVideoFrame::Format_IMC2},
//    {, QVideoFrame::Format_IMC3},
//    {, QVideoFrame::Format_IMC4},
    {AV_PIX_FMT_GRAY8, QVideoFrame::Format_Y8},
    {AV_PIX_FMT_GRAY16LE, QVideoFrame::Format_Y16},
//    {, QVideoFrame::Format_Jpeg},
//    {, QVideoFrame::Format_CameraRaw},
//    {, QVideoFrame::Format_AdobeDng},
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    {AV_PIX_FMT_BGR32, QVideoFrame::Format_ABGR32},
    {AV_PIX_FMT_YUV422P, QVideoFrame::Format_YUV422P, true}
#endif
};

QmlAVPixelFormat::QmlAVPixelFormat(AVPixelFormat avPixelFormat)
    : m_avPixelFormat(normalize(avPixelFormat))
{
}

QmlAVPixelFormat::QmlAVPixelFormat(int avPixelFormat)
    : QmlAVPixelFormat(static_cast<AVPixelFormat>(avPixelFormat))
{
}

QmlAVPixelFormat::QmlAVPixelFormat(QVideoFrame::PixelFormat pixelFormat)
    : m_avPixelFormat(avFormatFromPixelFormat(pixelFormat))
{
}

bool QmlAVPixelFormat::isHWAccel() const
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(m_avPixelFormat);
    return desc->flags & AV_PIX_FMT_FLAG_HWACCEL;
}

bool QmlAVPixelFormat::isQtNative() const
{
    for (const auto &pfm : m_pixelFormatMap) {
        if (pfm.avFormat == m_avPixelFormat) {
            return pfm.qtNative;
        }
    }

    return false;
}

QmlAVPixelFormat QmlAVPixelFormat::nearestQtNative() const
{
    if (!isQtNative()) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(m_avPixelFormat);
        if (desc->flags & AV_PIX_FMT_FLAG_RGB) {
            return AV_PIX_FMT_RGB32;
        }

        return AV_PIX_FMT_YUV420P;
    }

    return *this;
}

// We lose information about the deprecated format, but the new format combined with
// the AVFrame "color_range" field is a convenient replacement that does not raise FFmpeg warnings.
AVPixelFormat QmlAVPixelFormat::normalize(AVPixelFormat avPixelFormat) const
{
    static std::unordered_map<AVPixelFormat, AVPixelFormat> map = {
        {AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUV420P},
        {AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUV422P},
        {AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUV444P},
        {AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUV440P},
        {AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUV411P}
    };

    // Replace deprecated AV_PIX_FMT_YUVJXXXP formats
    if (auto pair = map.find(avPixelFormat); pair != map.end()) {
        return pair->second;
    }

    return avPixelFormat;
}

QVideoFrame::PixelFormat QmlAVPixelFormat::pixelFormatFromAVFormat(AVPixelFormat avPixelFormat) const
{
    for (const auto &pfm : m_pixelFormatMap) {
        if (pfm.avFormat == avPixelFormat) {
            return pfm.format;
        }
    }

    return QVideoFrame::Format_Invalid;
}

AVPixelFormat QmlAVPixelFormat::avFormatFromPixelFormat(QVideoFrame::PixelFormat pixelFormat) const
{
    for (const auto &pfm : m_pixelFormatMap) {
        if (pfm.format == pixelFormat) {
            return pfm.avFormat;
        }
    }

    return AV_PIX_FMT_NONE;
}

QmlAVColorSpace::QmlAVColorSpace(AVColorSpace avColorSpace)
    : m_avColorSpace(avColorSpace)
{
}

QmlAVColorSpace::operator QVideoSurfaceFormat::YCbCrColorSpace() const
{
    switch (m_avColorSpace) {
    case AVCOL_SPC_RGB:
        // Modified BT.601 with full 8-bit range of [0...255]
        // The Qt sources contain the corresponding RGB<->YUV conversion matrices
        return QVideoSurfaceFormat::YCbCr_JPEG;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_SMPTE240M:
        // NOTE: QVideoSurfaceFormat::YCbCr_BT601 and ::YCbCr_xvYCC601 are the same
        return QVideoSurfaceFormat::YCbCr_BT601;
    // BUG: #65 (Pink or Reddish tint)
    // It looks like the BT709 family color spaces are not being handled correctly.
//    case AVCOL_SPC_BT709:
//        // NOTE: QVideoSurfaceFormat::YCbCr_BT709 and ::YCbCr_xvYCC709 are the same
//        return QVideoSurfaceFormat::YCbCr_BT709;
    default:
        return QVideoSurfaceFormat::YCbCr_Undefined;
    }
}

QAudioFormat::SampleType QmlAVSampleFormat::audioFormatFromAVFormat(AVSampleFormat sampleFormat)
{
    QMap<AVSampleFormat, QAudioFormat::SampleType> sampleFormatMap {
        {AV_SAMPLE_FMT_U8, QAudioFormat::UnSignedInt},              // unsigned 8 bits
        {AV_SAMPLE_FMT_S16, QAudioFormat::QAudioFormat::SignedInt}, // signed 16 bits
        {AV_SAMPLE_FMT_S32, QAudioFormat::QAudioFormat::SignedInt}, // signed 32 bits
        {AV_SAMPLE_FMT_FLT, QAudioFormat::QAudioFormat::Float},     // float
        {AV_SAMPLE_FMT_DBL, QAudioFormat::QAudioFormat::Float},     // double

        {AV_SAMPLE_FMT_U8P, QAudioFormat::UnSignedInt},             // unsigned 8 bits, planar
        {AV_SAMPLE_FMT_S16P, QAudioFormat::SignedInt},              // signed 16 bits, planar
        {AV_SAMPLE_FMT_S32P, QAudioFormat::SignedInt},              // signed 32 bits, planar
        {AV_SAMPLE_FMT_FLTP, QAudioFormat::Float},                  // float, planar
        {AV_SAMPLE_FMT_DBLP, QAudioFormat::Float}/*,                // double, planar
        {AV_SAMPLE_FMT_S64, QAudioFormat::, },                      // signed 64 bits
        {AV_SAMPLE_FMT_S64P, QAudioFormat::, }                      // signed 64 bits, planar*/
    };

    return sampleFormatMap.value(sampleFormat, QAudioFormat::Unknown);
}

#ifndef QT_NO_DEBUG_STREAM
QDebug operator<<(QDebug dbg, const QmlAVPixelFormat &pixelFormat)
{
    dbg.nospace() << QString("AV_PIX_FMT_%1").arg(av_get_pix_fmt_name(pixelFormat)).toUpper();
    return dbg.nospace() << " (QVideoFrame::" << static_cast<QVideoFrame::PixelFormat>(pixelFormat) << ")";
}

QDebug operator<<(QDebug dbg, const QmlAVColorSpace &colorSpace)
{
    dbg.nospace() << QString("AVCOL_SPC_%1").arg(av_color_space_name(colorSpace)).toUpper();
    return dbg.nospace() << " (QVideoSurfaceFormat::" << static_cast<QVideoSurfaceFormat::YCbCrColorSpace>(colorSpace) << ")";
}
#endif
