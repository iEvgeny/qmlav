#include "qmlavhwoutput_vaapi_egl.h"

#if defined(__linux__) && !defined(__ANDROID__)
#include "qmlavutils.h"

QmlAVHWOutput_VAAPI_EGL::QmlAVHWOutput_VAAPI_EGL() = default;

QmlAVHWOutput_VAAPI_EGL::~QmlAVHWOutput_VAAPI_EGL() = default;

QVariant QmlAVHWOutput_VAAPI_EGL::handle(const QmlAVVideoFrame & /*videoFrame*/)
{
    logWarning() << "QmlAVHWOutput_VAAPI_EGL::handle() Unimplemented!";
    return {};
}

#endif