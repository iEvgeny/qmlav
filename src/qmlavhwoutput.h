#ifndef QMLAVHWOUTPUT_H
#define QMLAVHWOUTPUT_H

#include "qmlavframe.h"
#include "qmlavutils.h"
#include "qmlavformat.h"

class QmlAVHWOutput
{
public:
    enum Type
    {
        TypeUnknown,
        TypeVAAPI_GLX
    };

    struct Contract
    {
        int width = 0;
        int height = 0;
        AVPixelFormat swFormat = AV_PIX_FMT_NONE;

        Contract() = default;
        Contract(const QmlAVVideoFrame &videoFrame) {
            width = videoFrame.width();
            height = videoFrame.height();
            swFormat = videoFrame.swPixelFormat();
        }

        bool operator==(const Contract &other) const {
            return width == other.width &&
                   height == other.height &&
                   swFormat == other.swFormat;
        }

        bool operator!=(const Contract &other) const {
            return !(*this == other);
        }
    };

    QmlAVHWOutput() { }
    virtual ~QmlAVHWOutput() { }

    QmlAVHWOutput(const QmlAVHWOutput &other) = delete;
    QmlAVHWOutput &operator=(const QmlAVHWOutput &other) = delete;

    virtual Type type() const = 0;
    virtual QmlAVPixelFormat pixelFormat() const = 0;
    virtual QAbstractVideoBuffer::HandleType handleType() const = 0;
    virtual QVariant handle(const QmlAVVideoFrame &videoFrame) = 0;

protected:
    void resetContract() { m_contract = Contract{}; }

    Contract m_contract;
};

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
};
#endif

#endif // QMLAVHWOUTPUT_H
