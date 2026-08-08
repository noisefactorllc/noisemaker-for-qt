#pragma once

#include <QHash>
#include <QJsonValue>
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
// The `param` branch's live-uniform lookup (`uniforms[spec.param]`) is not
// wired to real pass uniforms in T3 — no Tier-1/smoke fixture exercises a
// `param`-based texture dimension, and doing so needs a
// collectDefaultUniforms()-equivalent this task does not build (see task
// report). This resolves as if the named uniform were always absent,
// matching the reference's own `?? paramDefault` fallback path exactly.
int resolveDimension(const QJsonValue& spec, int screenSize);

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
    GpuSurface& get(const Graph& graph, const QString& texId, QSize screenSize);

    bool contains(const QString& texId) const;
    const GpuSurface* find(const QString& texId) const;

    // 1x1 transparent-black texture used when a pass input resolves to no
    // texture — reference `defaultTexture`: a silent fallback, no warning
    // (e.g. reading a surface before anything has written to it).
    unsigned int defaultTextureHandle();

    // Deletes every GL texture/FBO this cache created. The owning context
    // must be current.
    void releaseAll();

private:
    GpuSurface createSurface(int width, int height, const QString& format);

    QOpenGLFunctions_4_1_Core* m_gl;
    QHash<QString, GpuSurface> m_surfaces;
    unsigned int m_defaultTexture = 0;
};

} // namespace nm
