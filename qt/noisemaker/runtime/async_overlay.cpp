#include "async_overlay.h"

#include "backend.h"
#include "graph.h"
#include "stroke_canvas.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#endif

namespace nm {

namespace {

// Called first on a background trace's worker thread (GAP-038). Lower its
// priority relative to the renderer, but keep the trace able to finish.
// Linux uses the fair scheduler with nice 19: SCHED_IDLE can starve the
// trace while a software renderer keeps presenting frames. macOS uses
// utility QoS, and Windows uses the lowest normal thread priority.
void lowerTraceThreadPriority() {
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#elif defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#elif defined(__linux__)
    sched_param param{};
    param.sched_priority = 0;
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
    setpriority(PRIO_PROCESS, 0, 19);
#endif
}

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// ECMAScript StringToNumber for decimal strings and Infinity. Other strings
// are NaN here; JavaScript also reads 0x, 0o and 0b integer strings, which
// the DSL does not produce for seed or density.
double stringToNumber(const QString& text) {
    // Not static: background traces call this from worker threads.
    const QRegularExpression decimal(QStringLiteral("^[+-]?(\\d+\\.?\\d*|\\.\\d+)([eE][+-]?\\d+)?$"));
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
    // reference asyncInit draw params (3bae8ea0): an automation descriptor is drawn from
    // the param's definition default, not from the descriptor.
    const double densityDefault = effectKey == QStringLiteral("filter.scratches") ? 0.3 : 0.5;
    QJsonObject params;
    for (const QString& name : read) {
        const auto it = source.constFind(name);
        if (it == source.constEnd()) continue;
        if (it.value().isObject()) {
            params.insert(name, name == QStringLiteral("seed") ? 1.0 : densityDefault);
        } else {
            params.insert(name, it.value());
        }
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

// A background trace. effectKey, size and params belong to the render
// thread; the worker thread traces its own deep copies of them. The worker
// writes only pixels, completed and done, and the render thread reads pixels
// and completed only after it sees done.
//
// done is the worker's last access to the Job: it frees its canvas and its
// copies first, so after done it only returns. The render thread therefore
// never joins an ended worker (release()): a join waits until the worker
// thread has exited, and a worker at its lowered priority
// (lowerTraceThreadPriority) can wait for a CPU behind every rasterizer
// thread first. Only wait(), clear() and the destructor join.
struct AsyncOverlays::Job {
    QString effectKey;
    QSize size;
    QJsonObject params;
    std::atomic<bool> cancelled{false};
    std::atomic<bool> done{false};
    bool completed = false; // traced to the end (not cancelled)
    // The overlay's rows in texture order, bottom row first: the worker
    // flips them, so the upload copies them as they are.
    std::vector<std::uint8_t> pixels;
    std::thread thread;

    bool matches(const QString& key, QSize s, const QJsonObject& p) const {
        return effectKey == key && size == s && params == p;
    }
    void join() {
        if (thread.joinable()) thread.join();
    }
    // After done: lets the worker finish returning on its own.
    void release() {
        if (thread.joinable()) thread.detach();
    }
    ~Job() {
        cancelled.store(true, std::memory_order_relaxed);
        join();
    }
};

namespace {

// A copy that shares no implicitly shared data with `value`. Every value
// stays exact, NaN and the infinities included (a JSON text round trip
// would turn them into null).
QJsonValue detachedCopy(const QJsonValue& value) {
    switch (value.type()) {
    case QJsonValue::String: {
        const QString text = value.toString();
        return QString(text.constData(), text.size());
    }
    case QJsonValue::Array: {
        QJsonArray copy;
        for (const QJsonValue& element : value.toArray()) copy.append(detachedCopy(element));
        return copy;
    }
    case QJsonValue::Object: {
        const QJsonObject object = value.toObject();
        QJsonObject copy;
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            copy.insert(QString(it.key().constData(), it.key().size()), detachedCopy(it.value()));
        }
        return copy;
    }
    default:
        return value; // null, bool, number, undefined: held by value
    }
}

// Reverses the row order of the RGBA8 `pixels` of `size`: the reference's
// flipY = true upload, done by a background trace's worker so the render
// thread copies nothing (Backend::uploadGeneratedTexture, bottomUp).
void flipRows(std::vector<std::uint8_t>& pixels, QSize size) {
    const std::size_t rowBytes = static_cast<std::size_t>(size.width()) * 4;
    if (size.height() <= 1 || pixels.size() != rowBytes * static_cast<std::size_t>(size.height())) return;
    for (int top = 0, bottom = size.height() - 1; top < bottom; ++top, --bottom) {
        std::uint8_t* const a = pixels.data() + static_cast<std::size_t>(top) * rowBytes;
        std::uint8_t* const b = pixels.data() + static_cast<std::size_t>(bottom) * rowBytes;
        std::swap_ranges(a, a + rowBytes, b);
    }
}

} // namespace

AsyncOverlays::AsyncOverlays() = default;

AsyncOverlays::~AsyncOverlays() {
    clear();
}

void AsyncOverlays::startJob(Job& job) {
    // The Job outlives this thread's last access to it: it joins the thread
    // before it is destroyed, or releases it after done.
    Job* const target = &job;
    // Deep copies, so no implicitly shared data crosses to the worker thread.
    QString effectKey = QString::fromUtf8(job.effectKey.toUtf8());
    QJsonObject params = detachedCopy(QJsonValue(job.params)).toObject();
    const QSize size = job.size;
    job.thread = std::thread([target, effectKey = std::move(effectKey), params = std::move(params), size]() mutable {
        lowerTraceThreadPriority();
        std::vector<std::uint8_t> pixels;
        bool completed = false;
        {
            StrokeCanvas canvas(size.width(), size.height());
            const TraceResult result = runAsyncInit(effectKey, canvas, params,
                                                    [target] { return target->cancelled.load(std::memory_order_relaxed); });
            if (result != TraceResult::Cancelled) {
                pixels = canvas.unpremultipliedRgba8();
                flipRows(pixels, size);
                completed = true;
            }
        }
        effectKey = QString();
        params = QJsonObject();
        target->pixels = std::move(pixels);
        target->completed = completed;
        target->done.store(true, std::memory_order_release); // the last access to *target
    });
}

void AsyncOverlays::retire(std::unique_ptr<Job> job) {
    if (!job) return;
    job->cancelled.store(true, std::memory_order_relaxed);
    m_retired.push_back(std::move(job));
}

int AsyncOverlays::sync(Backend& backend, const Graph& graph, QSize size, const QJsonObject& globalUniforms) {
    // Cancelled traces that have ended: released, not joined (Job).
    for (std::unique_ptr<Job>& job : m_retired) {
        if (job->done.load(std::memory_order_acquire)) job->release();
    }
    m_retired.erase(std::remove_if(m_retired.begin(), m_retired.end(),
                                   [](const std::unique_ptr<Job>& job) { return !job->thread.joinable(); }),
                    m_retired.end());

    int uploads = 0;
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
        Node& node = m_nodes[pass.nodeId];
        bool uploaded = !node.textureIds.isEmpty();
        for (const QString& texId : textureIds) uploaded = uploaded && backend.hasGeneratedTexture(texId);
        const bool upToDate = uploaded && !node.placeholder && node.effectKey == pass.effectKey
            && node.size == size && node.params == params;

        // bottomUp: `pixels` holds its rows in texture order (Job::pixels).
        const auto upload = [&](const std::vector<std::uint8_t>& pixels, bool placeholder, bool bottomUp) {
            for (const QString& texId : node.textureIds) {
                if (!textureIds.contains(texId)) backend.removeGeneratedTexture(texId);
            }
            for (const QString& texId : textureIds) backend.uploadGeneratedTexture(texId, pixels, size, bottomUp);
            node.effectKey = pass.effectKey;
            node.size = size;
            node.params = params;
            node.placeholder = placeholder;
            node.textureIds = textureIds;
        };

        if (!m_background) {
            retire(std::move(node.job)); // left over from the background mode
            if (upToDate) continue;
            upload(generateAsyncOverlay(pass.effectKey, size, params), false, false);
            ++uploads;
            continue;
        }

        if (node.job && node.job->matches(pass.effectKey, size, params)) {
            if (!node.job->done.load(std::memory_order_acquire)) continue; // keep the uploaded overlay
            std::unique_ptr<Job> job = std::move(node.job);
            job->release();
            if (job->completed) {
                upload(job->pixels, false, true);
                ++uploads;
                continue;
            }
            // Only a cancelled trace ends incomplete, and a node's current
            // trace is never cancelled; trace again below if it ever is.
        }
        retire(std::move(node.job)); // superseded: its pixels are never uploaded
        if (upToDate) continue;
        // Before the first trace at this size (or of this effect) completes,
        // show a transparent overlay, the reference's cleared canvas, rather
        // than another size's texels.
        if (!uploaded || node.size != size || node.effectKey != pass.effectKey) {
            upload(std::vector<std::uint8_t>(static_cast<size_t>(size.width()) * static_cast<size_t>(size.height()) * 4, 0),
                   true, true);
        }
        auto job = std::make_unique<Job>();
        job->effectKey = pass.effectKey;
        job->size = size;
        job->params = params;
        startJob(*job);
        node.job = std::move(job);
    }

    for (auto it = m_nodes.begin(); it != m_nodes.end();) {
        if (current.contains(it->first)) {
            ++it;
            continue;
        }
        retire(std::move(it->second.job));
        for (const QString& texId : it->second.textureIds) backend.removeGeneratedTexture(texId);
        it = m_nodes.erase(it);
    }
    return uploads;
}

bool AsyncOverlays::pending() const {
    for (const auto& entry : m_nodes) {
        if (entry.second.job) return true;
    }
    return false;
}

void AsyncOverlays::wait() {
    for (auto& entry : m_nodes) {
        if (entry.second.job) entry.second.job->join();
    }
}

void AsyncOverlays::clear() {
    for (auto& entry : m_nodes) {
        if (entry.second.job) entry.second.job->cancelled.store(true, std::memory_order_relaxed);
    }
    m_nodes.clear();   // each Job joins its thread
    m_retired.clear();
}

QStringList AsyncOverlays::textureIds() const {
    QStringList ids;
    for (const auto& entry : m_nodes) ids.append(entry.second.textureIds);
    return ids;
}

} // namespace nm
