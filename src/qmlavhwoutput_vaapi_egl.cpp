#include "qmlavhwoutput_vaapi_egl.h"

#if defined(__linux__) && !defined(__ANDROID__)
#include "qmlavutils.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <unistd.h>

#include <QOpenGLContext>
// Raw GL entry points are used only for the GL 1.0/1.1 core
// (glGetIntegerv, glBindTexture, glViewport, ...), which <GL/gl.h>
// declares on every platform — same as the GLX output does. Everything
// above GL 1.1 (buffers, FBOs, VAOs, shaders, attrib arrays) lives in
// <GL/glext.h>, which is not reliably available with prototypes, so it
// stays on QOpenGLExtraFunctions (VAO).
#include <GLES2/gl2.h>
// GL_RGBA8 is a desktop-GL token not present in GLES2 headers; the desktop
// internal format is selected at runtime, so a fallback is enough for the
// build (the GLES path never uses it).
#ifndef GL_RGBA8
#define GL_RGBA8 GL_RGBA
#endif
#include <QOpenGLExtraFunctions>

// Prevent eglplatform.h from pulling X11 macros (None/Status) into this TU.
#ifndef EGL_NO_X11
#define EGL_NO_X11
#endif
#ifndef MESA_EGL_NO_X11_HEADERS
#define MESA_EGL_NO_X11_HEADERS
#endif

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <va/va_drmcommon.h>

extern "C" {
    #include <libavutil/common.h> // MKTAG (DRM fourcc encoding)
    #include <libavutil/hwcontext_vaapi.h>
}

namespace {

constexpr uint32_t kDrmFormatR8   = MKTAG('R', '8', ' ', ' ');
constexpr uint32_t kDrmFormatGR88 = MKTAG('G', 'R', '8', '8');
constexpr uint32_t kDrmFormatNV12 = MKTAG('N', 'V', '1', '2');
constexpr uint64_t kDrmModInvalid = 0x00ffffffffffffffULL;

// Older Mesa versions shipped eglext.h without the modifier constants.
#ifndef EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT
#define EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT 0x3443
#define EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT 0x3444
#endif

// Some GL (non-GLES) headers do not define the EGLImage texture target types.
#ifndef GL_OES_EGL_image
typedef void *GLeglImageOES;
#endif
#ifndef PFNGLEGLIMAGETARGETTEXTURE2DOESPROC
typedef void (*PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)(GLenum target, GLeglImageOES image);
#endif

struct DmaPlane {
    int fd = -1;
    uint32_t fourcc = 0;
    uint32_t offset = 0;
    uint32_t pitch = 0;
    uint64_t modifier = kDrmModInvalid;
    int width = 0;
    int height = 0;
};

void closePrimeFds(const VADRMPRIMESurfaceDescriptor &prime)
{
    for (uint32_t i = 0; i < prime.num_objects; ++i) {
        if (prime.objects[i].fd >= 0) {
            close(prime.objects[i].fd);
        }
    }
}

struct PrimeGuard {
    VADRMPRIMESurfaceDescriptor desc{};
    ~PrimeGuard() { closePrimeFds(desc); }
};

// Flatten composed NV12 (1 layer / 2 planes) or separate R8+GR88 layers
// into two single-plane descriptors for EGLImage import.
bool flattenPrime(const VADRMPRIMESurfaceDescriptor &prime, int width, int height,
                  std::array<DmaPlane, 2> &planes)
{
    int n = 0;

    for (uint32_t l = 0; l < prime.num_layers; ++l) {
        const auto &layer = prime.layers[l];

        for (uint32_t p = 0; p < layer.num_planes; ++p) {
            if (n >= 2) {
                return false;
            }

            const auto &obj = prime.objects[layer.object_index[p]];
            DmaPlane &pl = planes[static_cast<size_t>(n)];
            pl.fd = obj.fd;
            pl.offset = layer.offset[p];
            pl.pitch = layer.pitch[p];
            pl.modifier = obj.drm_format_modifier;

            if (layer.drm_format == kDrmFormatNV12 && layer.num_planes > 1) {
                pl.fourcc = (p == 0) ? kDrmFormatR8 : kDrmFormatGR88;
            } else {
                pl.fourcc = layer.drm_format;
            }

            if (n == 0) {
                pl.width = width;
                pl.height = height;
            } else {
                pl.width = (width + 1) / 2;
                pl.height = (height + 1) / 2;
            }

            ++n;
        }
    }

    return n == 2;
}

// Export the VA surface as two single-plane DMA-BUF descriptors. Tries the
// separate-layers layout first, falls back to a composed NV12 layer.
// The caller owns the descriptor lifetime (its fds must stay open for as long
// as the EGLImages created from them are in use), so `desc` is passed in.
// Returns false on failure; *vaStatus is set to the VA error code when the
// export itself failed (as opposed to an unexpected plane layout).
bool exportPlanes(VADisplay vaDisplay, VASurfaceID vaSurface, int width, int height,
                  VADRMPRIMESurfaceDescriptor &desc, std::array<DmaPlane, 2> &planes,
                  VAStatus *vaStatus)
{
    uint32_t exportFlags = VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_SEPARATE_LAYERS;
    VAStatus status = vaExportSurfaceHandle(vaDisplay, vaSurface,
                                            VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                                            exportFlags, &desc);
    if (status != VA_STATUS_SUCCESS) {
        exportFlags = VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_COMPOSED_LAYERS;
        status = vaExportSurfaceHandle(vaDisplay, vaSurface,
                                       VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                                       exportFlags, &desc);
    }
    if (status != VA_STATUS_SUCCESS) {
        if (vaStatus) {
            *vaStatus = status;
        }
        return false;
    }

    return flattenPrime(desc, width, height, planes);
}

// Build the EGL_LINUX_DMA_BUF_EXT attribute list for one plane and create the
// EGLImage. Returns EGL_NO_IMAGE on failure (caller logs the EGL error).
EGLImageKHR createPlaneImage(EGLDisplay display, PFNEGLCREATEIMAGEKHRPROC createImage,
                             bool hasModifiers, const DmaPlane &plane)
{
    EGLint attribs[20];
    int a = 0;
    attribs[a++] = EGL_WIDTH;
    attribs[a++] = plane.width;
    attribs[a++] = EGL_HEIGHT;
    attribs[a++] = plane.height;
    attribs[a++] = EGL_LINUX_DRM_FOURCC_EXT;
    attribs[a++] = static_cast<EGLint>(plane.fourcc);
    attribs[a++] = EGL_DMA_BUF_PLANE0_FD_EXT;
    attribs[a++] = plane.fd;
    attribs[a++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
    attribs[a++] = static_cast<EGLint>(plane.offset);
    attribs[a++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
    attribs[a++] = static_cast<EGLint>(plane.pitch);
    if (hasModifiers && plane.modifier != kDrmModInvalid) {
        attribs[a++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
        attribs[a++] = static_cast<EGLint>(static_cast<uint32_t>(plane.modifier));
        attribs[a++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
        attribs[a++] = static_cast<EGLint>(static_cast<uint32_t>(plane.modifier >> 32));
    }
    attribs[a++] = EGL_NONE;

    return createImage(display, static_cast<EGLContext>(nullptr),
                       EGL_LINUX_DMA_BUF_EXT, nullptr, attribs);
}

bool hasEglExt(const char *exts, const char *name)
{
    return exts && std::strstr(exts, name);
}

// ---------------------------------------------------------------------------
// Shader sources (single body, parametrized for GLES / legacy GL / core GL)
// ---------------------------------------------------------------------------

std::string buildVertexSource(bool core, bool gles)
{
    std::string src;
    if (core) {
        src = "#version 150\n";
    } else if (!gles) {
        src = "#version 120\n";
    }
    if (core) {
        src += "in vec2 aPos;\n"
               "in vec2 aTex;\n"
               "out vec2 vTex;\n"
               "void main() {\n"
               "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
               "    vTex = aTex;\n"
               "}\n";
    } else {
        // GLES 2.0 has no explicit version line (ES 1.00 default).
        src += "attribute vec2 aPos;\n"
               "attribute vec2 aTex;\n"
               "varying vec2 vTex;\n"
               "void main() {\n"
               "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
               "    vTex = aTex;\n"
               "}\n";
    }
    return src;
}

std::string buildFragmentSource(bool core, bool gles)
{
    std::string src;
    if (core) {
        src = "#version 150\n";
    } else if (gles) {
        // `precision` is a keyword only in GLSL ES, never on desktop GL.
        src = "precision mediump float;\n";
    } else {
        src = "#version 120\n";
    }

    src += "uniform sampler2D texY;\n"
           "uniform sampler2D texUV;\n";
    if (core) {
        src += "in vec2 vTex;\n"
               "out vec4 fragColor;\n";
    } else {
        src += "varying vec2 vTex;\n";
    }
    const std::string sample = core ? std::string("texture") : std::string("texture2D");
    const std::string out = core ? std::string("fragColor") : std::string("gl_FragColor");
    src += "void main() {\n"
           "    float y = " + sample + "(texY, vTex).r;\n"
           "    vec2 uv = " + sample + "(texUV, vTex).rg - vec2(0.5);\n"
           "    y = (y - 0.062745) * 1.164;\n"
           "    " + out + " = vec4(y + 1.596 * uv.y,\n"
           "                     y - 0.391 * uv.x - 0.813 * uv.y,\n"
           "                     y + 2.018 * uv.x, 1.0);\n"
           "}\n";
    return src;
}

// Isolate our GL traffic from the Qt Quick scene graph.
// The previous version saved state *after* FBO creation and restored our FBO
// on the way out; Qt then sampled that same texture while it was still the
// color attachment — Mesa crashes in glDrawElements (feedback loop).
// Every capability we touch is saved/restored; the quad draw depends on
// blend/depth/scissor/cull being disabled, and Qt needs its own values back.
struct GLStateGuard {
    QOpenGLExtraFunctions *extra = nullptr;
    GLint viewport[4] = {};
    GLint fbo = 0;
    GLint program = 0;
    GLint activeTex = 0;
    GLint tex[2] = {0, 0};
    GLint arrayBuf = 0;
    GLint elementBuf = 0;
    GLint vao = 0;
    GLboolean caps[4] = {}; // blend, depth test, scissor test, cull face

    static constexpr GLenum kCaps[4] = {GL_BLEND, GL_DEPTH_TEST, GL_SCISSOR_TEST, GL_CULL_FACE};

    explicit GLStateGuard(QOpenGLContext *ctx)
        : extra(ctx->extraFunctions())
    {
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuf);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elementBuf);
        if (extra) {
            extra->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        }

        for (int i = 0; i < 4; ++i) {
            caps[i] = glIsEnabled(kCaps[i]);
            glDisable(kCaps[i]);
        }

        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex);
        for (int i = 0; i < 2; ++i) {
            glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + i));
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex[i]);
        }
    }

    ~GLStateGuard()
    {
        for (int i = 0; i < 4; ++i) {
            if (caps[i]) {
                glEnable(kCaps[i]);
            }
        }

        // VAO first: element-array binding lives in the VAO.
        if (extra) {
            extra->glBindVertexArray(static_cast<GLuint>(vao));
        }
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBuf));
        if (!extra || vao == 0) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(elementBuf));
        }
        glUseProgram(static_cast<GLuint>(program));

        for (int i = 0; i < 2; ++i) {
            glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + i));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(tex[i]));
        }
        glActiveTexture(static_cast<GLenum>(activeTex));

        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    }
};

} // namespace

struct QmlAVHWOutput_VAAPI_EGL::Priv
{
    EGLDisplay display = nullptr;
    bool hasModifiers = false;
    bool ready = false;

    PFNEGLCREATEIMAGEKHRPROC createImage = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC destroyImage = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC imageTargetTexture2D = nullptr;

    GLuint program = 0;
    GLint texYLoc = -1;
    GLint texUVLoc = -1;

    GLuint rgbTex = 0;
    GLuint fbo = 0;
    GLuint planeTex[2] = {0, 0};
    GLuint vbo = 0;
    GLuint vao = 0;
};

QmlAVHWOutput_VAAPI_EGL::QmlAVHWOutput_VAAPI_EGL()
    : m_egl(std::make_unique<Priv>())
{
}

QmlAVHWOutput_VAAPI_EGL::~QmlAVHWOutput_VAAPI_EGL()
{
    cleanupEGL();
}

QVariant QmlAVHWOutput_VAAPI_EGL::handle(const QmlAVVideoFrame &videoFrame)
{
    if (!videoFrame.isValid()) {
        return {};
    }
    if (videoFrame.pixelFormat() != QmlAVPixelFormat(AV_PIX_FMT_VAAPI)) {
        logWarning() << "Wrong pixel format: " << videoFrame.pixelFormat();
        return {};
    }

    // Qt5 VideoOutput + GLTextureHandle can only sample an RGB TEXTURE_2D.
    const QmlAVPixelFormat swFormat = videoFrame.swPixelFormat();
    if (swFormat != QmlAVPixelFormat(AV_PIX_FMT_NV12)) {
        logWarning() << "VAAPI-EGL MVP supports NV12 only, got " << swFormat;
        return {};
    }

    auto *ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        logWarning() << "No current OpenGL context. VAAPI-EGL requires Qt on EGL "
                        "(Wayland, or QT_XCB_GL_INTEGRATION=xcb_egl).";
        return {};
    }

    auto avHWFramesCtx = reinterpret_cast<AVHWFramesContext *>(videoFrame.avFrame()->hw_frames_ctx->data);
    auto vaDeviceCtx = static_cast<AVVAAPIDeviceContext *>(avHWFramesCtx->device_ctx->hwctx);
    VADisplay vaDisplay = vaDeviceCtx->display;
    VASurfaceID vaSurface = static_cast<VASurfaceID>(reinterpret_cast<uintptr_t>(videoFrame.avFrame()->data[3]));

    Contract newContract(videoFrame);
    if (m_contract != newContract) {
        m_contract = newContract;
        cleanupEGL();
    }

    // Capture Qt Quick state *before* any of our GL calls, including init.
    GLStateGuard state(ctx);

    if (!m_egl->ready && !initializeEGL(videoFrame.width(), videoFrame.height())) {
        return {};
    }

    vaSyncSurface(vaDisplay, vaSurface);

    // The fds owned by `prime` must stay open until the EGLImages below are
    // destroyed, so the guard lives here (not inside exportPlanes).
    PrimeGuard prime;
    std::array<DmaPlane, 2> planes{};
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    if (!exportPlanes(vaDisplay, vaSurface, videoFrame.width(), videoFrame.height(),
                      prime.desc, planes, &vaStatus)) {
        if (vaStatus != VA_STATUS_SUCCESS) {
            logWarning() << "vaExportSurfaceHandle() failed: 0x" << QmlAV::Hex << vaStatus;
        } else {
            logWarning() << "Unexpected DRM-PRIME layout (need 2 NV12 planes).";
        }
        return {};
    }

    QOpenGLExtraFunctions *extra = ctx->extraFunctions();

    EGLImageKHR images[2] = {};
    bool ok = true;

    for (int i = 0; i < 2 && ok; ++i) {
        images[i] = createPlaneImage(m_egl->display, m_egl->createImage,
                                     m_egl->hasModifiers, planes[static_cast<size_t>(i)]);
        if (!images[i]) {
            logWarning() << "eglCreateImageKHR() failed for plane " << i
                         << " (0x" << QmlAV::Hex << eglGetError() << ")";
            ok = false;
            break;
        }

        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + i));
        glBindTexture(GL_TEXTURE_2D, m_egl->planeTex[i]);
        setTextureParams();
        m_egl->imageTargetTexture2D(GL_TEXTURE_2D, images[i]);
    }

    if (ok) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_egl->fbo);
        glViewport(0, 0, videoFrame.width(), videoFrame.height());

        glUseProgram(m_egl->program);
        glUniform1i(m_egl->texYLoc, 0);
        glUniform1i(m_egl->texUVLoc, 1);

        bindQuad(extra);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        unbindQuad(extra);

        // Detach our FBO before returning the color texture to Qt.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    for (EGLImageKHR image : images) {
        if (image) {
            m_egl->destroyImage(m_egl->display, image);
        }
    }

    if (!ok) {
        return {};
    }

    return m_egl->rgbTex;
}

static std::string getInfoLog(GLuint obj, bool isProgram)
{
    GLint len = 0;
    if (isProgram) {
        glGetProgramiv(obj, GL_INFO_LOG_LENGTH, &len);
    } else {
        glGetShaderiv(obj, GL_INFO_LOG_LENGTH, &len);
    }
    if (len <= 1) {
        return {};
    }
    std::string log(static_cast<size_t>(len - 1), '\0');
    if (isProgram) {
        glGetProgramInfoLog(obj, len, nullptr, log.data());
    } else {
        glGetShaderInfoLog(obj, len, nullptr, log.data());
    }
    return log;
}

// Compile and link the YUV->RGB shader program. Returns 0 on failure.
GLuint QmlAVHWOutput_VAAPI_EGL::buildProgram(bool core, bool gles)
{
    const std::string vsrc = buildVertexSource(core, gles);
    const std::string fsrc = buildFragmentSource(core, gles);

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    const char *vSrc = vsrc.c_str();
    glShaderSource(vs, 1, &vSrc, nullptr);
    glCompileShader(vs);
    GLint ok = GL_FALSE;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        logWarning() << "Failed to compile vertex shader:" << getInfoLog(vs, false).c_str();
        glDeleteShader(vs);
        return 0;
    }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    const char *fSrc = fsrc.c_str();
    glShaderSource(fs, 1, &fSrc, nullptr);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        logWarning() << "Failed to compile fragment shader:" << getInfoLog(fs, false).c_str();
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }

    GLuint prog = glCreateProgram();
    glBindAttribLocation(prog, 0, "aPos");
    glBindAttribLocation(prog, 1, "aTex");
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        logWarning() << "Failed to link YUV->RGB shader:" << getInfoLog(prog, true).c_str();
        glDeleteProgram(prog);
        return 0;
    }

    m_egl->texYLoc = glGetUniformLocation(prog, "texY");
    m_egl->texUVLoc = glGetUniformLocation(prog, "texUV");
    return prog;
}

bool QmlAVHWOutput_VAAPI_EGL::initializeEGL(int width, int height)
{
    auto *ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        return false;
    }

    EGLDisplay dpy = eglGetCurrentDisplay();
    if (!dpy) {
        logWarning() << "No active EGL display found. Are you running under GLX? "
                        "Use Wayland or set QT_XCB_GL_INTEGRATION=xcb_egl.";
        return false;
    }

    const char *exts = eglQueryString(dpy, EGL_EXTENSIONS);
    if (!hasEglExt(exts, "EGL_EXT_image_dma_buf_import")) {
        logWarning() << "EGL_EXT_image_dma_buf_import is not supported by this display.";
        return false;
    }

    m_egl->createImage = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
    m_egl->destroyImage = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
    m_egl->imageTargetTexture2D = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
        ctx->getProcAddress("glEGLImageTargetTexture2DOES"));

    if (!m_egl->createImage || !m_egl->destroyImage || !m_egl->imageTargetTexture2D) {
        logWarning() << "Failed to get EGL/GL proc addresses for DMA-BUF import.";
        return false;
    }

    m_egl->display = dpy;
    m_egl->hasModifiers = hasEglExt(exts, "EGL_EXT_image_dma_buf_import_modifiers");

    const bool gles = ctx->isOpenGLES();
    const bool core = !gles && ctx->format().profile() == QSurfaceFormat::CoreProfile;

    m_egl->program = buildProgram(core, gles);
    if (!m_egl->program) {
        cleanupEGL();
        return false;
    }

    glGenTextures(1, &m_egl->rgbTex);
    glBindTexture(GL_TEXTURE_2D, m_egl->rgbTex);
    setTextureParams();
    glTexImage2D(GL_TEXTURE_2D, 0, gles ? GL_RGBA : GL_RGBA8,
                    width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, &m_egl->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_egl->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, m_egl->rgbTex, 0);
    const GLenum fbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (fbStatus != GL_FRAMEBUFFER_COMPLETE) {
        logWarning() << "RGB FBO incomplete: 0x" << QmlAV::Hex << fbStatus;
        cleanupEGL();
        return false;
    }

    glGenTextures(2, m_egl->planeTex);
    initQuad(ctx->extraFunctions());

    m_egl->ready = true;
    return true;
}

void QmlAVHWOutput_VAAPI_EGL::cleanupEGL()
{
    auto *ctx = QOpenGLContext::currentContext();
    if (ctx) {
        if (m_egl->fbo) {
            glDeleteFramebuffers(1, &m_egl->fbo);
            m_egl->fbo = 0;
        }
        if (m_egl->rgbTex) {
            glDeleteTextures(1, &m_egl->rgbTex);
            m_egl->rgbTex = 0;
        }
        if (m_egl->vbo) {
            glDeleteBuffers(1, &m_egl->vbo);
            m_egl->vbo = 0;
        }
        if (m_egl->planeTex[0] || m_egl->planeTex[1]) {
            glDeleteTextures(2, m_egl->planeTex);
            m_egl->planeTex[0] = 0;
            m_egl->planeTex[1] = 0;
        }
        if (m_egl->vao) {
            if (auto *extra = ctx->extraFunctions()) {
                extra->glDeleteVertexArrays(1, &m_egl->vao);
            }
            m_egl->vao = 0;
        }
    } else {
        m_egl->fbo = 0;
        m_egl->rgbTex = 0;
        m_egl->vbo = 0;
        m_egl->planeTex[0] = 0;
        m_egl->planeTex[1] = 0;
        m_egl->vao = 0;
    }

    if (m_egl->program) {
        glDeleteProgram(m_egl->program);
    }
    m_egl->program = 0;
    m_egl->texYLoc = -1;
    m_egl->texUVLoc = -1;
    m_egl->display = nullptr;
    m_egl->hasModifiers = false;
    m_egl->ready = false;
    m_egl->createImage = nullptr;
    m_egl->destroyImage = nullptr;
    m_egl->imageTargetTexture2D = nullptr;
}

void QmlAVHWOutput_VAAPI_EGL::setTextureParams()
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void QmlAVHWOutput_VAAPI_EGL::setupAttribs()
{
    glBindBuffer(GL_ARRAY_BUFFER, m_egl->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                             static_cast<GLsizei>(4 * sizeof(GLfloat)), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                             static_cast<GLsizei>(4 * sizeof(GLfloat)),
                             reinterpret_cast<const void *>(static_cast<uintptr_t>(2 * sizeof(GLfloat))));
}

void QmlAVHWOutput_VAAPI_EGL::bindQuad(QOpenGLExtraFunctions *extra)
{
    if (extra && m_egl->vao) {
        extra->glBindVertexArray(m_egl->vao);
    } else {
        setupAttribs();
    }
}

void QmlAVHWOutput_VAAPI_EGL::unbindQuad(QOpenGLExtraFunctions *extra)
{
    if (!(extra && m_egl->vao)) {
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
    }
}

void QmlAVHWOutput_VAAPI_EGL::initQuad(QOpenGLExtraFunctions *extra)
{
    // NDC (-1,-1) is the FBO's first stored row; v=0 is the top of the video
    // (DMA-BUF / EGLImage origin). Qt VideoOutput samples (0,0) at item top-left.
    static const GLfloat quad[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f
    };

    glGenBuffers(1, &m_egl->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_egl->vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(quad)), quad, GL_STATIC_DRAW);

    if (extra) {
        extra->glGenVertexArrays(1, &m_egl->vao);
        extra->glBindVertexArray(m_egl->vao);
        setupAttribs(); // capture the attribute layout into the VAO
        extra->glBindVertexArray(0);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

#endif
