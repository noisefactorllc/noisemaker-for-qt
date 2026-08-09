#include "surface.h"

#include <QJsonObject>
#include <QOpenGLFunctions_4_1_Core>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace nm {

namespace {

// True iff `obj[key]` is present AND not JSON null/undefined -- the exact
// shape of the reference's `??` (nullish coalescing) test.
bool jsonHasLiveValue(const QJsonObject& obj, const QString& key) {
    if (!obj.contains(key)) {
        return false;
    }
    const QJsonValue v = obj.value(key);
    return !v.isNull() && !v.isUndefined();
}

} // namespace

int resolveDimension(const QJsonValue& spec, int screenSize, const QJsonObject& mergedUniforms) {
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
            const QString paramKey = obj.value(QStringLiteral("param")).toString();
            // reference pipeline.js:1211 `uniforms[spec.param] ?? paramDefault`.
            const bool paramPresent = jsonHasLiveValue(mergedUniforms, paramKey);
            double value = paramPresent
                ? mergedUniforms.value(paramKey).toDouble()
                : (obj.contains(QStringLiteral("paramDefault"))
                       ? obj.value(QStringLiteral("paramDefault")).toDouble()
                       : 64.0);
            if (hasMultiply) {
                value *= obj.value(QStringLiteral("multiply")).toDouble();
            }
            if (hasPower) {
                value = std::pow(value, obj.value(QStringLiteral("power")).toDouble());
            }
            // reference: "If we have a transform AND the param wasn't found
            // in uniforms AND a 'default' is specified, use 'default' as
            // the final computed value" -- gated on absence, not on the
            // paramDefault fallback numerically coinciding with anything.
            if (hasTransform && !paramPresent && obj.contains(QStringLiteral("default"))) {
                value = obj.value(QStringLiteral("default")).toDouble();
            }
            return std::max(1, static_cast<int>(std::floor(value)));
        }

        if (obj.contains(QStringLiteral("screenDivide"))) {
            // reference pipeline.js:1244-1247:
            // `divisor = uniforms[spec.screenDivide] ?? spec.default ?? 1`.
            const QString divideKey = obj.value(QStringLiteral("screenDivide")).toString();
            const bool divideKeyPresent = jsonHasLiveValue(mergedUniforms, divideKey);
            const double divisor = divideKeyPresent
                ? mergedUniforms.value(divideKey).toDouble()
                : (obj.contains(QStringLiteral("default")) ? obj.value(QStringLiteral("default")).toDouble() : 1.0);
            // `safeDivisor` is a defensive addition beyond the reference:
            // JS division by 0 yields Infinity (safely Math.round/Math.max'd
            // to Infinity), but casting an infinite double to `int` in C++
            // is undefined behavior, so a divisor of exactly 0 is treated
            // as 1 instead of reproducing that UB.
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

    // reference/05-webgl2-backend.md §10.1 createFBO requires checking
    // FRAMEBUFFER_COMPLETE after attachment; a silently incomplete FBO
    // would make every subsequent draw into it a no-op (or driver-defined
    // garbage) instead of a loud, diagnosable failure.
    const GLenum fboStatus = m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fboStatus != GL_FRAMEBUFFER_COMPLETE) {
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        m_gl->glDeleteFramebuffers(1, &fbo);
        m_gl->glDeleteTextures(1, &texture);
        throw std::runtime_error(
            QStringLiteral("nm::SurfaceCache: incomplete framebuffer (status 0x%1) for a %2x%3 '%4' surface")
                .arg(static_cast<uint>(fboStatus), 0, 16)
                .arg(width)
                .arg(height)
                .arg(format)
                .toStdString());
    }

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

SurfaceCache::ResolvedSpec SurfaceCache::resolveSpec(const Graph& graph, const QString& specTexId, QSize screenSize,
                                                       const QJsonObject& mergedUniforms) const {
    ResolvedSpec resolved{screenSize.width(), screenSize.height(), QStringLiteral("rgba16f")};

    const auto specIt = graph.textures.find(specTexId);
    if (specIt != graph.textures.end()) {
        resolved.width = resolveDimension(specIt->width, screenSize.width(), mergedUniforms);
        resolved.height = resolveDimension(specIt->height, screenSize.height(), mergedUniforms);
        if (!specIt->format.isEmpty()) {
            resolved.format = specIt->format;
        }
    }
    // else: no explicit TextureSpec. Every non-global virtual texId used by
    // any pass is guaranteed an entry in graph.textures (verified against
    // real exported graphs -- see task report); this branch is therefore
    // only reachable for `global_*` surfaces, which default to
    // screen-sized rgba16f exactly like reference/04 §8 "Display surfaces".
    return resolved;
}

GpuSurface& SurfaceCache::getOrCreate(const QString& cacheKey, const ResolvedSpec& spec) {
    auto it = m_surfaces.find(cacheKey);
    if (it != m_surfaces.end()) {
        return it.value();
    }
    return *m_surfaces.insert(cacheKey, createSurface(spec.width, spec.height, spec.format));
}

GpuSurface& SurfaceCache::get(const Graph& graph, const QString& texId, QSize screenSize,
                               const QJsonObject& mergedUniforms) {
    auto it = m_surfaces.find(texId);
    if (it != m_surfaces.end()) {
        return it.value();
    }
    return getOrCreate(texId, resolveSpec(graph, texId, screenSize, mergedUniforms));
}

GpuSurface& SurfaceCache::getAliased(const QString& physicalKey, const Graph& graph, const QString& specTexId,
                                      QSize screenSize, const QJsonObject& mergedUniforms) {
    auto it = m_surfaces.find(physicalKey);
    if (it != m_surfaces.end()) {
        return it.value();
    }
    return getOrCreate(physicalKey, resolveSpec(graph, specTexId, screenSize, mergedUniforms));
}

unsigned int SurfaceCache::mrtFramebuffer(const QMap<int, unsigned int>& attachments) {
    QString key;
    for (auto it = attachments.begin(); it != attachments.end(); ++it) {
        key += QString::number(it.key()) + QLatin1Char(':') + QString::number(it.value()) + QLatin1Char(',');
    }
    const auto cached = m_mrtFbos.constFind(key);
    if (cached != m_mrtFbos.constEnd()) {
        return cached.value();
    }

    unsigned int fbo = 0;
    m_gl->glGenFramebuffers(1, &fbo);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    int maxLocation = 0;
    for (auto it = attachments.begin(); it != attachments.end(); ++it) {
        m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(it.key()),
                                      GL_TEXTURE_2D, it.value(), 0);
        maxLocation = std::max(maxLocation, it.key());
    }

    // reference createMRTFBO / webgl2.js executePass: glDrawBuffers must
    // list every attachment location the shader's `layout(location=N) out`
    // declarations use, GL_NONE for any gap so unused locations don't
    // silently receive writes from a differently-shaped program sharing
    // the same cache_key space (not currently possible here since the key
    // is exact, but matches the reference's own explicit-array approach).
    QVector<GLenum> drawBuffers(maxLocation + 1, GL_NONE);
    for (auto it = attachments.begin(); it != attachments.end(); ++it) {
        drawBuffers[it.key()] = GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(it.key());
    }
    m_gl->glDrawBuffers(drawBuffers.size(), drawBuffers.constData());

    const GLenum status = m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        m_gl->glDeleteFramebuffers(1, &fbo);
        throw std::runtime_error(
            QStringLiteral("nm::SurfaceCache: incomplete MRT framebuffer (status 0x%1)")
                .arg(static_cast<uint>(status), 0, 16)
                .toStdString());
    }
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);

    m_mrtFbos.insert(key, fbo);
    return fbo;
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
    for (auto it = m_mrtFbos.begin(); it != m_mrtFbos.end(); ++it) {
        unsigned int fbo = it.value();
        m_gl->glDeleteFramebuffers(1, &fbo);
    }
    m_mrtFbos.clear();
    if (m_defaultTexture != 0) {
        m_gl->glDeleteTextures(1, &m_defaultTexture);
        m_defaultTexture = 0;
    }
}

} // namespace nm
