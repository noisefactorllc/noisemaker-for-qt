#include "surface.h"

#include <QJsonObject>
#include <QOpenGLFunctions_4_1_Core>

#include <algorithm>
#include <cmath>

namespace nm {

int resolveDimension(const QJsonValue& spec, int screenSize) {
    if (spec.isDouble()) {
        return std::max(1, static_cast<int>(std::floor(spec.toDouble())));
    }

    if (spec.isString()) {
        const QString s = spec.toString();
        if (s == QStringLiteral("screen") || s == QStringLiteral("auto")) {
            return screenSize;
        }
        if (s.endsWith(QLatin1Char('%'))) {
            bool ok = false;
            const double pct = s.left(s.size() - 1).toDouble(&ok);
            if (ok) {
                return std::max(1, static_cast<int>(std::floor(screenSize * pct / 100.0)));
            }
        }
        // Unrecognized string (e.g. a bare "input" some effect texture
        // specs use) falls through to the reference's own fallback
        // (reference/04-resources-pipeline.md §9 rule 5: "fallback ->
        // screenSize").
        return screenSize;
    }

    if (spec.isObject()) {
        const QJsonObject obj = spec.toObject();

        if (obj.contains(QStringLiteral("param"))) {
            const bool hasMultiply = obj.contains(QStringLiteral("multiply"));
            const bool hasPower = obj.contains(QStringLiteral("power"));
            const bool hasTransform = hasMultiply || hasPower;
            // No live pass-uniform context is wired up in T3 (see this
            // function's header doc) -- `uniforms[spec.param]` is always
            // treated as absent, so `value` starts at paramDefault exactly
            // as the reference does when the named uniform is undefined.
            double value = obj.contains(QStringLiteral("paramDefault"))
                ? obj.value(QStringLiteral("paramDefault")).toDouble()
                : 64.0;
            if (hasMultiply) {
                value *= obj.value(QStringLiteral("multiply")).toDouble();
            }
            if (hasPower) {
                value = std::pow(value, obj.value(QStringLiteral("power")).toDouble());
            }
            if (hasTransform && obj.contains(QStringLiteral("default"))) {
                value = obj.value(QStringLiteral("default")).toDouble();
            }
            return std::max(1, static_cast<int>(std::floor(value)));
        }

        if (obj.contains(QStringLiteral("screenDivide"))) {
            const double divisor = obj.contains(QStringLiteral("default"))
                ? obj.value(QStringLiteral("default")).toDouble()
                : 1.0;
            const double safeDivisor = (divisor != 0.0) ? divisor : 1.0;
            return std::max(1, static_cast<int>(std::round(screenSize / safeDivisor)));
        }

        if (obj.contains(QStringLiteral("scale"))) {
            double computed = std::floor(screenSize * obj.value(QStringLiteral("scale")).toDouble());
            if (obj.contains(QStringLiteral("clamp"))) {
                const QJsonObject clamp = obj.value(QStringLiteral("clamp")).toObject();
                if (clamp.contains(QStringLiteral("min"))) {
                    computed = std::max(computed, clamp.value(QStringLiteral("min")).toDouble());
                }
                if (clamp.contains(QStringLiteral("max"))) {
                    computed = std::min(computed, clamp.value(QStringLiteral("max")).toDouble());
                }
            }
            return std::max(1, static_cast<int>(computed));
        }

        return screenSize;
    }

    return screenSize;
}

namespace {

// reference/05-webgl2-backend.md §6.1: format table, fallback for an
// unrecognized format string is rgba8 (distinct from the *default* used
// when no format is specified at all -- callers of createSurface() pass
// "rgba16f" for that case; see SurfaceCache::get()).
void resolveGlFormat(const QString& format, unsigned int* internalFormat, unsigned int* glFormat, unsigned int* glType) {
    *glFormat = GL_RGBA;
    if (format == QStringLiteral("rgba16f") || format == QStringLiteral("rgba16float")) {
        *internalFormat = GL_RGBA16F;
        *glType = GL_HALF_FLOAT;
    } else if (format == QStringLiteral("rgba32f")) {
        *internalFormat = GL_RGBA32F;
        *glType = GL_FLOAT;
    } else if (format == QStringLiteral("rgba8") || format == QStringLiteral("rgba8unorm")) {
        *internalFormat = GL_RGBA8;
        *glType = GL_UNSIGNED_BYTE;
    } else {
        *internalFormat = GL_RGBA8;
        *glType = GL_UNSIGNED_BYTE;
    }
}

} // namespace

SurfaceCache::SurfaceCache(QOpenGLFunctions_4_1_Core* gl) : m_gl(gl) {}

GpuSurface SurfaceCache::createSurface(int width, int height, const QString& format) {
    unsigned int internalFormat = 0, glFormat = 0, glType = 0;
    resolveGlFormat(format, &internalFormat, &glFormat, &glType);

    unsigned int texture = 0;
    m_gl->glGenTextures(1, &texture);
    m_gl->glBindTexture(GL_TEXTURE_2D, texture);
    m_gl->glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat), width, height, 0,
                        glFormat, glType, nullptr);
    // PORTING-GUIDE.md "GL state parity rules": intermediates are always
    // NEAREST + CLAMP_TO_EDGE.
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);

    unsigned int fbo = 0;
    m_gl->glGenFramebuffers(1, &fbo);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

    // Clear to transparent black once at creation time only (reference
    // createTexture: "if spec.usage includes 'render', clear to
    // transparent black" -- initializes storage). PORTING-GUIDE.md: "No
    // automatic color clear per pass" -- targets otherwise carry prior
    // contents across passes/frames, so this must NOT be repeated later.
    m_gl->glViewport(0, 0, width, height);
    m_gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    m_gl->glClear(GL_COLOR_BUFFER_BIT);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);

    GpuSurface surface;
    surface.texture = texture;
    surface.fbo = fbo;
    surface.width = width;
    surface.height = height;
    return surface;
}

GpuSurface& SurfaceCache::get(const Graph& graph, const QString& texId, QSize screenSize) {
    auto it = m_surfaces.find(texId);
    if (it != m_surfaces.end()) {
        return it.value();
    }

    int width = screenSize.width();
    int height = screenSize.height();
    QString format = QStringLiteral("rgba16f");

    const auto specIt = graph.textures.find(texId);
    if (specIt != graph.textures.end()) {
        width = resolveDimension(specIt->width, screenSize.width());
        height = resolveDimension(specIt->height, screenSize.height());
        if (!specIt->format.isEmpty()) {
            format = specIt->format;
        }
    }
    // else: no explicit TextureSpec. Every non-global virtual texId used by
    // any pass is guaranteed an entry in graph.textures (verified against
    // real exported graphs -- see task report); this branch is therefore
    // only reachable for `global_*` surfaces, which default to
    // screen-sized rgba16f exactly like reference/04 §8 "Display surfaces".

    return *m_surfaces.insert(texId, createSurface(width, height, format));
}

bool SurfaceCache::contains(const QString& texId) const {
    return m_surfaces.contains(texId);
}

const GpuSurface* SurfaceCache::find(const QString& texId) const {
    const auto it = m_surfaces.constFind(texId);
    return it == m_surfaces.constEnd() ? nullptr : &it.value();
}

unsigned int SurfaceCache::defaultTextureHandle() {
    if (m_defaultTexture != 0) {
        return m_defaultTexture;
    }
    m_gl->glGenTextures(1, &m_defaultTexture);
    m_gl->glBindTexture(GL_TEXTURE_2D, m_defaultTexture);
    const unsigned char pixel[4] = {0, 0, 0, 0};
    m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    return m_defaultTexture;
}

void SurfaceCache::releaseAll() {
    for (auto it = m_surfaces.begin(); it != m_surfaces.end(); ++it) {
        m_gl->glDeleteFramebuffers(1, &it->fbo);
        m_gl->glDeleteTextures(1, &it->texture);
    }
    m_surfaces.clear();
    if (m_defaultTexture != 0) {
        m_gl->glDeleteTextures(1, &m_defaultTexture);
        m_defaultTexture = 0;
    }
}

} // namespace nm
