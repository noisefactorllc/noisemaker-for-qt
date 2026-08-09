#include "pingpong.h"

#include <QRegularExpression>

namespace nm {

namespace {

const QString kGlobalPrefix = QStringLiteral("global_");

// texId -> bare name iff texId is a `global_`-prefixed reference; otherwise
// an empty (null) string. Mirrors the reference Pipeline.parseGlobalName /
// godot's own `t.begins_with("global_")` checks -- deliberately NOT the
// broader webgl2.js backend-level `parseGlobalName` (which also recognizes
// bare "globalFoo" camelCase), since every texId this port's graphs
// actually emit (export-graph.mjs, verified against real minted graphs
// for physarum/flow/pointsEmit/navierStokes) uses the `global_` underscore
// form exclusively, and godot -- the T5 brief's explicit structural
// cross-check for this exact rule -- matches that same narrower form
// throughout.
QString stripGlobalPrefix(const QString& texId) {
    return texId.startsWith(kGlobalPrefix) ? texId.mid(kGlobalPrefix.size()) : QString();
}

QString physicalKeyA(const QString& bareName) {
    return kGlobalPrefix + bareName + QStringLiteral("#a");
}

QString physicalKeyB(const QString& bareName) {
    return kGlobalPrefix + bareName + QStringLiteral("#b");
}

} // namespace

QSet<QString> computeHazardSurfaces(const Graph& graph) {
    QHash<QString, int> firstWrite; // full texId -> pass index
    for (int i = 0; i < graph.passes.size(); ++i) {
        const Pass& pass = graph.passes.at(i);
        for (auto it = pass.outputs.begin(); it != pass.outputs.end(); ++it) {
            const QString texId = it.value().toString();
            if (texId.startsWith(kGlobalPrefix) && !firstWrite.contains(texId)) {
                firstWrite.insert(texId, i);
            }
        }
    }

    QSet<QString> hazardTexIds;
    for (int i = 0; i < graph.passes.size(); ++i) {
        const Pass& pass = graph.passes.at(i);

        // (a) same-pass in-place read+write.
        QSet<QString> inputSet;
        for (auto it = pass.inputs.begin(); it != pass.inputs.end(); ++it) {
            const QString texId = it.value().toString();
            if (texId.startsWith(kGlobalPrefix)) {
                inputSet.insert(texId);
            }
        }
        for (auto it = pass.outputs.begin(); it != pass.outputs.end(); ++it) {
            const QString texId = it.value().toString();
            if (inputSet.contains(texId)) {
                hazardTexIds.insert(texId);
            }
        }

        // (b) read at-or-before the surface's first write.
        for (auto it = pass.inputs.begin(); it != pass.inputs.end(); ++it) {
            const QString texId = it.value().toString();
            if (texId.isEmpty() || texId == QStringLiteral("none")) {
                continue;
            }
            if (!texId.startsWith(kGlobalPrefix)) {
                continue;
            }
            const auto writeIt = firstWrite.constFind(texId);
            if (writeIt != firstWrite.constEnd() && i <= writeIt.value()) {
                hazardTexIds.insert(texId);
            }
        }
    }

    QSet<QString> hazardBareNames;
    hazardBareNames.reserve(hazardTexIds.size());
    for (const QString& texId : hazardTexIds) {
        hazardBareNames.insert(stripGlobalPrefix(texId));
    }
    return hazardBareNames;
}

bool isStateSurface(const QString& name) {
    if (name.isEmpty()) {
        return false;
    }
    if (name == QStringLiteral("xyz") || name == QStringLiteral("vel") || name == QStringLiteral("rgba")
        || name == QStringLiteral("trail")) {
        return true;
    }
    if (name.endsWith(QStringLiteral("_xyz")) || name.endsWith(QStringLiteral("_vel"))
        || name.endsWith(QStringLiteral("_rgba")) || name.endsWith(QStringLiteral("_trail"))) {
        return true;
    }
    if (name.contains(QStringLiteral("state")) || name.contains(QStringLiteral("State"))) {
        return true;
    }
    static const QRegularExpression stateNodeRe(QStringLiteral("^(xyz|vel|rgba|points_trail)_node_\\d+$"));
    return stateNodeRe.match(name).hasMatch();
}

QJsonObject mergeAllPassUniforms(const Graph& graph) {
    QJsonObject out;
    for (const Pass& pass : graph.passes) {
        for (auto it = pass.uniforms.begin(); it != pass.uniforms.end(); ++it) {
            out.insert(it.key(), it.value());
        }
    }
    return out;
}

void PingPongState::syncGraph(const Graph& graph) {
    m_hazardBareNames = computeHazardSurfaces(graph);
}

bool PingPongState::isHazard(const QString& bareName) const {
    return m_hazardBareNames.contains(bareName);
}

void PingPongState::beginFrame() {
    for (const QString& bare : std::as_const(m_hazardBareNames)) {
        auto it = m_persistent.find(bare);
        if (it == m_persistent.end()) {
            it = m_persistent.insert(bare, Binding{physicalKeyA(bare), physicalKeyB(bare)});
        }
        m_frameRead.insert(bare, it->read);
        m_frameWrite.insert(bare, it->write);
    }
}

QString PingPongState::physicalRead(const QString& bareName) const {
    return m_frameRead.value(bareName);
}

QString PingPongState::physicalWrite(const QString& bareName) const {
    return m_frameWrite.value(bareName);
}

void PingPongState::updateFrameBindings(const Pass& pass) {
    for (auto it = pass.outputs.begin(); it != pass.outputs.end(); ++it) {
        const QString texId = it.value().toString();
        const QString bare = stripGlobalPrefix(texId);
        if (bare.isEmpty() || !m_hazardBareNames.contains(bare)) {
            continue;
        }
        const auto writeIt = m_frameWrite.constFind(bare);
        if (writeIt == m_frameWrite.constEnd()) {
            continue;
        }
        const QString writeId = writeIt.value();
        const QString curRead = m_frameRead.value(bare);
        m_frameRead.insert(bare, writeId);
        if (!curRead.isEmpty()) {
            m_frameWrite.insert(bare, curRead);
        }
    }
}

void PingPongState::adoptIterationBindings(const Pass& pass) {
    for (auto it = pass.outputs.begin(); it != pass.outputs.end(); ++it) {
        const QString texId = it.value().toString();
        const QString bare = stripGlobalPrefix(texId);
        if (bare.isEmpty()) {
            continue;
        }
        auto persistentIt = m_persistent.find(bare);
        if (persistentIt == m_persistent.end()) {
            continue;
        }
        const auto readIt = m_frameRead.constFind(bare);
        if (readIt != m_frameRead.constEnd()) {
            persistentIt->read = readIt.value();
        }
        const auto writeIt = m_frameWrite.constFind(bare);
        if (writeIt != m_frameWrite.constEnd()) {
            persistentIt->write = writeIt.value();
        }
    }
}

void PingPongState::endFrame() {
    for (const QString& bare : std::as_const(m_hazardBareNames)) {
        auto persistentIt = m_persistent.find(bare);
        if (persistentIt == m_persistent.end()) {
            continue;
        }
        if (isStateSurface(bare)) {
            const auto readIt = m_frameRead.constFind(bare);
            const auto writeIt = m_frameWrite.constFind(bare);
            if (readIt != m_frameRead.constEnd() && writeIt != m_frameWrite.constEnd()) {
                persistentIt->read = readIt.value();
                persistentIt->write = writeIt.value();
            }
        } else {
            std::swap(persistentIt->read, persistentIt->write);
        }
    }
    // Deliberately NOT clearing m_frameRead/m_frameWrite -- see header doc:
    // a caller's readSurface() after the final render() of a settle loop
    // needs this frame's bindings to still be visible, and the next
    // beginFrame() unconditionally overwrites every hazard bare name's
    // entry before any pass reads it again.
}

} // namespace nm
