#ifndef QMLAVHWOUTPUT_VAAPI_EGL_H
#define QMLAVHWOUTPUT_VAAPI_EGL_H

#include "qmlavhwoutput.h"

#if defined(__linux__) && !defined(__ANDROID__)

#include <GLES2/gl2.h> // GLuint, GLenum (buildProgram signature)
#include <memory>

class QOpenGLExtraFunctions;

// Zero-copy VAAPI → DMA-BUF → EGLImage → GL.
// Qt5 VideoOutput/GLTextureHandle can only sample an RGB TEXTURE_2D, so NV12 is
// converted on the GPU into a persistent FBO texture (no CPU readback).
// Qt6 RHI can consume the imported planes directly and drop the blit.

class QmlAVHWOutput_VAAPI_EGL final : public QmlAVHWOutput
{
public:
    QmlAVHWOutput_VAAPI_EGL();
    ~QmlAVHWOutput_VAAPI_EGL() override;

    Type type() const override { return TypeVAAPI_EGL; }
    QmlAVPixelFormat pixelFormat() const override { return AV_PIX_FMT_BGR32; }
    QAbstractVideoBuffer::HandleType handleType() const override { return QAbstractVideoBuffer::GLTextureHandle; }
    QVariant handle(const QmlAVVideoFrame &videoFrame) override;

private:
    struct Priv;
    std::unique_ptr<Priv> m_egl;

    bool initializeEGL(int width, int height);
    GLuint buildProgram(bool core, bool gles, int planeCount, int bitDepth, bool chromaSwap);
    void cleanupEGL();
    void setTextureParams();
    void setupAttribs();
    void bindQuad(QOpenGLExtraFunctions *extra);
    void unbindQuad(QOpenGLExtraFunctions *extra);
    void initQuad(QOpenGLExtraFunctions *extra);
};

#endif // __linux__

#endif // QMLAVHWOUTPUT_VAAPI_EGL_H
