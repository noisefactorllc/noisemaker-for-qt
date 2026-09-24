#include "async_overlay.h"

#include "backend.h"
#include "graph.h"
#include "stroke_canvas.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace nm {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// ECMAScript StringToNumber for decimal strings and Infinity. Other strings
// are NaN here; JavaScript also reads 0x, 0o and 0b integer strings, which
// the DSL does not produce for seed or density.
double stringToNumber(const QString& text) {
    static const QRegularExpression decimal(
        QStringLiteral("^[+-]?(\\d+\\.?\\d*|\\.\\d+)([eE][+-]?\\d+)?$"));
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return 0.0;
    if (trimmed == QStringLiteral("Infinity") || trimmed == QStringLiteral("+Infinity")) {
        return std::numeric_limits<double>::infinity();
    }
    if (trimmed == QStringLiteral("-Infinity")) return -std::numeric_limits<double>::infinity();
    if (!decimal.match(trimmed).hasMatch()) return kNaN;
    return trimmed.toDouble();
}

// ECMAScript ToNumber for a JSON value (arrays through Array.prototype.join).
double jsToNumber(const QJsonValue& value) {
    switch (value.type()) {
    case QJsonValue::Null: return 0.0;
    case QJsonValue::Bool: return value.toBool() ? 1.0 : 0.0;
    case QJsonValue::Double: return value.toDouble();
    case QJsonValue::String: return stringToNumber(value.toString());
    case QJsonValue::Array: {
        const QJsonArray array = value.toArray();
        if (array.isEmpty()) return 0.0;
        if (array.size() > 1) return kNaN; // "a,b" is not a number
        const QJsonValue element = array.at(0);
        if (element.isNull() || element.isUndefined()) return 0.0; // join() writes ""
        if (element.isBool()) return kNaN;                           // "true" / "false"
        return jsToNumber(element);
    }
    default: return kNaN; // an object ("[object Object]") or undefined
    }
}

bool jsTruthy(const QJsonValue& value) {
    switch (value.type()) {
    case QJsonValue::Bool: return value.toBool();
    case QJsonValue::Double: {
        const double d = value.toDouble();
        return d != 0.0 && !std::isnan(d);
    }
    case QJsonValue::String: return !value.toString().isEmpty();
    case QJsonValue::Array:
    case QJsonValue::Object: return true;
    default: return false; // null or undefined
    }
}

// `params.seed || 1`
double seedParam(const QJsonObject& params) {
    const QJsonValue seed = params.value(QStringLiteral("seed"));
    return jsTruthy(seed) ? jsToNumber(seed) : 1.0;
}

// `params.density !== undefined ? params.density : fallback`
double densityParam(const QJsonObject& params, double fallback) {
    const QJsonValue density = params.value(QStringLiteral("density"));
    return density.isUndefined() ? fallback : jsToNumber(density);
}

double jsMax(double a, double b) {
    if (std::isnan(a) || std::isnan(b)) return kNaN;
    return std::max(a, b);
}

bool isScalar(const QJsonValue& value) {
    return value.isDouble() || value.isString() || value.isBool();
}

const QString kOverlayTex = QStringLiteral("overlayTex");

// shaders/effects/filter/fibers/definition.js asyncInit.
TraceResult fibers(StrokeCanvas& canvas, const QJsonObject& params,
                   const std::function<bool()>& isCancelled, const std::function<void()>& progress) {
    const double width = static_cast<double>(canvas.width());
    const double seed = seedParam(params);
    const double density = densityParam(params, 0.5);
    // Python reference: 4 layers of chaotic worms; density scales the count.
    const double baseDensity = 0.5 + density * 2.0;
    for (int layer = 0; layer < 4; ++layer) {
        if (isCancelled && isCancelled()) return TraceResult::Cancelled;
        const double layerSeed = seed * 1000.0 + static_cast<double>(layer) * 137.0;
        WormTraceOptions opts;
        opts.width = canvas.width();
        opts.height = canvas.height();
        opts.seed = layerSeed;
        opts.density = baseDensity;
        opts.kink = 5.0 + std::fmod(layerSeed, 5.0);
        opts.stride = 0.75;
        opts.strideDeviation = 0.125;
        opts.duration = 1.0;
        opts.behavior = WormBehavior::Chaotic;
        opts.flowFreq = 4.0;
        opts.lineWidth = jsMax(1.5, width / 384.0);
        opts.colorFn = [](SeededRng& rng, int) {
            WormColor color;
            color.r = std::floor(rng.nextFloat() * 200.0 + 55.0);
            color.g = std::floor(rng.nextFloat() * 200.0 + 55.0);
            color.b = std::floor(rng.nextFloat() * 200.0 + 55.0);
            color.a = 0.5;
            return color;
        };
        const TraceResult result = traceWorms(canvas, opts, isCancelled, progress);
        if (result != TraceResult::Completed) return result;
    }
    return TraceResult::Completed;
}

// shaders/effects/filter/scratches/definition.js asyncInit.
TraceResult scratches(StrokeCanvas& canvas, const QJsonObject& params,
                      const std::function<bool()>& isCancelled, const std::function<void()>& progress) {
    const double width = static_cast<double>(canvas.width());
    const double seed = seedParam(params);
    const double density = densityParam(params, 0.3);
    // Python reference: 4 layers alternating obedient/unruly, low kink.
    for (int layer = 0; layer < 4; ++layer) {
        if (isCancelled && isCancelled()) return TraceResult::Cancelled;
        const double layerSeed = seed * 1000.0 + static_cast<double>(layer) * 251.0;
        const bool isObedient = std::fmod(layerSeed, 2.0) == 0.0;
        WormTraceOptions opts;
        opts.width = canvas.width();
        opts.height = canvas.height();
        opts.seed = layerSeed;
        opts.density = 0.1 + density * 0.4;
        opts.kink = 0.125 + std::fmod(layerSeed, 50.0) / 400.0;
        opts.stride = 0.75;
        opts.strideDeviation = 0.5;
        opts.duration = 2.0 + std::fmod(layerSeed, 3.0);
        opts.behavior = isObedient ? WormBehavior::Obedient : WormBehavior::Unruly;
        opts.flowFreq = 2.0 + std::fmod(layerSeed, 3.0);
        opts.lineWidth = jsMax(0.5, width / 1024.0);
        opts.colorFn = [](SeededRng&, int) {
            WormColor color;
            color.r = 255.0;
            color.g = 255.0;
            color.b = 255.0;
            color.a = 1.0;
            return color;
        };
        const TraceResult result = traceWorms(canvas, opts, isCancelled, progress);
        if (result != TraceResult::Completed) return result;
    }
    return TraceResult::Completed;
}

// shaders/effects/filter/strayHair/definition.js asyncInit.
TraceResult strayHair(StrokeCanvas& canvas, const QJsonObject& params,
                      const std::function<bool()>& isCancelled, const std::function<void()>& progress) {
    const double width = static_cast<double>(canvas.width());
    const double seed = seedParam(params);
    const double density = densityParam(params, 0.5);
    // Python reference: one layer of sparse unruly worms, dark strands.
    const double layerSeed = seed * 1000.0 + 42.0;
    WormTraceOptions opts;
    opts.width = canvas.width();
    opts.height = canvas.height();
    opts.seed = layerSeed;
    opts.density = 0.001 + density * 0.004;
    opts.kink = 5.0 + std::fmod(layerSeed, 45.0);
    opts.stride = 0.5;
    opts.strideDeviation = 0.25;
    opts.duration = 8.0 + std::fmod(layerSeed, 8.0);
    opts.behavior = WormBehavior::Unruly;
    opts.flowFreq = 4.0;
    opts.lineWidth = jsMax(1.0, width / 400.0);
    opts.colorFn = [](SeededRng& rng, int) {
        WormColor color;
        color.r = std::floor(rng.nextFloat() * 30.0);
        color.g = std::floor(rng.nextFloat() * 30.0);
        color.b = std::floor(rng.nextFloat() * 30.0);
        color.a = 0.666;
        return color;
    };
    return traceWorms(canvas, opts, isCancelled, progress);
}

using AsyncInitFn = TraceResult (*)(StrokeCanvas&, const QJsonObject&, const std::function<bool()>&,
                                    const std::function<void()>&);

AsyncInitFn asyncInitFor(const QString& effectKey) {
    if (effectKey == QStringLiteral("filter.fibers")) return &fibers;
    if (effectKey == QStringLiteral("filter.scratches")) return &scratches;
    if (effectKey == QStringLiteral("filter.strayHair")) return &strayHair;
    return nullptr;
}

} // namespace

bool hasAsyncInit(const QString& effectKey) {
    return asyncInitFor(effectKey) != nullptr;
}

QStringList asyncInitTextures(const QString& effectKey) {
    if (!hasAsyncInit(effectKey)) return {};
    return {kOverlayTex};
}

QJsonObject asyncInitParams(const QString& effectKey, const QJsonObject& uniforms,
                            const QJsonObject& globalUniforms) {
    if (!hasAsyncInit(effectKey)) return {};
    // reference checkAsyncRegen: only a scalar value of a non-alpha global
    // triggers the re-trace with the step values. seed and density are the
    // non-alpha globals of all three effects, and the only params they read.
    const QStringList read = {QStringLiteral("seed"), QStringLiteral("density")};
    bool stepValues = false;
    for (const QString& name : read) {
        if (isScalar(uniforms.value(name))) stepValues = true;
    }
    const QJsonObject& source = stepValues ? uniforms : globalUniforms;
    QJsonObject params;
    for (const QString& name : read) {
        const auto it = source.constFind(name);
        if (it != source.constEnd()) params.insert(name, it.value());
    }
    return params;
}

TraceResult runAsyncInit(const QString& effectKey, StrokeCanvas& canvas, const QJsonObject& params,
                         const std::function<bool()>& isCancelled,
                         const std::function<void(const QString&)>& onUpdate) {
    const AsyncInitFn fn = asyncInitFor(effectKey);
    if (!fn) return TraceResult::Failed;
    // The reference creates the canvas, clears it and uploads it once
    // before tracing.
    canvas.clear();
    if (onUpdate) onUpdate(kOverlayTex);
    std::function<void()> progress;
    if (onUpdate) progress = [&onUpdate] { onUpdate(kOverlayTex); };
    return fn(canvas, params, isCancelled, progress);
}

std::vector<std::uint8_t> generateAsyncOverlay(const QString& effectKey, QSize size,
                                               const QJsonObject& params) {
    if (size.width() <= 0 || size.height() <= 0) return {};
    StrokeCanvas canvas(size.width(), size.height());
    runAsyncInit(effectKey, canvas, params);
    return canvas.unpremultipliedRgba8();
}

QStringList asyncOverlayTextureIds(const Graph& graph) {
    QStringList ids;
    QSet<QString> seen;
    for (const Pass& pass : graph.passes) {
        if (pass.effectKey.isEmpty() || pass.nodeId.isEmpty() || seen.contains(pass.nodeId)) continue;
        seen.insert(pass.nodeId);
        for (const QString& name : asyncInitTextures(pass.effectKey)) {
            ids.append(pass.nodeId + QLatin1Char('_') + name);
        }
    }
    return ids;
}

int AsyncOverlays::sync(Backend& backend, const Graph& graph, QSize size, const QJsonObject& globalUniforms) {
    int generated = 0;
    QSet<QString> seen;
    QSet<QString> current; // async nodes whose overlay this object keeps
    for (const Pass& pass : graph.passes) {
        // reference initAsyncEffects: the first pass of each node decides.
        if (pass.effectKey.isEmpty() || pass.nodeId.isEmpty() || seen.contains(pass.nodeId)) continue;
        seen.insert(pass.nodeId);
        if (!hasAsyncInit(pass.effectKey) || size.width() <= 0 || size.height() <= 0) continue;

        QStringList textureIds;
        bool hostSupplied = false;
        for (const QString& name : asyncInitTextures(pass.effectKey)) {
            const QString texId = pass.nodeId + QLatin1Char('_') + name;
            textureIds.append(texId);
            if (backend.hostSuppliesTexture(texId)) hostSupplied = true;
        }
        // A host texture for the node's overlay takes precedence (backend.h).
        if (hostSupplied) continue;

        current.insert(pass.nodeId);
        const QJsonObject params = asyncInitParams(pass.effectKey, pass.uniforms, globalUniforms);
        const auto existing = m_nodes.constFind(pass.nodeId);
        bool uploaded = true;
        for (const QString& texId : textureIds) uploaded = uploaded && backend.hasGeneratedTexture(texId);
        if (uploaded && existing != m_nodes.constEnd() && existing->effectKey == pass.effectKey
            && existing->size == size && existing->params == params) {
            continue;
        }

        const std::vector<std::uint8_t> pixels = generateAsyncOverlay(pass.effectKey, size, params);
        Node node;
        node.effectKey = pass.effectKey;
        node.size = size;
        node.params = params;
        node.textureIds = textureIds;
        for (const QString& texId : textureIds) backend.uploadGeneratedTexture(texId, pixels, size);
        if (existing != m_nodes.constEnd()) {
            for (const QString& texId : existing->textureIds) {
                if (!textureIds.contains(texId)) backend.removeGeneratedTexture(texId);
            }
        }
        m_nodes.insert(pass.nodeId, node);
        ++generated;
    }

    for (auto it = m_nodes.begin(); it != m_nodes.end();) {
        if (current.contains(it.key())) {
            ++it;
            continue;
        }
        for (const QString& texId : it->textureIds) backend.removeGeneratedTexture(texId);
        it = m_nodes.erase(it);
    }
    return generated;
}

QStringList AsyncOverlays::textureIds() const {
    QStringList ids;
    for (const Node& node : m_nodes) ids.append(node.textureIds);
    return ids;
}

} // namespace nm
