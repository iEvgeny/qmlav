#ifndef QMLAVHWOUTPUT_VAAPI_GLX_H
#define QMLAVHWOUTPUT_VAAPI_GLX_H

#include "qmlavhwoutput.h"

#if defined(__linux__) && !defined(__ANDROID__)
#include <QEvent> // Must be included first due to conflict with X11/X.h
#include <GL/glx.h>

class QmlAVHWOutput_VAAPI_GLX final : public QmlAVHWOutput
{
public:
    QmlAVHWOutput_VAAPI_GLX();
    ~QmlAVHWOutput_VAAPI_GLX() override;

    Type type() const override { return TypeVAAPI_GLX; }
    QmlAVPixelFormat pixelFormat() const override { return AV_PIX_FMT_BGR32; }
    QAbstractVideoBuffer::HandleType handleType() const override { return QAbstractVideoBuffer::GLTextureHandle; }
    QVariant handle(const QmlAVVideoFrame &videoFrame) override;

private:
    Display *m_glxDisplay;
    GLuint m_glTexture;    // Resulting GL texture
    Pixmap m_x11Pixmap;    // Target X11 pixmap for vaPutSurface()
    GLXPixmap m_glXPixmap; // Associated GLX pixmap for glXBindTexImageEXT()

    PFNGLXBINDTEXIMAGEEXTPROC m_glXBindTexImageEXT;
    PFNGLXRELEASETEXIMAGEEXTPROC m_glXReleaseTexImageEXT;

    void cleanupGLX();
    bool initializeGLX(int width, int height);

    uint32_t getVAAPIColorFlags(const AVFramePtr &avFrame) const;
};
#endif // __linux__

#endif // QMLAVHWOUTPUT_VAAPI_GLX_H
