#include "viewer.h"

#include <QDir>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>

#include <cmath>
#include <cstdio>

namespace {

// The hero program: parity/corpus/weird_mandala.dsl, copied here VERBATIM
// as a compile-time constant per the brief ("DSL source in a constant") —
// not read from parity/ at runtime, and parity/ is otherwise off-limits to
// this task. Picked over the other parity/corpus candidates after actually
// test-rendering each with a scratch-built nm-render (see task report for
// the full comparison table):
//   - agentsNoOklab/agentsSpawn/target/targetO0: agent/points chains
//     (pointsEmit/flow/pointsRender) — avoided; more GL-state surface
//     (GL_PROGRAM_POINT_SIZE etc.) than "the smallest honest
//     demonstration" needs, and parity/programs/agentsPoints is in T6's
//     own "chaos" skip set for the same family of effects.
//   - navTargetParams/rd_example: navierStokes/reactionDiffusion — these
//     are EXACTLY T6's own named "timed"/"chaos" skip-set fixture classes
//     (parity/sweep.sh tol_for() candidates), i.e. numerically the least
//     stable class in the corpus. Avoided for a continuously-running demo.
//   - passing_through.dsl: LOOKED like a great, safe synth+filter chain,
//     but actually FAILS on this backend — verified via the scratch
//     nm-render: `node_0_curl__OCTAVES_1__OUTPUT_MODE_4__RIDGES_false`:
//     "ERROR: 0:237: Condition must be of type bool" (a real GLSL
//     fragment-shader compile error). Out of this task's scope to fix
//     (qt/noisemaker/runtime + shaders are renderer-track-owned, and
//     shaders are byte-copies-never-ports per PORTING-GUIDE.md rule 1);
//     flagged in the task report as a genuine defect for the renderer
//     track / T6's sweep to triage.
//   - vertical_blobs.dsl: leads with `mnca(tex: read(o1), ...)` (a
//     cellular-automaton-style effect reading a not-yet-written surface,
//     relying on double-buffer settle semantics) — avoided for the same
//     "don't gamble a live, indefinitely-running loop on a
//     feedback-sensitive effect" reason as reactionDiffusion/navierStokes.
//   - lit_noise.dsl: a strong second choice (perlin -> tetraCosine ->
//     lighting; genuinely time-varying — measured max-abs-diff 249/255
//     between two sampled times — and visually rich, no agents/feedback).
// weird_mandala.dsl won: it renders cleanly (verified, exit 0, no
// stderr), is EXPLICITLY time-animated (`animation:`/`speed:` params, not
// an incidental side effect — measured max-abs-diff 203/255 between two
// sampled times), has no agents/points/feedback/external-input surface,
// and exercises the widest effect variety of any safe candidate: three
// chained `mandala` generators (synth), two `blendMode` composites
// (mixer), `tetraCosine` (classicNoisedeck), and `celShading` (filter) —
// all ordinary single-pass-per-effect 2D chains.
const char* const kHeroSource = R"DSL(search filter, synth, mixer, render, points

let osc2 = osc(type: oscKind.noise)
let osc1 = osc(type: oscKind.noise)
let osc3 = osc(type: oscKind.noise, min: 0.09, max: 0.46, speed: 3, seed: 5701)
let osc4 = osc(type: oscKind.noise, min: 0.39, max: 0.8, speed: 5)

mandala(
  scale: 20,
  rotation: -157.326,
  thickness: 0.762,
  smoothness: 0.074,
  symmetry: 15,
  bindu: true,
  layers: 4,
  layerSpacing: 2.1,
  twist: 13.65,
  shapeGrowth: 0.435,
  fgColor: #d4a70c,
  bgColor: #f8642b,
  animation: ripple,
  pulseDepth: 0.603
)
  .write(o0)

mandala(
  scale: 20,
  rotation: -140.991,
  thickness: 0.133,
  smoothness: 0.87,
  symmetry: 22,
  bindu: true,
  layers: 10,
  layerSpacing: 0.589,
  twist: 10.19,
  shapeGrowth: 0.556,
  fgColor: #5ad28d,
  bgColor: #c56d1c,
  animation: spiralWave,
  speed: -1,
  pulseDepth: 0.356
)
  .blendMode(
    tex: read(o0),
    mode: hardLight,
    mix: -0.069
  )
  .write(o1)

mandala(
  scale: 13.86,
  rotation: 68.927,
  thickness: 0.974,
  smoothness: 0.423,
  symmetry: 22,
  shape: triangle,
  layers: 12,
  layerSpacing: 2.44,
  twist: 2.37,
  shapeGrowth: 0.574,
  fgColor: #cb49c6,
  bgColor: #f51832,
  animation: ripple,
  speed: -1,
  pulseDepth: 0.857
)
  .blendMode(
    tex: read(o1),
    mode: hardLight,
    mix: 6.769
  )
  .tetraCosine(
    offsetR: 0.843,
    offsetG: 0.262,
    offsetB: 0.103,
    ampR: 0.79,
    ampG: 0.273,
    ampB: 0.533,
    freqR: 4,
    freqG: 3,
    freqB: 0,
    phaseR: 0.873,
    phaseG: 0.838,
    phaseB: 0.182,
    rotation: back,
    repeat: 5.38,
    alpha: 0.715
  )
  .celShading(
    levels: 2,
    gamma: 0.3,
    edgeWidth: 0,
    edgeThreshold: 0.51,
    edgeColor: #340001
  )
  .write(o2)

render(o2)
)DSL";

// One full animation loop, in seconds — a DEMO-ONLY choice (the DSL's own
// per-layer `speed`/`animation` params scale within this normalized [0,1)
// cycle; see ARCHITECTURE.md "Runtime model" Time row). Not a
// parity-graded constant.
constexpr double kLoopSeconds = 12.0;

// dataRoot resolution, in priority order:
//   1. NM_DATA_ROOT environment variable (explicit override).
//   2. NM_DATA_ROOT compile definition, baked by examples/viewer/
//      CMakeLists.txt to whichever tree actually supplied the library this
//      binary linked against — the dev-tree qt/noisemaker (add_subdirectory
//      flow) or the installed share/noisemaker-qt/noisemaker (find_package
//      flow).
//   3. nm::EffectRegistry::defaultDataRoot()'s own CWD-relative "qt/
//      noisemaker" convention, as a last resort (mirrors the sibling
//      resolveDataRoot() in qt/tools/nm-render/main.cpp, duplicated here
//      per that file's own established per-translation-unit convention
//      rather than shared, since main.cpp is frozen and its helper is
//      anonymous-namespace-local to it anyway).
QString resolveDataRoot() {
    auto looksValid = [](const QString& root) {
        QDir d(root);
        return d.exists(QStringLiteral("shaders")) && d.exists(QStringLiteral("effects"));
    };

    const QByteArray envRoot = qgetenv("NM_DATA_ROOT");
    if (!envRoot.isEmpty()) {
        const QString candidate = QDir(QString::fromLocal8Bit(envRoot)).absolutePath();
        if (looksValid(candidate)) return candidate;
    }

#ifdef NM_DATA_ROOT
    {
        const QString candidate = QDir(QStringLiteral(NM_DATA_ROOT)).absolutePath();
        if (looksValid(candidate)) return candidate;
    }
#endif

    return nm::EffectRegistry::defaultDataRoot();
}

} // namespace

Viewer::Viewer(QWidget* parent) : QOpenGLWidget(parent) {
    resize(512, 512); // resizable: resizeGL() calls nm::Backend::resize()
    m_timer.setInterval(16); // ~60fps
    connect(&m_timer, &QTimer::timeout, this, [this] { update(); });
}

Viewer::~Viewer() {
    // Fix round 3: this destructor's BODY runs before m_backend (a plain
    // member) is torn down and before ~QOpenGLWidget() runs -- ordinary
    // C++ derived-class destruction order (body, then members in reverse
    // declaration order, then base class). That makes this the ONLY place
    // that can release m_backend's GPU objects on the ORDINARY
    // widget-destruction path while its context is both still valid AND
    // still m_backend's own: cleanupGl() (below), wired to the context's
    // aboutToBeDestroyed(), fires from INSIDE ~QOpenGLWidget() -- by then,
    // without this destructor, m_backend would already have been
    // destroyed by member teardown, so cleanupGl() would see null and
    // no-op every time. Confirmed empirically (not just reasoned about):
    // a temporary probe showed cleanupGl() never actually entering its
    // release branch on ordinary shutdown before this fix existed.
    //
    // QOpenGLWidget::makeCurrent() is valid here -- the context is still
    // alive; only ~QOpenGLWidget() (which runs AFTER this body) tears it
    // down.
    makeCurrent();
    releaseGlObjects();
    doneCurrent();
}

// The backend renders at the widget's size in device pixels, so the blit
// in paintGL() is 1:1 on high-DPI screens.
QSize Viewer::pixelSize() const {
    const qreal ratio = devicePixelRatioF();
    return QSize(qMax(1, qRound(width() * ratio)), qMax(1, qRound(height() * ratio)));
}

// Frees this widget's GL objects and the Backend's. The context must be
// current.
void Viewer::releaseGlObjects() {
    if (m_readFbo != 0) {
        context()->functions()->glDeleteFramebuffers(1, &m_readFbo);
        m_readFbo = 0;
    }
    if (m_backend) {
        m_backend->releaseGl();
    }
}

void Viewer::checkGlErrors(const char* where) {
    QOpenGLContext* ctx = context();
    if (!ctx) return;
    QOpenGLFunctions* f = ctx->functions();
    for (;;) {
        const unsigned int err = f->glGetError();
        if (err == 0) break; // GL_NO_ERROR
        if (m_lastGlError == 0) m_lastGlError = err;
        std::fprintf(stderr, "viewer: GL error 0x%04x at %s\n", err, where);
    }
}

void Viewer::cleanupGl() {
    // Fix round 3: null check BEFORE the log line, not after -- the log
    // must never again claim a release that didn't happen. On the
    // ORDINARY widget-destruction path m_backend is already null here
    // (member teardown ran before ~QOpenGLWidget() fired this signal --
    // see ~Viewer(), which handles that path instead), so this correctly,
    // silently no-ops there; it only actually releases anything on the
    // mid-run context-recreation path (reparent / screen / GPU change),
    // where the widget survives and m_backend is still valid.
    if (!m_backend) return;
    std::fprintf(stderr,
                  "viewer: context aboutToBeDestroyed -- releasing this Backend's GL resources "
                  "before teardown\n");
    makeCurrent(); // the dying context is still valid here -- see initializeGL()'s comment
    releaseGlObjects();
    doneCurrent();
}

void Viewer::initializeGL() {
    // Re-entry guard (coordinator review finding): Qt re-invokes
    // initializeGL() whenever the widget's underlying context is recreated
    // -- reparenting into a window on a different screen/GPU, a display
    // reconfiguration, etc. -- not just once at startup.
    // nm::Backend::setup() is NOT reentrant-safe on an already-set-up
    // instance (qt/noisemaker/runtime/backend.cpp unconditionally
    // allocates a fresh QOpenGLFunctions_4_1_Core and re-derives GL state
    // against whatever context happens to be current, with no guard --
    // renderer-track-owned, not this task's to change). The fix that stays
    // within this example's own ownership: never call setup() twice on the
    // same Backend. m_backend is destroyed and reconstructed from scratch
    // on EVERY call to this function (first call included), so there is
    // never a second setup() call on one instance, by construction.
    m_ready = false; // defensively false for the duration of the rebuild below
    if (m_initializeCount > 0) {
        std::fprintf(stderr,
                      "viewer: initializeGL() invoked again (call #%d) -- Qt recreated this "
                      "widget's GL context (reparent / screen / GPU change); recreating "
                      "nm::Backend fresh rather than reusing the old instance\n",
                      m_initializeCount + 1);
    }
    ++m_initializeCount;

    // Fix round 2: the re-entry guard above stops setup() from ever being
    // called twice on one Backend, but it does NOT by itself free the
    // OUTGOING Backend's GPU objects -- nm::Backend::~Backend() only does
    // real GL cleanup for a context it OWNS (nm-render's offscreen mode);
    // for this widget's externally-owned context, the destructor is
    // intentionally no-GL (deleting someone else's context is not this
    // class's place), so without explicit action every re-entry leaked the
    // previous Backend's entire GPU resource set. The canonical Qt fix:
    // connect to THIS (about-to-become-outgoing) context's own
    // aboutToBeDestroyed() signal, which fires while the context is still
    // valid -- i.e. before Qt actually tears it down and before the next
    // initializeGL() call -- and release the (still-current) Backend's GL
    // objects right there. See cleanupGl().
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &Viewer::cleanupGl);

    const QString dataRoot = resolveDataRoot();
    if (!QDir(dataRoot).exists(QStringLiteral("effects")) || !QDir(dataRoot).exists(QStringLiteral("shaders"))) {
        std::fprintf(stderr,
                      "viewer: dataRoot '%s' has no effects/ and shaders/ subdirectories -- "
                      "set NM_DATA_ROOT to override (see resolveDataRoot() in viewer.cpp)\n",
                      dataRoot.toUtf8().constData());
    }

    try {
        m_registry.loadAll(dataRoot);
        m_graph = nm::compileGraph(QString::fromUtf8(kHeroSource), m_registry);

        // A brand-new instance every time (see the re-entry-guard comment
        // above) -- constructing it here, still before the old one (if
        // any) is destroyed by this assignment, is safe: Backend's
        // constructor touches no GL state, only setup() does.
        m_backend = std::make_unique<nm::Backend>();

        // Drive the WIDGET's own already-current context — Qt makes it
        // current before calling initializeGL() — not an offscreen-owned
        // one; that's nm::Backend's OTHER supported mode (the nullptr path
        // nm-render's main.cpp uses). This is the brief's literal ask:
        // "QOpenGLWidget subclass driving nm::Backend with the widget's
        // context."
        m_backend->setup(context(), dataRoot, pixelSize());
        // A live host: asyncInit overlays (fibers, scratches, strayHair)
        // trace on a worker thread, so a resize never stalls the window.
        // The timer renders every frame, which uploads a completed trace.
        m_backend->setOverlayTraceMode(nm::OverlayTraceMode::Background);
        m_renderSize = pixelSize();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "viewer: initialization failed: %s\n", e.what());
        return; // m_ready stays false; paintGL() below no-ops rather than
                // dereference a graph that never compiled.
    }
    checkGlErrors("initializeGL");

    m_ready = true;
    m_framesRendered = 0;
    m_clock.start();
    m_timer.start();
}

void Viewer::resizeGL(int, int) {
    // nm::Backend::resize() (reference Pipeline.resize) recreates every
    // surface at the new size; feedback state starts over, as in the
    // reference.
    if (!m_ready || pixelSize() == m_renderSize) return;
    m_backend->resize(pixelSize());
    m_renderSize = pixelSize();
    checkGlErrors("resize");
}

void Viewer::paintGL() {
    if (!m_ready) return;

    const double t = std::fmod(m_clock.elapsed() / 1000.0 / kLoopSeconds, 1.0);
    m_backend->render(m_graph, t);
    checkGlErrors("render");

    // Present on the GPU: nm::Backend renders into its own textures, and
    // renderSurfaceTexture() names the one render(o2) presents. Attach it
    // to a read framebuffer and blit it into this widget's framebuffer.
    // Both are bottom-up GL images, so no flip is needed.
    const nm::Backend::SurfaceTexture surface = m_backend->renderSurfaceTexture();
    QOpenGLExtraFunctions* f = context()->extraFunctions();
    if (m_readFbo == 0) f->glGenFramebuffers(1, &m_readFbo);
    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_readFbo);
    f->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, surface.texture, 0);
    f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
    f->glBlitFramebuffer(0, 0, surface.size.width(), surface.size.height(), 0, 0, m_renderSize.width(),
                         m_renderSize.height(), GL_COLOR_BUFFER_BIT, GL_LINEAR);
    f->glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    checkGlErrors("present");
    ++m_framesRendered;
}
