#include "backend.h"

#include "shader_assembly.h"

#include <QFile>
#include <QJsonArray>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_1_Core>
#include <QSurfaceFormat>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace nm {

namespace {

// Hardcoded builtin programs -- these are NOT part of the byte-identical
// shader corpus (PORTING-GUIDE.md rule 1 governs `qt/noisemaker/shaders/
// effects/**` only). They mirror the reference's own runtime-owned
// constants: DEFAULT_VERTEX_SHADER (shaders/src/runtime/default-shaders.js)
// and the blit program's fragment source, which the reference's expander
// injects directly (shaders/src/runtime/expander.js `ensureBlitProgram`)
// rather than resolving from a file -- docs/GRAPH-JSON-SCHEMA.md: "the
// backend does NOT read shader source from [graph.programs]". Both are run
// through the same assembleShader() as every other program.
const char* const kDefaultVertexSource =
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 a_position;\n"
    "out vec2 v_texCoord;\n"
    "void main() {\n"
    "    v_texCoord = a_position * 0.5 + 0.5;\n"
    "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "}\n";

const char* const kBlitFragmentSource =
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 v_texCoord;\n"
    "uniform sampler2D src;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = texture(src, v_texCoord);\n"
    "}\n";

bool isBlit(const Pass& pass) {
    return pass.passType == QStringLiteral("blit");
}

QString serializeDefines(const QJsonObject& defines) {
    // Order doesn't affect the resulting compiled shader (see
    // shader_assembly.h); this just needs to be a stable string for a
    // given key/value set, which QJsonObject's (sorted) iteration already
    // guarantees.
    QStringList parts;
    for (auto it = defines.begin(); it != defines.end(); ++it) {
        parts << it.key() + QLatin1Char('=') + it.value().toVariant().toString();
    }
    return parts.join(QLatin1Char(','));
}

// Program cache key: (effectKey, progName, serialized defines) per the T3
// task brief Step 4, rather than reusing the reference's own node-prefixed
// `pass.program` id verbatim -- that would forgo cross-node sharing of
// identical effect+define combinations.
QString programCacheKey(const Pass& pass) {
    return pass.effectKey + QLatin1Char('|') + pass.progName + QLatin1Char('|') + serializeDefines(pass.defines);
}

float jsonArrayComponent(const QJsonArray& arr, int index, float fallback) {
    if (index >= arr.size()) {
        return fallback;
    }
    const QJsonValue v = arr.at(index);
    return (v.isNull() || v.isUndefined()) ? fallback : static_cast<float>(v.toDouble());
}

} // namespace

Backend::Backend() = default;

Backend::~Backend() {
    if (m_ownedContext && m_gl) {
        if (m_ownedContext->makeCurrent(m_ownedSurface)) {
            if (m_surfaces) {
                m_surfaces->releaseAll();
            }
            for (auto it = m_programs.begin(); it != m_programs.end(); ++it) {
                m_gl->glDeleteProgram(it->handle);
            }
            if (m_fullscreenVao) {
                m_gl->glDeleteVertexArrays(1, &m_fullscreenVao);
            }
            if (m_fullscreenVbo) {
                m_gl->glDeleteBuffers(1, &m_fullscreenVbo);
            }
            m_ownedContext->doneCurrent();
        }
    }
    delete m_gl;
    delete m_ownedSurface;
    delete m_ownedContext;
}

void Backend::setup(QOpenGLContext* context, const QString& dataRoot, QSize size) {
    m_dataRoot = dataRoot;
    m_size = size;

    if (context) {
        m_context = context;
    } else {
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setProfile(QSurfaceFormat::CoreProfile);
        format.setVersion(4, 1);

        m_ownedContext = new QOpenGLContext();
        m_ownedContext->setFormat(format);
        if (!m_ownedContext->create()) {
            throw std::runtime_error("nm::Backend::setup: failed to create OpenGL 4.1 core context");
        }

        m_ownedSurface = new QOffscreenSurface();
        m_ownedSurface->setFormat(m_ownedContext->format());
        m_ownedSurface->create();
        if (!m_ownedSurface->isValid()) {
            throw std::runtime_error("nm::Backend::setup: failed to create QOffscreenSurface");
        }

        if (!m_ownedContext->makeCurrent(m_ownedSurface)) {
            throw std::runtime_error("nm::Backend::setup: QOpenGLContext::makeCurrent() failed");
        }
        m_context = m_ownedContext;
    }

    // Qt6 moved the versioned OpenGL function wrappers to the QtOpenGL
    // module and dropped QOpenGLContext::versionFunctions<T>() (a Qt5 API);
    // the current pattern is to own the wrapper directly and initialize it
    // against whichever context is current.
    m_gl = new QOpenGLFunctions_4_1_Core();
    if (!m_gl->initializeOpenGLFunctions()) {
        throw std::runtime_error("nm::Backend::setup: failed to resolve OpenGL 4.1 core functions");
    }

    GLint maxTextureUnits = m_maxTextureUnits;
    m_gl->glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxTextureUnits);
    m_maxTextureUnits = maxTextureUnits;

    m_surfaces = std::make_unique<SurfaceCache>(m_gl);
    createFullscreenVao();
}

void Backend::createFullscreenVao() {
    // FULLSCREEN_TRIANGLE_POSITIONS (default-shaders.js): a single
    // oversized triangle covering NDC, not a quad.
    static const GLfloat positions[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};

    m_gl->glGenBuffers(1, &m_fullscreenVbo);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_fullscreenVbo);
    m_gl->glBufferData(GL_ARRAY_BUFFER, sizeof(positions), positions, GL_STATIC_DRAW);

    m_gl->glGenVertexArrays(1, &m_fullscreenVao);
    m_gl->glBindVertexArray(m_fullscreenVao);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    m_gl->glBindVertexArray(0);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
}

QByteArray Backend::loadEffectSource(const Pass& pass) const {
    const QString path = QStringLiteral("%1/shaders/effects/%2/%3/%4.frag")
        .arg(m_dataRoot, pass.effectNamespace, pass.func, pass.progName);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error(
            QStringLiteral("nm::Backend: failed to open shader file '%1'").arg(path).toStdString());
    }
    return file.readAll();
}

const Backend::CompiledProgram& Backend::programFor(const Pass& pass) {
    const QString key = programCacheKey(pass);
    auto cached = m_programs.find(key);
    if (cached != m_programs.end()) {
        return cached.value();
    }

    const QByteArray fragRaw = isBlit(pass) ? QByteArray(kBlitFragmentSource) : loadEffectSource(pass);
    const QByteArray vertRaw(kDefaultVertexSource);

    const bool needsPolyfill = !isBlit(pass) && shaderNeedsPackHalfPolyfill(QString::fromUtf8(fragRaw));

    const QByteArray fragAssembled = assembleShader(QString::fromUtf8(fragRaw), pass.defines, needsPolyfill);
    const QByteArray vertAssembled = assembleShader(QString::fromUtf8(vertRaw), QJsonObject(), false);

    GLuint vertShader = m_gl->glCreateShader(GL_VERTEX_SHADER);
    {
        const char* src = vertAssembled.constData();
        const GLint len = vertAssembled.size();
        m_gl->glShaderSource(vertShader, 1, &src, &len);
        m_gl->glCompileShader(vertShader);
        GLint ok = GL_FALSE;
        m_gl->glGetShaderiv(vertShader, GL_COMPILE_STATUS, &ok);
        if (ok != GL_TRUE) {
            GLint logLen = 0;
            m_gl->glGetShaderiv(vertShader, GL_INFO_LOG_LENGTH, &logLen);
            QByteArray log(logLen, Qt::Uninitialized);
            m_gl->glGetShaderInfoLog(vertShader, logLen, nullptr, log.data());
            m_gl->glDeleteShader(vertShader);
            throw std::runtime_error(
                ("nm::Backend: vertex shader compile failed for '" + pass.program + "': " + log).toStdString());
        }
    }

    GLuint fragShader = m_gl->glCreateShader(GL_FRAGMENT_SHADER);
    {
        const char* src = fragAssembled.constData();
        const GLint len = fragAssembled.size();
        m_gl->glShaderSource(fragShader, 1, &src, &len);
        m_gl->glCompileShader(fragShader);
        GLint ok = GL_FALSE;
        m_gl->glGetShaderiv(fragShader, GL_COMPILE_STATUS, &ok);
        if (ok != GL_TRUE) {
            GLint logLen = 0;
            m_gl->glGetShaderiv(fragShader, GL_INFO_LOG_LENGTH, &logLen);
            QByteArray log(logLen, Qt::Uninitialized);
            m_gl->glGetShaderInfoLog(fragShader, logLen, nullptr, log.data());
            m_gl->glDeleteShader(vertShader);
            m_gl->glDeleteShader(fragShader);
            throw std::runtime_error(
                ("nm::Backend: fragment shader compile failed for '" + pass.program + "': " + log).toStdString());
        }
    }

    GLuint program = m_gl->glCreateProgram();
    m_gl->glAttachShader(program, vertShader);
    m_gl->glAttachShader(program, fragShader);
    m_gl->glBindAttribLocation(program, 0, "a_position");
    m_gl->glLinkProgram(program);

    GLint linked = GL_FALSE;
    m_gl->glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint logLen = 0;
        m_gl->glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
        QByteArray log(logLen, Qt::Uninitialized);
        m_gl->glGetProgramInfoLog(program, logLen, nullptr, log.data());
        m_gl->glDeleteShader(vertShader);
        m_gl->glDeleteShader(fragShader);
        m_gl->glDeleteProgram(program);
        throw std::runtime_error(
            ("nm::Backend: program link failed for '" + pass.program + "': " + log).toStdString());
    }

    m_gl->glDeleteShader(vertShader);
    m_gl->glDeleteShader(fragShader);

    CompiledProgram compiled;
    compiled.handle = program;

    GLint uniformCount = 0;
    m_gl->glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &uniformCount);
    for (GLint i = 0; i < uniformCount; ++i) {
        char nameBuf[256];
        GLsizei nameLen = 0;
        GLint size = 0;
        GLenum type = 0;
        m_gl->glGetActiveUniform(program, static_cast<GLuint>(i), sizeof(nameBuf), &nameLen, &size, &type, nameBuf);
        const QString name = QString::fromLatin1(nameBuf, nameLen);
        const GLint location = m_gl->glGetUniformLocation(program, nameBuf);

        compiled.uniformLocations.insert(name, location);
        compiled.uniformTypes.insert(name, type);

        // Reference extractUniforms(): array uniforms ("foo[0]") are also
        // registered without the "[0]" suffix.
        if (name.endsWith(QStringLiteral("[0]"))) {
            const QString baseName = name.left(name.size() - 3);
            compiled.uniformLocations.insert(baseName, location);
            compiled.uniformTypes.insert(baseName, type);
        }
    }

    return m_programs.insert(key, compiled).value();
}

QJsonObject Backend::engineUniforms() const {
    // reference/04 §10.1 updateGlobalUniforms (shaders/src/runtime/pipeline.js
    // ~L1385-1427): `aspect` and `aspectRatio` are BOTH set, to the SAME
    // value, every frame — `g.aspect = g.aspectRatio = aspectValue` (or
    // `= fullAspect` under tile-region export, which T3 does not implement;
    // `_tileOffset`/`_fullResolution` are always null in our scope, so the
    // two branches collapse to the same `width/height` formula here).
    // FIX ROUND 1: `aspect` was missing entirely. Nine fullscreen-pass
    // fragment shaders in this corpus declare `uniform float aspect;`
    // (distinct from `aspectRatio`; verified via `grep -rl "uniform float
    // aspect;" qt/noisemaker/shaders/effects/`: synth/mandala, synth/osc2d,
    // synth/pattern, synth/perlin, synth/polygon (shape.frag),
    // synth/sacredGeometry, filter/repeat, filter/scale, filter/scroll —
    // plus render/meshRender/render.vert, out of T3's fullscreen-pass
    // scope). Leaving it unbound means GL's uniform default (0.0) reaches
    // those shaders instead of the real aspect ratio: `st.x *= aspect`
    // silently zeroes a coordinate (mandala: stripe/garbage output),
    // `st.x /= aspect` divides by zero (repeat/scale/scroll: Inf/NaN into
    // the rgba16f intermediate, then undefined behavior on the final
    // float->uint8 readback cast). See task report "Fix round 1" for the
    // before/after verification.
    //
    // Full engine-uniform set: time, resolution, tileOffset, fullResolution,
    // aspect, aspectRatio, renderScale. (deltaTime/frame/audio/midi are
    // pipeline bookkeeping or external-input state not read by any
    // fullscreen-effect-pass shader in this corpus and remain out of T3
    // scope.)
    QJsonObject globals;
    globals.insert(QStringLiteral("time"), m_time);
    globals.insert(QStringLiteral("resolution"), QJsonArray{m_size.width(), m_size.height()});
    globals.insert(QStringLiteral("tileOffset"), QJsonArray{0, 0});
    globals.insert(QStringLiteral("fullResolution"), QJsonArray{m_size.width(), m_size.height()});
    const double aspect = m_size.height() != 0 ? double(m_size.width()) / double(m_size.height()) : 1.0;
    globals.insert(QStringLiteral("aspect"), aspect);
    globals.insert(QStringLiteral("aspectRatio"), aspect);
    globals.insert(QStringLiteral("renderScale"), 1.0);
    return globals;
}

void Backend::setUniformValue(int location, unsigned int glType, const QJsonValue& value) {
    switch (glType) {
    case GL_FLOAT: {
        if (value.isArray()) {
            const QJsonArray arr = value.toArray();
            QVector<GLfloat> buf;
            buf.reserve(arr.size());
            for (const QJsonValue& v : arr) {
                buf.append(static_cast<GLfloat>(v.toDouble()));
            }
            m_gl->glUniform1fv(location, buf.size(), buf.constData());
        } else {
            m_gl->glUniform1f(location, static_cast<GLfloat>(value.toDouble()));
        }
        break;
    }
    case GL_INT:
    case GL_BOOL: {
        const int iv = value.isBool() ? (value.toBool() ? 1 : 0) : static_cast<int>(value.toDouble());
        m_gl->glUniform1i(location, iv);
        break;
    }
    case GL_FLOAT_VEC2: {
        const QJsonArray arr = value.isArray() ? value.toArray() : QJsonArray{value, value};
        const GLfloat v[2] = {jsonArrayComponent(arr, 0, 0.0f), jsonArrayComponent(arr, 1, 0.0f)};
        m_gl->glUniform2fv(location, 1, v);
        break;
    }
    case GL_FLOAT_VEC3: {
        const QJsonArray arr = value.isArray() ? value.toArray() : QJsonArray{value, value, value};
        const GLfloat v[3] = {
            jsonArrayComponent(arr, 0, 0.0f), jsonArrayComponent(arr, 1, 0.0f), jsonArrayComponent(arr, 2, 0.0f)};
        m_gl->glUniform3fv(location, 1, v);
        break;
    }
    case GL_FLOAT_VEC4: {
        const QJsonArray arr = value.isArray() ? value.toArray() : QJsonArray{value, value, value, value};
        const GLfloat v[4] = {jsonArrayComponent(arr, 0, 0.0f), jsonArrayComponent(arr, 1, 0.0f),
                               jsonArrayComponent(arr, 2, 0.0f), jsonArrayComponent(arr, 3, 1.0f)};
        m_gl->glUniform4fv(location, 1, v);
        break;
    }
    case GL_FLOAT_MAT3: {
        const QJsonArray arr = value.toArray();
        GLfloat m[9];
        for (int i = 0; i < 9; ++i) {
            m[i] = jsonArrayComponent(arr, i, 0.0f);
        }
        m_gl->glUniformMatrix3fv(location, 1, GL_FALSE, m);
        break;
    }
    case GL_FLOAT_MAT4: {
        const QJsonArray arr = value.toArray();
        GLfloat m[16];
        for (int i = 0; i < 16; ++i) {
            m[i] = jsonArrayComponent(arr, i, 0.0f);
        }
        m_gl->glUniformMatrix4fv(location, 1, GL_FALSE, m);
        break;
    }
    default:
        // Sampler types are bound in bindTextures(); other types (INT_VECn,
        // UINT*, BOOL_VECn, MAT2, non-square mats) are silently dropped,
        // matching the reference _setUniform's switch (reference/05
        // §8.2: "Types NOT handled ... value is silently dropped").
        break;
    }
}

void Backend::bindUniforms(const CompiledProgram& program, const Pass& pass) {
    const QJsonObject globals = engineUniforms();

    // Pass uniforms first (DSL/effect defaults), then globals -- but skip
    // any global name already present in pass.uniforms. Pass uniforms win
    // (reference/05 §8.1 "Order & precedence").
    for (auto it = pass.uniforms.begin(); it != pass.uniforms.end(); ++it) {
        if (!program.uniformLocations.contains(it.key())) {
            continue;
        }
        if (it.value().isNull() || it.value().isUndefined()) {
            continue;
        }
        setUniformValue(program.uniformLocations.value(it.key()), program.uniformTypes.value(it.key()), it.value());
    }
    for (auto it = globals.begin(); it != globals.end(); ++it) {
        if (pass.uniforms.contains(it.key())) {
            continue;
        }
        if (!program.uniformLocations.contains(it.key())) {
            continue;
        }
        setUniformValue(program.uniformLocations.value(it.key()), program.uniformTypes.value(it.key()), it.value());
    }
}

void Backend::bindTextures(const Graph& graph, const CompiledProgram& program, const Pass& pass) {
    int unit = 0;
    // Iterated in QJsonObject's own (sorted-by-key) order rather than the
    // graph JSON's original textual/insertion order (see shader_assembly.h
    // and graph.h's notes on this Qt build's QJsonObject behavior). This is
    // deliberate, not an oversight: texture-unit *numbers* are an
    // implementation detail — each sampler uniform is told its own assigned
    // unit explicitly below (`glUniform1i(loc, unit)`), so any consistent
    // permutation of which samplerName gets unit 0 vs. unit 1 etc. produces
    // the same final bindings and identical rendered output.
    for (auto it = pass.inputs.begin(); it != pass.inputs.end(); ++it) {
        const QString samplerName = it.key();
        const QString texId = it.value().toString();

        if (unit >= m_maxTextureUnits) {
            // reference/05 §8.3 bindTextures / webgl2.js ~L1364-1370:
            // ERR_TOO_MANY_TEXTURES.
            throw std::runtime_error(QStringLiteral(
                "nm::Backend: pass '%1' binds more textures than the GL implementation "
                "supports (limit %2)")
                                          .arg(pass.id)
                                          .arg(m_maxTextureUnits)
                                          .toStdString());
        }

        GLuint handle = 0;
        if (!texId.isEmpty() && texId != QStringLiteral("none")) {
            handle = m_surfaces->get(graph, texId, m_size).texture;
        }
        if (handle == 0) {
            // Reference bindTextures(): a missing/uninitialized input
            // silently binds the 1x1 transparent-black default texture
            // rather than leaving the unit unbound or warning.
            handle = m_surfaces->defaultTextureHandle();
        }

        m_gl->glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        m_gl->glBindTexture(GL_TEXTURE_2D, handle);

        if (program.uniformLocations.contains(samplerName)) {
            m_gl->glUniform1i(program.uniformLocations.value(samplerName), unit);
        }
        ++unit;
    }
}

void Backend::executePass(const Graph& graph, const Pass& pass) {
    const CompiledProgram& program = programFor(pass);
    m_gl->glUseProgram(program.handle);

    // T3 scope is non-MRT: exactly one output, preferring the "color" key
    // when present else the first (only) value (reference executePass:
    // `outputs?.color || Object.values(outputs||{})[0]`).
    QString outputId;
    if (pass.outputs.contains(QStringLiteral("color"))) {
        outputId = pass.outputs.value(QStringLiteral("color")).toString();
    } else if (!pass.outputs.isEmpty()) {
        outputId = pass.outputs.begin().value().toString();
    }
    if (outputId.isEmpty()) {
        throw std::runtime_error(("nm::Backend: pass '" + pass.id + "' declares no output").toStdString());
    }

    GpuSurface& outSurface = m_surfaces->get(graph, outputId, m_size);

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, outSurface.fbo);
    m_gl->glViewport(0, 0, outSurface.width, outSurface.height);

    bindTextures(graph, program, pass);
    bindUniforms(program, pass);

    // PORTING-GUIDE.md GL state parity rules / reference/05 §18.9: default
    // blend is OFF. T3 does not implement additive-deposit passes (points),
    // so blending is always disabled here.
    m_gl->glDisable(GL_BLEND);

    m_gl->glBindVertexArray(m_fullscreenVao);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 3);
    m_gl->glBindVertexArray(0);

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_gl->glUseProgram(0);
}

void Backend::render(const Graph& graph, double t) {
    m_time = t;
    m_currentRenderSurface = graph.renderSurface;

    for (const Pass& pass : graph.passes) {
        executePass(graph, pass);
    }

    m_gl->glFlush();
}

QImage Backend::readSurface() const {
    if (m_currentRenderSurface.isEmpty()) {
        throw std::runtime_error("nm::Backend::readSurface: graph has no renderSurface");
    }
    const QString texId = QStringLiteral("global_") + m_currentRenderSurface;
    const GpuSurface* surface = m_surfaces->find(texId);
    if (!surface) {
        throw std::runtime_error(
            ("nm::Backend::readSurface: surface '" + texId + "' was never written").toStdString());
    }

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, surface->fbo);
    // PORTING-GUIDE.md GL state parity rules: PACK_ALIGNMENT 1 before
    // readback (row padding otherwise corrupts non-multiple-of-4 widths).
    m_gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);

    QVector<float> buffer(surface->width * surface->height * 4);
    m_gl->glReadPixels(0, 0, surface->width, surface->height, GL_RGBA, GL_FLOAT, buffer.data());
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);

    QImage image(surface->width, surface->height, QImage::Format_RGBA8888);
    for (int y = 0; y < surface->height; ++y) {
        // glReadPixels is bottom-up; flip to top-down once here, matching
        // the reference's own PNG capture exactly (PORTING-GUIDE.md: "a
        // second flip or a gamma pass shows up as instant whole-image FAIL").
        const int srcRow = surface->height - 1 - y;
        const float* src = buffer.constData() + static_cast<qsizetype>(srcRow) * surface->width * 4;
        uchar* dst = image.scanLine(y);
        for (int x = 0; x < surface->width; ++x) {
            for (int c = 0; c < 4; ++c) {
                double v = std::round(static_cast<double>(src[x * 4 + c]) * 255.0);
                v = std::clamp(v, 0.0, 255.0);
                dst[x * 4 + c] = static_cast<uchar>(v);
            }
        }
    }
    return image;
}

} // namespace nm
