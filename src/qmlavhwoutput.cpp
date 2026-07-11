#include "qmlavhwoutput.h"

#if defined(__linux__) && !defined(__ANDROID__)
#include <va/va_x11.h>

extern "C" {
    #include <libavutil/hwcontext_vaapi.h>
}

QmlAVHWOutput_VAAPI_GLX::QmlAVHWOutput_VAAPI_GLX()
    : m_glxDisplay(nullptr)
    , m_glTexture(0)
    , m_x11Pixmap(0)
    , m_glXPixmap(0)
{
    m_glXBindTexImageEXT = reinterpret_cast<PFNGLXBINDTEXIMAGEEXTPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXBindTexImageEXT")));
    m_glXReleaseTexImageEXT = reinterpret_cast<PFNGLXRELEASETEXIMAGEEXTPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXReleaseTexImageEXT")));
}

QmlAVHWOutput_VAAPI_GLX::~QmlAVHWOutput_VAAPI_GLX()
{
    if (m_glxDisplay) {
        if (m_glXPixmap) {
            m_glXReleaseTexImageEXT(m_glxDisplay, m_glXPixmap, GLX_FRONT_EXT);
        }

        if (m_glXPixmap) glXDestroyPixmap(m_glxDisplay, m_glXPixmap);
        if (m_x11Pixmap) XFreePixmap(m_glxDisplay, m_x11Pixmap);
    }

    glDeleteTextures(1, &m_glTexture);
}

// TODO: Implement video resolution changing and get display depth via glXGetFBConfigAttrib()
QVariant QmlAVHWOutput_VAAPI_GLX::handle(const AVFramePtr &avFrame)
{
    if (!m_glXBindTexImageEXT || !m_glXReleaseTexImageEXT) {
        logWarning() << "Failed to get GLX proc addresses.";
        return {};
    }
    if (avFrame->format != AV_PIX_FMT_VAAPI) {
        logWarning() << QString("Wrong pixel format: ") << QmlAVPixelFormat(avFrame->format);
        return {};
    }

    // Get VA-API context
    auto avHWFramesCtx = reinterpret_cast<AVHWFramesContext *>(avFrame->hw_frames_ctx->data);
    auto vaDeviceCtx = static_cast<AVVAAPIDeviceContext *>(avHWFramesCtx->device_ctx->hwctx);
    VADisplay vaDisplay = vaDeviceCtx->display;
    VASurfaceID vaSurface = reinterpret_cast<uintptr_t>(avFrame->data[3]);

    // Lazy initialization
    if (!m_glxDisplay && !initializeGLX(avFrame->width, avFrame->height)) {
        return {};
    }

    vaSyncSurface(vaDisplay, vaSurface);

    uint status = vaPutSurface(vaDisplay, vaSurface, m_x11Pixmap,
                               0, 0, avFrame->width, avFrame->height,
                               0, 0, avFrame->width, avFrame->height,
                               nullptr, 0, VA_FRAME_PICTURE | VA_SRC_BT601);
    if (status != VA_STATUS_SUCCESS) {
        logWarning() << "vaPutSurface() failed: 0x" << QmlAV::Hex << status;
        return {};
    }

    XSync(m_glxDisplay, False);

    glBindTexture(GL_TEXTURE_2D, m_glTexture);

    m_glXReleaseTexImageEXT(m_glxDisplay, m_glXPixmap, GLX_FRONT_EXT);
    m_glXBindTexImageEXT(m_glxDisplay, m_glXPixmap, GLX_FRONT_EXT, nullptr);

    glBindTexture(GL_TEXTURE_2D, 0);

    return m_glTexture;
}

bool QmlAVHWOutput_VAAPI_GLX::initializeGLX(int width, int height)
{
    Display *glxDisplay = glXGetCurrentDisplay();
    if (!glxDisplay) {
        logWarning() << "No active GLX display found. Are you running under native Wayland?";
        return false;
    }

    int x11Screen = DefaultScreen(glxDisplay);

    if (!QString(glXQueryExtensionsString(glxDisplay, x11Screen)).contains("GLX_EXT_texture_from_pixmap")) {
        logWarning() << "GLX_EXT_texture_from_pixmap is not supported by this display!";
        return false;
    }

    const int fbConfigAttribs[] = {
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT | GLX_PIXMAP_BIT,
        GLX_BIND_TO_TEXTURE_RGBA_EXT, True,
        GLX_BIND_TO_TEXTURE_TARGETS_EXT, GLX_TEXTURE_2D_BIT_EXT,
        GLX_DOUBLEBUFFER, False,
        None
    };

    int nItems;
    std::unique_ptr<GLXFBConfig, decltype(&XFree)> fbConfigs(glXChooseFBConfig(glxDisplay, x11Screen, fbConfigAttribs, &nItems), XFree);
    if (!fbConfigs) {
        logWarning() << "Failed to choose GLX FBConfig.";
        return false;
    }

    std::unique_ptr<XVisualInfo, decltype(&XFree)> vis(glXGetVisualFromFBConfig(glxDisplay, fbConfigs.get()[0]), XFree);
    if (!vis) {
        logWarning() << "Failed to get visual from GLX FBConfig.";
        return false;
    }

    int depth = vis->depth;

    m_x11Pixmap = XCreatePixmap(glxDisplay, DefaultRootWindow(glxDisplay), width, height, depth);
    if (!m_x11Pixmap) {
        logWarning() << "Failed to create X11 Pixmap.";
        return false;
    }

    const int pixmapAttribs[] = {
        GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
        GLX_TEXTURE_FORMAT_EXT, depth == 32 ? GLX_TEXTURE_FORMAT_RGBA_EXT : GLX_TEXTURE_FORMAT_RGB_EXT,
        GLX_MIPMAP_TEXTURE_EXT, False,
        None,
    };

    m_glXPixmap = glXCreatePixmap(glxDisplay, fbConfigs.get()[0], m_x11Pixmap, pixmapAttribs);
    if (!m_glXPixmap) {
        logWarning() << "Failed to create GLX Pixmap.";
        XFreePixmap(glxDisplay, m_x11Pixmap);
        m_x11Pixmap = 0;
        return false;
    }

    glGenTextures(1, &m_glTexture);

    m_glxDisplay = glxDisplay;

    return true;
}

#endif