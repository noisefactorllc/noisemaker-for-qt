#include "device_limits.h"

#include <QVector>

#include <algorithm>

namespace nm::detail {

namespace {

bool isVolumeSizeUniform(const QString& name) {
    return name == QStringLiteral("volumeSize")
        || name.startsWith(QStringLiteral("volumeSize_chain_"))
        || name.startsWith(QStringLiteral("volumeSize_node_"));
}

double clampVolumeSize(double value, int maxTextureSize) {
    if (maxTextureSize <= 0
        || value * value <= maxTextureSize) {
        return value;
    }
    int clamped = 16;
    while (static_cast<long long>(clamped * 2) * static_cast<long long>(clamped * 2) <= maxTextureSize
           && clamped * 2 < value) {
        clamped *= 2;
    }
    return static_cast<double>(clamped);
}

int colorAttachmentBytes(const QString& format) {
    if (format == QStringLiteral("rgba32f") || format == QStringLiteral("rgba32float")) {
        return 16;
    }
    if (format == QStringLiteral("rgba8") || format == QStringLiteral("rgba8unorm")) {
        return 4;
    }
    return 8;
}

struct MrtEntry {
    QString textureId;
    int location = -1;
};

} // namespace

bool clampGraphVolumeSizes(Graph& graph, int maxTextureSize) {
    if (maxTextureSize <= 0) return false;
    bool changed = false;
    for (Pass& pass : graph.passes) {
        for (auto it = pass.uniforms.begin(); it != pass.uniforms.end(); ++it) {
            if (!isVolumeSizeUniform(it.key()) || !it.value().isDouble()) continue;
            const double requested = it.value().toDouble();
            const double clamped = clampVolumeSize(requested, maxTextureSize);
            if (clamped == requested) continue;
            it.value() = static_cast<int>(clamped);
            changed = true;
        }
    }
    return changed;
}

bool applyMrtFormatBudget(
    Graph& graph,
    const Pass& pass,
    const QHash<QString, int>& outputLocations,
    int maxColorBytesPerSample) {
    if (maxColorBytesPerSample <= 0 || pass.outputs.size() <= 1) return false;

    QVector<MrtEntry> entries;
    int total = 0;
    for (auto it = pass.outputs.begin(); it != pass.outputs.end(); ++it) {
        const auto locationIt = outputLocations.constFind(it.key());
        if (locationIt == outputLocations.constEnd() || locationIt.value() < 0) continue;
        const QString textureId = it.value().toString();
        const auto specIt = graph.textures.constFind(textureId);
        const QString format = specIt == graph.textures.constEnd()
            ? QStringLiteral("rgba16f")
            : specIt->format;
        total += colorAttachmentBytes(format);
        entries.append({textureId, locationIt.value()});
    }
    if (total <= maxColorBytesPerSample) return false;

    bool changed = false;
    std::sort(entries.begin(), entries.end(), [](const MrtEntry& lhs, const MrtEntry& rhs) {
        return lhs.location < rhs.location;
    });
    for (auto it = entries.crbegin(); it != entries.crend() && total > maxColorBytesPerSample; ++it) {
        auto specIt = graph.textures.find(it->textureId);
        if (specIt == graph.textures.end()) continue;
        if (specIt->format != QStringLiteral("rgba32f")
            && specIt->format != QStringLiteral("rgba32float")) {
            continue;
        }
        specIt->format = QStringLiteral("rgba16f");
        total -= 8;
        changed = true;
    }
    return changed;
}

} // namespace nm::detail
