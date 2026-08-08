#ifndef QMLAVHWOUTPUT_VAAPI_EGL_H
#define QMLAVHWOUTPUT_VAAPI_EGL_H

#include "qmlavhwoutput.h"

#if defined(__linux__) && !defined(__ANDROID__)

class QmlAVHWOutput_VAAPI_EGL final : public QmlAVHWOutput
{
public:
    QmlAVHWOutput_VAAPI_EGL();
    ~QmlAVHWOutput_VAAPI_EGL() override;

    Type type() const override { return TypeVAAPI_EGL; }
    QmlAVPixelFormat pixelFormat() const override { return AV_PIX_FMT_BGR32; }
    QAbstractVideoBuffer::HandleType handleType() const override { return QAbstractVideoBuffer::GLTextureHandle; }
    QVariant handle(const QmlAVVideoFrame &videoFrame) override;
};

#endif // __linux__

#endif // QMLAVHWOUTPUT_VAAPI_EGL_H