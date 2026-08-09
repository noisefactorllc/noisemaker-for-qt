#pragma once

#include <QHash>
#include <QJsonValue>
#include <QMap>
#include <QSize>
#include <QString>

#include "graph.h"

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
int resolveDimension(const QJsonValue& spec, int screenSize, const QJsonObject& mergedUniforms = QJsonObject());

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

    // Deletes every GL texture/FBO this cache created. The owning context
    // must be current.
    void releaseAll();

private:
    struct ResolvedSpec {
        int width;
        int height;
        QString format;
    };

    ResolvedSpec resolveSpec(const Graph& graph, const QString& specTexId, QSize screenSize,
                              const QJsonObject& mergedUniforms) const;
    GpuSurface createSurface(int width, int height, const QString& format);
    GpuSurface& getOrCreate(const QString& cacheKey, const ResolvedSpec& spec);

    QOpenGLFunctions_4_1_Core* m_gl;
    QHash<QString, GpuSurface> m_surfaces;
    QHash<QString, unsigned int> m_mrtFbos;
    unsigned int m_defaultTexture = 0;
};

} // namespace nm
