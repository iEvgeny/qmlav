#include "qmlavhwoutput_vaapi_egl.h"

#if defined(__linux__) && !defined(__ANDROID__)
#include "qmlavutils.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <unistd.h>

#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>

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
    #include <libavutil/hwcontext_vaapi.h>
}

namespace {

constexpr uint32_t fourcc(char a, char b, char c, char d)
{
    return static_cast<uint32_t>(static_cast<unsigned char>(a))
         | (static_cast<uint32_t>(static_cast<unsigned char>(b)) << 8)
         | (static_cast<uint32_t>(static_cast<unsigned char>(c)) << 16)
         | (static_cast<uint32_t>(static_cast<unsigned char>(d)) << 24);
}

constexpr uint32_t kDrmFormatR8   = fourcc('R', '8', ' ', ' ');
constexpr uint32_t kDrmFormatGR88 = fourcc('G', 'R', '8', '8');
constexpr uint32_t kDrmFormatNV12 = fourcc('N', 'V', '1', '2');
constexpr uint64_t kDrmModInvalid = 0x00ffffffffffffffULL;

#ifndef EGL_LINUX_DMA_BUF_EXT
#define EGL_LINUX_DMA_BUF_EXT              0x3270
#define EGL_LINUX_DRM_FOURCC_EXT           0x3271
#define EGL_DMA_BUF_PLANE0_FD_EXT          0x3272
#define EGL_DMA_BUF_PLANE0_OFFSET_EXT      0x3273
#define EGL_DMA_BUF_PLANE0_PITCH_EXT       0x3274
#endif
#ifndef EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT
#define EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT 0x3443
#define EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT 0x3444
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER_BINDING
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#endif

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

bool hasEglExt(const char *exts, const char *name)
{
    return exts && std::strstr(exts, name);
}

// Isolate our GL traffic from the Qt Quick scene graph.
// The previous version saved state *after* FBO creation and restored our FBO
// on the way out; Qt then sampled that same texture while it was still the
// color attachment — Mesa crashes in glDrawElements (feedback loop).
struct GLStateGuard {
    QOpenGLFunctions *f = nullptr;
    QOpenGLExtraFunctions *extra = nullptr;
    GLint viewport[4] = {};
    GLint fbo = 0;
    GLint program = 0;
    GLint activeTex = 0;
    GLint tex0 = 0;
    GLint tex1 = 0;
    GLint arrayBuf = 0;
    GLint elementBuf = 0;
    GLint vao = 0;
    GLboolean blend = GL_FALSE;
    GLboolean depth = GL_FALSE;
    GLboolean scissor = GL_FALSE;
    GLboolean cull = GL_FALSE;

    explicit GLStateGuard(QOpenGLContext *ctx)
        : f(ctx->functions())
        , extra(ctx->extraFunctions())
    {
        f->glGetIntegerv(GL_VIEWPORT, viewport);
        f->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        f->glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        f->glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex);
        f->glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuf);
        f->glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elementBuf);
        if (extra) {
            extra->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        }

        f->glActiveTexture(GL_TEXTURE0);
        f->glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex0);
        f->glActiveTexture(GL_TEXTURE1);
        f->glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex1);

        blend = f->glIsEnabled(GL_BLEND);
        depth = f->glIsEnabled(GL_DEPTH_TEST);
        scissor = f->glIsEnabled(GL_SCISSOR_TEST);
        cull = f->glIsEnabled(GL_CULL_FACE);

        f->glDisable(GL_BLEND);
        f->glDisable(GL_DEPTH_TEST);
        f->glDisable(GL_SCISSOR_TEST);
        f->glDisable(GL_CULL_FACE);
    }

    ~GLStateGuard()
    {
        if (blend) { f->glEnable(GL_BLEND); } else { f->glDisable(GL_BLEND); }
        if (depth) { f->glEnable(GL_DEPTH_TEST); } else { f->glDisable(GL_DEPTH_TEST); }
        if (scissor) { f->glEnable(GL_SCISSOR_TEST); } else { f->glDisable(GL_SCISSOR_TEST); }
        if (cull) { f->glEnable(GL_CULL_FACE); } else { f->glDisable(GL_CULL_FACE); }

        // VAO first: element-array binding lives in the VAO.
        if (extra) {
            extra->glBindVertexArray(static_cast<GLuint>(vao));
        }
        f->glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBuf));
        if (!extra || vao == 0) {
            f->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(elementBuf));
        }
        f->glUseProgram(static_cast<GLuint>(program));

        f->glActiveTexture(GL_TEXTURE0);
        f->glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(tex0));
        f->glActiveTexture(GL_TEXTURE1);
        f->glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(tex1));
        f->glActiveTexture(static_cast<GLenum>(activeTex));

        f->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));
        f->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
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

    std::unique_ptr<QOpenGLShaderProgram> program;

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
        logWarning() << QString("Wrong pixel format: ") << videoFrame.pixelFormat();
        return {};
    }

    // Qt5 VideoOutput + GLTextureHandle can only sample an RGB TEXTURE_2D.
    const QmlAVPixelFormat swFormat = videoFrame.swPixelFormat();
    if (swFormat != QmlAVPixelFormat(AV_PIX_FMT_NV12)) {
        logWarning() << QString("VAAPI-EGL MVP supports NV12 only, got ") << swFormat;
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

    PrimeGuard prime;
    uint32_t exportFlags = VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_SEPARATE_LAYERS;
    VAStatus status = vaExportSurfaceHandle(vaDisplay, vaSurface,
                                            VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                                            exportFlags, &prime.desc);
    if (status != VA_STATUS_SUCCESS) {
        exportFlags = VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_COMPOSED_LAYERS;
        status = vaExportSurfaceHandle(vaDisplay, vaSurface,
                                       VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                                       exportFlags, &prime.desc);
    }
    if (status != VA_STATUS_SUCCESS) {
        logWarning() << "vaExportSurfaceHandle() failed: 0x" << QmlAV::Hex << status;
        return {};
    }

    std::array<DmaPlane, 2> planes{};
    if (!flattenPrime(prime.desc, videoFrame.width(), videoFrame.height(), planes)) {
        logWarning() << "Unexpected DRM-PRIME layout (need 2 NV12 planes).";
        return {};
    }

    QOpenGLFunctions *f = ctx->functions();
    QOpenGLExtraFunctions *extra = ctx->extraFunctions();

    EGLImageKHR images[2] = {};
    bool ok = true;

    for (int i = 0; i < 2 && ok; ++i) {
        const auto &pl = planes[static_cast<size_t>(i)];

        EGLint attribs[20];
        int a = 0;
        attribs[a++] = EGL_WIDTH;
        attribs[a++] = pl.width;
        attribs[a++] = EGL_HEIGHT;
        attribs[a++] = pl.height;
        attribs[a++] = EGL_LINUX_DRM_FOURCC_EXT;
        attribs[a++] = static_cast<EGLint>(pl.fourcc);
        attribs[a++] = EGL_DMA_BUF_PLANE0_FD_EXT;
        attribs[a++] = pl.fd;
        attribs[a++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
        attribs[a++] = static_cast<EGLint>(pl.offset);
        attribs[a++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
        attribs[a++] = static_cast<EGLint>(pl.pitch);
        if (m_egl->hasModifiers && pl.modifier != kDrmModInvalid) {
            attribs[a++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
            attribs[a++] = static_cast<EGLint>(static_cast<uint32_t>(pl.modifier));
            attribs[a++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
            attribs[a++] = static_cast<EGLint>(static_cast<uint32_t>(pl.modifier >> 32));
        }
        attribs[a++] = EGL_NONE;

        images[i] = m_egl->createImage(m_egl->display, static_cast<EGLContext>(nullptr),
                                       EGL_LINUX_DMA_BUF_EXT, nullptr, attribs);
        if (!images[i]) {
            logWarning() << "eglCreateImageKHR() failed for plane " << i
                         << " (0x" << QmlAV::Hex << eglGetError() << ")";
            ok = false;
            break;
        }

        f->glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + i));
        f->glBindTexture(GL_TEXTURE_2D, m_egl->planeTex[i]);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_egl->imageTargetTexture2D(GL_TEXTURE_2D, images[i]);
    }

    if (ok) {
        f->glBindFramebuffer(GL_FRAMEBUFFER, m_egl->fbo);
        f->glViewport(0, 0, videoFrame.width(), videoFrame.height());

        m_egl->program->bind();
        m_egl->program->setUniformValue("texY", 0);
        m_egl->program->setUniformValue("texUV", 1);

        if (extra && m_egl->vao) {
            extra->glBindVertexArray(m_egl->vao);
        } else {
            f->glBindBuffer(GL_ARRAY_BUFFER, m_egl->vbo);
            f->glEnableVertexAttribArray(0);
            f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                                     static_cast<GLsizei>(4 * sizeof(GLfloat)),
                                     nullptr);
            f->glEnableVertexAttribArray(1);
            f->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                                     static_cast<GLsizei>(4 * sizeof(GLfloat)),
                                     reinterpret_cast<const void *>(static_cast<uintptr_t>(2 * sizeof(GLfloat))));
        }

        f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        if (!(extra && m_egl->vao)) {
            f->glDisableVertexAttribArray(0);
            f->glDisableVertexAttribArray(1);
        }

        // Detach our FBO before returning the color texture to Qt.
        f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
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

    QByteArray vsrc;
    QByteArray fsrc;
    if (gles) {
        vsrc = "attribute vec2 aPos;\n"
               "attribute vec2 aTex;\n"
               "varying vec2 vTex;\n"
               "void main() {\n"
               "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
               "    vTex = aTex;\n"
               "}\n";
        fsrc = "precision mediump float;\n"
               "uniform sampler2D texY;\n"
               "uniform sampler2D texUV;\n"
               "varying vec2 vTex;\n"
               "void main() {\n"
               "    float y = texture2D(texY, vTex).r;\n"
               "    vec2 uv = texture2D(texUV, vTex).rg - vec2(0.5);\n"
               "    y = (y - 0.062745) * 1.164;\n"
               "    gl_FragColor = vec4(y + 1.596 * uv.y,\n"
               "                        y - 0.391 * uv.x - 0.813 * uv.y,\n"
               "                        y + 2.018 * uv.x, 1.0);\n"
               "}\n";
    } else if (core) {
        vsrc = "#version 150\n"
               "in vec2 aPos;\n"
               "in vec2 aTex;\n"
               "out vec2 vTex;\n"
               "void main() {\n"
               "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
               "    vTex = aTex;\n"
               "}\n";
        fsrc = "#version 150\n"
               "uniform sampler2D texY;\n"
               "uniform sampler2D texUV;\n"
               "in vec2 vTex;\n"
               "out vec4 fragColor;\n"
               "void main() {\n"
               "    float y = texture(texY, vTex).r;\n"
               "    vec2 uv = texture(texUV, vTex).rg - vec2(0.5);\n"
               "    y = (y - 0.062745) * 1.164;\n"
               "    fragColor = vec4(y + 1.596 * uv.y,\n"
               "                     y - 0.391 * uv.x - 0.813 * uv.y,\n"
               "                     y + 2.018 * uv.x, 1.0);\n"
               "}\n";
    } else {
        vsrc = "#version 120\n"
               "attribute vec2 aPos;\n"
               "attribute vec2 aTex;\n"
               "varying vec2 vTex;\n"
               "void main() {\n"
               "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
               "    vTex = aTex;\n"
               "}\n";
        fsrc = "#version 120\n"
               "uniform sampler2D texY;\n"
               "uniform sampler2D texUV;\n"
               "varying vec2 vTex;\n"
               "void main() {\n"
               "    float y = texture2D(texY, vTex).r;\n"
               "    vec2 uv = texture2D(texUV, vTex).rg - vec2(0.5);\n"
               "    y = (y - 0.062745) * 1.164;\n"
               "    gl_FragColor = vec4(y + 1.596 * uv.y,\n"
               "                        y - 0.391 * uv.x - 0.813 * uv.y,\n"
               "                        y + 2.018 * uv.x, 1.0);\n"
               "}\n";
    }

    m_egl->program = std::make_unique<QOpenGLShaderProgram>();
    m_egl->program->bindAttributeLocation("aPos", 0);
    m_egl->program->bindAttributeLocation("aTex", 1);
    if (!m_egl->program->addShaderFromSourceCode(QOpenGLShader::Vertex, vsrc) ||
        !m_egl->program->addShaderFromSourceCode(QOpenGLShader::Fragment, fsrc) ||
        !m_egl->program->link()) {
        logWarning() << "Failed to build NV12->RGB shader: " << m_egl->program->log();
        cleanupEGL();
        return false;
    }

    QOpenGLFunctions *f = ctx->functions();

    f->glGenTextures(1, &m_egl->rgbTex);
    f->glBindTexture(GL_TEXTURE_2D, m_egl->rgbTex);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    f->glTexImage2D(GL_TEXTURE_2D, 0, gles ? GL_RGBA : GL_RGBA8,
                    width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    f->glGenFramebuffers(1, &m_egl->fbo);
    f->glBindFramebuffer(GL_FRAMEBUFFER, m_egl->fbo);
    f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, m_egl->rgbTex, 0);
    const GLenum fbStatus = f->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (fbStatus != GL_FRAMEBUFFER_COMPLETE) {
        logWarning() << "RGB FBO incomplete: 0x" << QmlAV::Hex << fbStatus;
        cleanupEGL();
        return false;
    }

    f->glGenTextures(2, m_egl->planeTex);
    f->glGenBuffers(1, &m_egl->vbo);

    // NDC (-1,-1) is the FBO's first stored row; v=0 is the top of the video
    // (DMA-BUF / EGLImage origin). Qt VideoOutput samples (0,0) at item top-left.
    static const GLfloat quad[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f
    };

    f->glBindBuffer(GL_ARRAY_BUFFER, m_egl->vbo);
    f->glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(quad)), quad, GL_STATIC_DRAW);

    if (auto *extra = ctx->extraFunctions()) {
        extra->glGenVertexArrays(1, &m_egl->vao);
        extra->glBindVertexArray(m_egl->vao);
        f->glBindBuffer(GL_ARRAY_BUFFER, m_egl->vbo);
        f->glEnableVertexAttribArray(0);
        f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                                 static_cast<GLsizei>(4 * sizeof(GLfloat)), nullptr);
        f->glEnableVertexAttribArray(1);
        f->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                                 static_cast<GLsizei>(4 * sizeof(GLfloat)),
                                 reinterpret_cast<const void *>(static_cast<uintptr_t>(2 * sizeof(GLfloat))));
        extra->glBindVertexArray(0);
    }

    f->glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_egl->ready = true;
    return true;
}

void QmlAVHWOutput_VAAPI_EGL::cleanupEGL()
{
    auto *ctx = QOpenGLContext::currentContext();
    if (ctx) {
        QOpenGLFunctions *f = ctx->functions();
        if (m_egl->fbo) {
            f->glDeleteFramebuffers(1, &m_egl->fbo);
            m_egl->fbo = 0;
        }
        if (m_egl->rgbTex) {
            f->glDeleteTextures(1, &m_egl->rgbTex);
            m_egl->rgbTex = 0;
        }
        if (m_egl->vbo) {
            f->glDeleteBuffers(1, &m_egl->vbo);
            m_egl->vbo = 0;
        }
        if (m_egl->planeTex[0] || m_egl->planeTex[1]) {
            f->glDeleteTextures(2, m_egl->planeTex);
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

    m_egl->program.reset();
    m_egl->display = nullptr;
    m_egl->hasModifiers = false;
    m_egl->ready = false;
    m_egl->createImage = nullptr;
    m_egl->destroyImage = nullptr;
    m_egl->imageTargetTexture2D = nullptr;
}

#endif
