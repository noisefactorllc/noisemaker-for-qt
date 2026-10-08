#pragma once

#include <QHash>
#include <QJsonValue>
#include <QMap>
#include <QSize>
#include <QString>

#include "graph.h"
#include "diagnostics.h"

class QOpenGLFunctions_4_1_Core;

namespace nm {

// One GPU-resident render target: a 2D RGBA texture plus its single-color-
// attachment FBO. Handles are kept untyped (GLuint is `unsigned int`) so
// this header does not need to pull in GL headers. T3 scope is non-MRT,
// single-buffered (see the T5 task brief for ping-pong / global-surface
// read-write semantics, which extends this file).
struct GpuSurface {
    unsigned int texture = 0;
    unsigned int fbo = 0;
    int width = 0;
    int height = 0;
    QString format;
};

// Resolves a TextureSpec dimension value against a screen size. Exact
// rounding rules from reference/04-resources-pipeline.md §9
// `resolveDimension`: floor for number/percent/param/scale, round for
// screenDivide, always max(1, ...). `??` in the reference is nullish
// (missing/null only, 0 is a valid override).
//
// `mergedUniforms`: the live-uniform context for the `param`/`screenDivide`
// branches (reference `uniforms[spec.param]` / `uniforms[spec.screenDivide]`
// -- pipeline.js:1211/1244-1247), consulted with the reference's exact
// nullish-coalescing fallback chain: present-and-non-null wins, otherwise
// fall through to `paramDefault`/`default`. Callers pass the graph-wide
// merge of every pass's uniforms (see pingpong.h mergeAllPassUniforms),
// matching the reference's own collectDefaultUniforms() input. Defaults to
// an empty object so call sites that don't have (or don't need) this
// context still compile and behave as if the named uniform were absent.
//
// `sink` (optional): when non-null, an unknown dimension form (an
// unrecognized string, a bool, or an object without a
// param/screenDivide/scale key) keeps the historical screen-size fallback
// but additionally records a deduplicated ERR_DIMENSION_FALLBACK
// diagnostic (upstream dd4606ea — "unknown dimension
// forms keep the historical fallback ... surface it as a structured
// diagnostic instead of pure silence"; upstream a0e9bbff — the
// validator-accepted 'input'/'resolution' keywords are recognized forms,
// resolved like "screen"/"auto" with no diagnostic; an absent/null spec
// is a default, not an unknown form, and records nothing).
int resolveDimension(const QJsonValue& spec, int screenSize, const QJsonObject& mergedUniforms = QJsonObject(),
                     DiagnosticSink* sink = nullptr);

// Owns the live texId -> GpuSurface registry for one Backend. Creates
// textures lazily on first reference, sized/formatted per
// `graph.textures[texId]` when present, defaulting to screen-sized rgba16f
// for `global_*` surfaces that have no explicit spec (reference/04 §8
// "Display surfaces"). Filtering/wrap are always NEAREST/CLAMP_TO_EDGE
// (PORTING-GUIDE.md "GL state parity rules"); texture creation clears the
// surface to transparent black once (reference createTexture), and never
// again per pass (PORTING-GUIDE: targets otherwise carry prior contents).
class SurfaceCache {
public:
    explicit SurfaceCache(QOpenGLFunctions_4_1_Core* gl);

    // Returns the surface for `texId`, creating it on first use against
    // `graph`'s TextureSpec (if any) and `screenSize` as the fallback.
    // `mergedUniforms`: see resolveDimension() -- the live-uniform context
    // for param/screenDivide-based specs.
    GpuSurface& get(const Graph& graph, const QString& texId, QSize screenSize,
                     const QJsonObject& mergedUniforms = QJsonObject());

    // Ping-pong variant of get(): `physicalKey` is the actual cache/GL-
    // resource key (e.g. "global_xyz#a"), while `specTexId` (the BARE
    // graph texId, e.g. "global_xyz") is what TextureSpec dimensions are
    // resolved against -- the two physical buffers of a hazard surface are
    // always identically sized/formatted, so both share one spec lookup.
    GpuSurface& getAliased(const QString& physicalKey, const Graph& graph, const QString& specTexId,
                            QSize screenSize, const QJsonObject& mergedUniforms = QJsonObject());

    bool contains(const QString& texId) const;
    const GpuSurface* find(const QString& texId) const;

    // 1x1 transparent-black texture used when a pass input resolves to no
    // texture — reference `defaultTexture`: a silent fallback, no warning
    // (e.g. reading a surface before anything has written to it).
    unsigned int defaultTextureHandle();

    // Returns a cached FBO with each (location, textureHandle) pair in
    // `attachments` bound at that GL_COLOR_ATTACHMENT0+location, with
    // glDrawBuffers already configured — reference `createMRTFBO` / godot's
    // per-pass MRT framebuffer_create. Cached by the exact attachment set
    // (a hazard MRT pass's two alternating ping-pong states produce exactly
    // two distinct FBOs, reused thereafter — mirrors the reference's own
    // per-mrt-id `this.fbos` cache). Throws on an incomplete framebuffer.
    unsigned int mrtFramebuffer(const QMap<int, unsigned int>& attachments);

    // reference webgl2.js ensureDepthBuffer (triangle mesh passes): gives
    // `fbo` a DEPTH_COMPONENT24 renderbuffer of width x height, created once
    // and resized when the size changes. Binds `fbo` while attaching and
    // leaves GL_FRAMEBUFFER bound to 0, so call it before binding the draw
    // target. The renderbuffer is deleted with its FBO.
    void ensureDepthBuffer(unsigned int fbo, int width, int height);

    // Deletes every GL texture/FBO this cache created. The owning context
    // must be current.
    void releaseAll();

    // Structured diagnostics recorded (rather than thrown) by this cache:
    // deduplicated ERR_DIMENSION_FALLBACK / ERR_UNKNOWN_FORMAT_FALLBACK
    // records for the historically-silent unknown-dimension and
    // unknown-format fallbacks, as the reference does. The collector is
    // capped at 64 records like the reference DiagnosticCollector.
    const DiagnosticCollector& diagnostics() const { return m_diagnostics; }
    void clearDiagnostics() {
        m_diagnostics.clear();
        m_warnedDimensionFallbacks.clear();
        m_warnedFormatFallbacks.clear();
    }

private:
    struct ResolvedSpec {
        int width;
        int height;
        QString format;
    };

    ResolvedSpec resolveSpec(const Graph& graph, const QString& specTexId, QSize screenSize,
                              const QJsonObject& mergedUniforms);
    GpuSurface createSurface(int width, int height, const QString& format);
    GpuSurface& getOrCreate(const QString& cacheKey, const ResolvedSpec& spec);

    QOpenGLFunctions_4_1_Core* m_gl;
    QHash<QString, GpuSurface> m_surfaces;
    QHash<QString, unsigned int> m_mrtFbos;
    DiagnosticCollector m_diagnostics;
    QSet<QString> m_warnedDimensionFallbacks;
    QSet<QString> m_warnedFormatFallbacks;
    struct DepthBuffer {
        unsigned int renderbuffer = 0;
        int width = 0;
        int height = 0;
    };
    QHash<unsigned int, DepthBuffer> m_depthBuffers; // fbo -> depth attachment
    void releaseDepthBuffer(unsigned int fbo);
    unsigned int m_defaultTexture = 0;
};

} // namespace nm
