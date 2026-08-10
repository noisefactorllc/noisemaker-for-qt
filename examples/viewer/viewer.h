#pragma once

// viewer.h — the example's QOpenGLWidget: a minimal live embedding of
// nm::Backend, driving the WIDGET'S OWN QOpenGLContext (see
// initializeGL()) rather than an offscreen-owned one (that's nm-render's
// job — qt/tools/nm-render/main.cpp always passes nullptr to
// Backend::setup()). The DSL source is a compile-time constant
// (kHeroSource, viewer.cpp); this exercises the LIVE COMPILER path end to
// end — nm::compileGraph() -> nm::Backend — with no pre-exported graph
// JSON involved (ARCHITECTURE.md "Integration surface": "examples/viewer:
// a minimal QOpenGLWidget live view (DSL in, animated render out)").

#include "compiler/dsl_compiler.h"
#include "compiler/effect_registry.h"
#include "runtime/backend.h"
#include "runtime/graph.h"

#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QTimer>

#include <memory>

class Viewer : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit Viewer(QWidget* parent = nullptr);

    // Highest GL error code observed since initializeGL() (0 / GL_NO_ERROR
    // if none). Polled explicitly after each render() / readSurface() call
    // rather than assumed absent — see paintGL() / checkGlErrors().
    unsigned int lastGlError() const { return m_lastGlError; }

    // True once initializeGL() has successfully compiled the graph and set
    // up the backend (false if that failed — see initializeGL()'s
    // try/catch — in which case paintGL() intentionally no-ops rather than
    // crash on a null graph).
    bool isReady() const { return m_ready; }

protected:
    void initializeGL() override;
    void paintGL() override;

private:
    void checkGlErrors(const char* where);

    nm::EffectRegistry m_registry;
    nm::Graph m_graph;

    // Owned via unique_ptr (not a value member) so initializeGL() can
    // destroy-and-recreate it wholesale on every call, including re-entry
    // (Qt re-invokes initializeGL() on reparent / screen-or-GPU change;
    // nm::Backend::setup() is NOT reentrant-safe on an already-set-up
    // instance -- see initializeGL()'s comment). nm::Backend is also
    // neither copyable nor movable (backend.h deletes the copy ops and its
    // user-declared destructor suppresses the implicit move ops), so a
    // fresh heap instance is the only way to get a clean one without
    // touching nm::Backend itself (runtime/, not this task's to edit).
    std::unique_ptr<nm::Backend> m_backend;
    bool m_ready = false;
    int m_initializeCount = 0; // >0 on the 2nd+ call to initializeGL() -- re-entry detection only

    QElapsedTimer m_clock;   // wall clock -> normalized loop time (paintGL)
    QTimer m_timer;          // ~60fps repaint driver
    unsigned int m_lastGlError = 0;
};
