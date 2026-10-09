#include "surface.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOpenGLFunctions_4_1_Core>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>
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

namespace {

// The dedup/warning key the reference derives for an unknown dimension
// spec (pipeline.js guard): non-objects use String(spec); objects
// AND arrays go through JSON.stringify (typeof [] === 'object'), which
// the compact QJsonDocument form matches element-for-element. QJsonDocument
// emits sorted object keys (the reference preserves insertion order), so
// two object specs differing only in key order share one dedup key — a
// documented deviation that only coalesces the warning/record for
// equivalent specs; the resolved value is unaffected.
QString dimensionFallbackKey(const QJsonValue& spec) {
    if (spec.isObject()) {
        return QString::fromUtf8(QJsonDocument(spec.toObject()).toJson(QJsonDocument::Compact));
    }
    if (spec.isArray()) {
        return QString::fromUtf8(
            QJsonDocument::fromVariant(spec.toArray().toVariantList()).toJson(QJsonDocument::Compact));
    }
    return spec.toVariant().toString();
}

// JS `Number(arrayValue)`: arrays coerce through their comma-joined String()
// form (Array.prototype.toString), which never yields a numeric string for
// more than one element -- the separator comma itself makes Number() NaN --
// while a single-element array degenerates to Number of that element's
// String(). The recursion below is exact without building the string at all:
// - empty array: Number("") is 0;
// - two or more elements: the join always contains a comma, and no
//   comma-containing string parses as a JS number, so NaN;
// - a single numeric element e: Number(String(e)) === e for every finite
//   double (the ECMAScript ToString/ToNumber round-trip), so e is returned
//   directly and no decimal formatting can perturb it;
// - a single string element keeps the string rules (trim, empty -> 0);
// - a single null/undefined element contributes the empty string -> 0;
// - a single bool element ("true"/"false") or object element
//   ("[object Object]") parses as NaN; nested arrays flatten by recursion.
double jsNumberOfArray(const QJsonArray& array) {
    if (array.isEmpty()) {
        return 0.0;
    }
    if (array.size() > 1) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const QJsonValue element = array.first();
    if (element.isDouble()) {
        return element.toDouble();
    }
    if (element.isArray()) {
        return jsNumberOfArray(element.toArray());
    }
    if (element.isString()) {
        const QString trimmed = element.toString().trimmed();
        if (trimmed.isEmpty()) {
            return 0.0;
        }
        bool ok = false;
        const double parsed = trimmed.toDouble(&ok);
        return ok ? parsed : std::numeric_limits<double>::quiet_NaN();
    }
    if (element.isNull() || element.isUndefined()) {
        return 0.0;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

// JS `Number(value)` for the JSON shapes a merged-uniform lookup can return:
// numbers pass through, booleans map to 0/1, numeric strings parse as decimals
// (JS trims surrounding whitespace and maps the empty string to 0), null maps
// to 0, and arrays coerce exactly as jsNumberOfArray documents -- so [32] is
// 32, [] is 0, and ["a","b"] is NaN. Objects and non-numeric strings map to
// NaN -- a function wrapper or any other nonnumeric authored uniform value
// lands in NaN, which is exactly the input the reference's
// `!Number.isFinite(Number(value))` dimension guards (upstream 00fb941c)
// exist for.
double jsNumber(const QJsonValue& value) {
    if (value.isDouble()) {
        return value.toDouble();
    }
    if (value.isBool()) {
        return value.toBool() ? 1.0 : 0.0;
    }
    if (value.isArray()) {
        return jsNumberOfArray(value.toArray());
    }
    if (value.isString()) {
        const QString trimmed = value.toString().trimmed();
        if (trimmed.isEmpty()) {
            return 0.0;
        }
        bool ok = false;
        const double parsed = trimmed.toDouble(&ok);
        return ok ? parsed : std::numeric_limits<double>::quiet_NaN();
    }
    if (value.isNull()) {
        return 0.0;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

} // namespace

int resolveDimension(const QJsonValue& spec, int screenSize, const QJsonObject& mergedUniforms,
                     DiagnosticSink* sink) {
    // An absent/null spec is a default, not an unknown form: the
    // reference's guard excludes undefined/null and this port's
    // callers use a null QJsonValue for "no spec" exactly like the
    // reference's `undefined` (no diagnostic).
    if (spec.isNull() || spec.isUndefined()) {
        return screenSize;
    }

    if (spec.isDouble()) {
        return std::max(1, static_cast<int>(std::floor(spec.toDouble())));
    }

    if (spec.isString()) {
        const QString s = spec.toString();
        // 'input' and 'resolution' are validator-accepted dimension
        // keywords (DIM_KEYWORDS in the reference effect-validator.js)
        // whose historical resolution is the screen dimension; they are
        // recognized forms, not unknown fallbacks, so they add no
        // diagnostic (upstream a0e9bbff).
        if (s == QStringLiteral("screen") || s == QStringLiteral("auto")
            || s == QStringLiteral("input") || s == QStringLiteral("resolution")) {
            return screenSize;
        }
        if (s.endsWith(QLatin1Char('%'))) {
            bool ok = false;
            const double pct = s.left(s.size() - 1).toDouble(&ok);
            if (ok) {
                return std::max(1, static_cast<int>(std::floor(screenSize * pct / 100.0)));
            }
            // A non-numeric percent string does not resolve upstream's
            // parseFloat path to a usable number; this port keeps the
            // historical screen-size fallback (reference/04 §9 rule 5)
            // and surfaces it like any other unknown form.
            if (sink != nullptr) {
                sink->recordDimensionFallback(s, screenSize);
            }
        } else {
            // Unrecognized string (e.g. a bare "zoom") falls through to
            // the reference's own fallback (reference/04-resources-pipeline.md
            // §9 rule 5: "fallback -> screenSize") — recorded as a
            // structured diagnostic since dd4606ea.
            if (sink != nullptr) {
                sink->recordDimensionFallback(s, screenSize);
            }
            return screenSize;
        }
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
                ? jsNumber(mergedUniforms.value(paramKey))
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
            // reference pipeline.js (upstream 00fb941c): func wrappers and
            // other nonnumeric authored values are not evaluated by the
            // runtime -- fall back to the texture spec's existing numeric
            // default (or the param default with its transforms reapplied)
            // so no backend ever sees a NaN size.
            if (!std::isfinite(value)) {
                if (obj.contains(QStringLiteral("default"))) {
                    value = obj.value(QStringLiteral("default")).toDouble();
                } else {
                    value = obj.contains(QStringLiteral("paramDefault"))
                        ? obj.value(QStringLiteral("paramDefault")).toDouble()
                        : 64.0;
                    if (hasMultiply) {
                        value *= obj.value(QStringLiteral("multiply")).toDouble();
                    }
                    if (hasPower) {
                        value = std::pow(value, obj.value(QStringLiteral("power")).toDouble());
                    }
                }
            }
            // Defensive (beyond the reference, same rationale as the
            // screenDivide safeDivisor below): a still-non-finite value here
            // (an authored nonnumeric `default`) cannot reproduce the
            // reference's NaN propagation without undefined behavior in the
            // double->int cast, so it clamps to the Math.max(1, ...) floor.
            value = std::isfinite(value) ? value : 1.0;
            return std::max(1, static_cast<int>(std::floor(value)));
        }

        if (obj.contains(QStringLiteral("screenDivide"))) {
            // reference pipeline.js:1244-1247:
            // `divisor = uniforms[spec.screenDivide] ?? spec.default ?? 1`.
            const QString divideKey = obj.value(QStringLiteral("screenDivide")).toString();
            const bool divideKeyPresent = jsonHasLiveValue(mergedUniforms, divideKey);
            double divisor = divideKeyPresent
                ? jsNumber(mergedUniforms.value(divideKey))
                : (obj.contains(QStringLiteral("default")) ? obj.value(QStringLiteral("default")).toDouble() : 1.0);
            // reference pipeline.js (upstream 00fb941c): a nonnumeric uniform
            // value (function wrapper, malformed string) falls back to the
            // spec default -- or 1 when there is none -- rather than dividing
            // by NaN.
            divisor = std::isfinite(divisor) ? divisor
                : (obj.contains(QStringLiteral("default")) ? obj.value(QStringLiteral("default")).toDouble() : 1.0);
            // `safeDivisor` is a defensive addition beyond the reference:
            // JS division by 0 yields Infinity (safely Math.round/Math.max'd
            // to Infinity), but casting an infinite double to `int` in C++
            // is undefined behavior, so a divisor of exactly 0 is treated
            // as 1 instead of reproducing that UB (and a still-non-finite
            // divisor after the fallback above -- an authored nonnumeric
            // default -- gets the same treatment; the reference's own
            // Math.max(1, Math.round(...)) would propagate NaN there).
            const double safeDivisor = (std::isfinite(divisor) && divisor != 0.0) ? divisor : 1.0;
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

        // Unknown object forms (none of param/screenDivide/scale) keep
        // the historical screen-size fallback but surface a structured
        // diagnostic instead of pure silence.
        if (sink != nullptr) {
            sink->recordDimensionFallback(dimensionFallbackKey(spec), screenSize);
        }
        return screenSize;
    }

    // Unknown array/bool forms keep the historical screen-size fallback
    // (no new rejection of previously accepted input), but surface a
    // structured diagnostic instead of pure silence.
    if (sink != nullptr) {
        sink->recordDimensionFallback(dimensionFallbackKey(spec), screenSize);
    }
    return screenSize;
}

namespace {

// reference/05-webgl2-backend.md §6.1: format table, fallback for an
// unrecognized format string is rgba8 (distinct from the *default* used
// when no format is specified at all -- callers of createSurface() pass
// "rgba16f" for that case; see SurfaceCache::get()).
void resolveGlFormat(const QString& format, unsigned int* internalFormat, unsigned int* glFormat, unsigned int* glType,
                     DiagnosticSink* sink = nullptr) {
    *glFormat = GL_RGBA;
    if (format == QStringLiteral("rgba16f") || format == QStringLiteral("rgba16float")) {
        *internalFormat = GL_RGBA16F;
        *glType = GL_HALF_FLOAT;
    } else if (format == QStringLiteral("rgba32f") || format == QStringLiteral("rgba32float")) {
        *internalFormat = GL_RGBA32F;
        *glType = GL_FLOAT;
    } else if (format == QStringLiteral("rgba8") || format == QStringLiteral("rgba8unorm")) {
        *internalFormat = GL_RGBA8;
        *glType = GL_UNSIGNED_BYTE;
    } else {
        *internalFormat = GL_RGBA8;
        *glType = GL_UNSIGNED_BYTE;
        // Unknown formats keep the historical silent rgba8 fallback (no
        // new rejection of previously accepted input), but surface it as
        // a structured diagnostic (reference dd4606ea: "Unknown
        // texture format '<key>'; falling back to rgba8", deduplicated
        // per format string). A caller passing no format at all is the
        // default, not a fallback — SurfaceCache::resolveSpec only
        // forwards non-empty spec formats, so an empty string records
        // nothing here.
        if (sink != nullptr && !format.isEmpty()) {
            sink->recordFormatFallback(format);
        }
    }
}

} // namespace

SurfaceCache::SurfaceCache(QOpenGLFunctions_4_1_Core* gl) : m_gl(gl) {}

GpuSurface SurfaceCache::createSurface(int width, int height, const QString& format) {
    DiagnosticSink sink;
    sink.collector = &m_diagnostics;
    sink.seen = &m_warnedFormatFallbacks;
    unsigned int internalFormat = 0, glFormat = 0, glType = 0;
    resolveGlFormat(format, &internalFormat, &glFormat, &glType, &sink);

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
    surface.format = format;
    return surface;
}

SurfaceCache::ResolvedSpec SurfaceCache::resolveSpec(const Graph& graph, const QString& specTexId, QSize screenSize,
                                                     const QJsonObject& mergedUniforms) {
    ResolvedSpec resolved{screenSize.width(), screenSize.height(), QStringLiteral("rgba16f")};
    // An undeclared volume surface (vol0..vol7) keeps the reference's native 64x4096 atlas
    // (Pipeline.createSurfaces); write3d declares the written ones.
    const QString surfaceName = specTexId.startsWith(QStringLiteral("global_")) ? specTexId.mid(7) : QString();
    if (surfaceName.size() == 4 && surfaceName.startsWith(QStringLiteral("vol"))
        && surfaceName.at(3) >= QLatin1Char('0') && surfaceName.at(3) <= QLatin1Char('7')) {
        resolved.width = 64;
        resolved.height = 4096;
    }

    const auto specIt = graph.textures.find(specTexId);
    if (specIt != graph.textures.end()) {
        DiagnosticSink sink;
        sink.collector = &m_diagnostics;
        sink.seen = &m_warnedDimensionFallbacks;
        resolved.width = resolveDimension(specIt->width, screenSize.width(), mergedUniforms, &sink);
        resolved.height = resolveDimension(specIt->height, screenSize.height(), mergedUniforms, &sink);
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
        if (it->width == spec.width && it->height == spec.height && it->format == spec.format) {
            return it.value();
        }
        if (it->fbo != 0) {
            releaseDepthBuffer(it->fbo);
            m_gl->glDeleteFramebuffers(1, &it->fbo);
            it->fbo = 0;
        }
        if (it->texture != 0) {
            const unsigned int oldTex = it->texture;
            for (auto mrtIt = m_mrtFbos.begin(); mrtIt != m_mrtFbos.end(); ) {
                if (mrtIt.key().contains(QStringLiteral(":%1,").arg(oldTex))) {
                    unsigned int fbo = mrtIt.value();
                    releaseDepthBuffer(fbo);
                    m_gl->glDeleteFramebuffers(1, &fbo);
                    mrtIt = m_mrtFbos.erase(mrtIt);
                } else {
                    ++mrtIt;
                }
            }
            m_gl->glDeleteTextures(1, &it->texture);
            it->texture = 0;
        }
        it.value() = createSurface(spec.width, spec.height, spec.format);
        return it.value();
    }
    return *m_surfaces.insert(cacheKey, createSurface(spec.width, spec.height, spec.format));
}

GpuSurface& SurfaceCache::get(const Graph& graph, const QString& texId, QSize screenSize,
                               const QJsonObject& mergedUniforms) {
    return getOrCreate(texId, resolveSpec(graph, texId, screenSize, mergedUniforms));
}

GpuSurface& SurfaceCache::getAliased(const QString& physicalKey, const Graph& graph, const QString& specTexId,
                                      QSize screenSize, const QJsonObject& mergedUniforms) {
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

void SurfaceCache::ensureDepthBuffer(unsigned int fbo, int width, int height) {
    const auto existing = m_depthBuffers.find(fbo);
    if (existing != m_depthBuffers.end()) {
        if (existing->width != width || existing->height != height) {
            m_gl->glBindRenderbuffer(GL_RENDERBUFFER, existing->renderbuffer);
            m_gl->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
            m_gl->glBindRenderbuffer(GL_RENDERBUFFER, 0);
            existing->width = width;
            existing->height = height;
        }
        return;
    }

    unsigned int renderbuffer = 0;
    m_gl->glGenRenderbuffers(1, &renderbuffer);
    m_gl->glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    m_gl->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    m_gl->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, renderbuffer);
    const GLenum status = m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_gl->glBindRenderbuffer(GL_RENDERBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        m_gl->glDeleteRenderbuffers(1, &renderbuffer);
        throw std::runtime_error(
            QStringLiteral("nm::SurfaceCache: framebuffer incomplete (status 0x%1) after adding a depth buffer")
                .arg(static_cast<uint>(status), 0, 16)
                .toStdString());
    }
    m_depthBuffers.insert(fbo, DepthBuffer{renderbuffer, width, height});
}

void SurfaceCache::releaseDepthBuffer(unsigned int fbo) {
    const auto it = m_depthBuffers.find(fbo);
    if (it == m_depthBuffers.end()) return;
    m_gl->glDeleteRenderbuffers(1, &it->renderbuffer);
    m_depthBuffers.erase(it);
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
    for (auto it = m_depthBuffers.begin(); it != m_depthBuffers.end(); ++it) {
        m_gl->glDeleteRenderbuffers(1, &it->renderbuffer);
    }
    m_depthBuffers.clear();
    if (m_defaultTexture != 0) {
        m_gl->glDeleteTextures(1, &m_defaultTexture);
        m_defaultTexture = 0;
    }
}

} // namespace nm
