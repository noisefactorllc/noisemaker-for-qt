#include "resources.h"

#include <QSet>
#include <QRegularExpression>

namespace nm {

namespace {
bool isGlobal(const QString& texId) {
    return texId.startsWith(QStringLiteral("global_"));
}

QMap<QString, QString> buildInputAliases(const QVector<PassIO>& passes) {
    static const QRegularExpression outputPattern(QStringLiteral("^node_[0-9]+_out$"));
    QSet<QString> written;
    for (const PassIO& pass : passes) for (const QString& id : pass.outputs) written.insert(id);
    QMap<QString, QString> aliases;
    for (int i = 0; i < passes.size(); ++i) {
        const PassIO& pass = passes.at(i);
        if (pass.nodeId.isEmpty()) continue;
        for (const QString& id : pass.inputs) {
            if (written.contains(id) || aliases.contains(id)) continue;
            if (id != pass.nodeId + QStringLiteral("_inputTex") && id != pass.nodeId + QStringLiteral("_inputColor")) continue;
            QString upstream;
            for (int j = i - 1; j >= 0 && upstream.isEmpty(); --j) {
                for (const QString& output : passes.at(j).outputs) {
                    if (output != pass.nodeId + QStringLiteral("_out") && outputPattern.match(output).hasMatch()) {
                        upstream = output;
                        break;
                    }
                }
            }
            if (!upstream.isEmpty()) aliases.insert(id, upstream);
        }
    }
    return aliases;
}

void touch(QMap<QString, TexLifetime>& lifetime, const QString& texId, int index) {
    if (texId.isEmpty()) return;
    if (isGlobal(texId)) return;
    auto it = lifetime.find(texId);
    if (it == lifetime.end()) {
        lifetime.insert(texId, TexLifetime{index, index});
    } else {
        it->start = std::min(it->start, index);
        it->end = std::max(it->end, index);
    }
}
} // namespace

QMap<QString, TexLifetime> analyzeLiveness(const QVector<PassIO>& passes) {
    QMap<QString, TexLifetime> lifetime;
    const auto aliases = buildInputAliases(passes);
    for (int index = 0; index < passes.size(); ++index) {
        const PassIO& pass = passes.at(index);
        for (const QString& tex : pass.inputs) touch(lifetime, aliases.value(tex, tex), index);
        for (const QString& tex : pass.outputs) touch(lifetime, tex, index);
    }
    return lifetime;
}

QMap<QString, QString> allocateResources(const QVector<PassIO>& passes) {
    const QMap<QString, TexLifetime> lifetime = analyzeLiveness(passes);
    const auto aliases = buildInputAliases(passes);
    QMap<QString, QString> allocations;
    // freeList: ordered list of {physId, availableAfter}. Ordered (not a
    // map) because the reused-slot search must scan in PUSH order and
    // splice out the first eligible match — reference/04 §1.2/§1.3.
    QVector<QPair<QString, int>> freeList;
    int physicalCount = 0;

    for (int i = 0; i < passes.size(); ++i) {
        const PassIO& pass = passes.at(i);

        // 1. Allocate outputs (definitions), in declaration order.
        for (const QString& texId : pass.outputs) {
            if (isGlobal(texId)) continue;
            if (allocations.contains(texId)) continue;

            int freeIdx = -1;
            for (int k = 0; k < freeList.size(); ++k) {
                if (freeList.at(k).second < i) { freeIdx = k; break; }
            }
            if (freeIdx != -1) {
                allocations.insert(texId, freeList.at(freeIdx).first);
                freeList.remove(freeIdx);
            } else {
                allocations.insert(texId, QStringLiteral("phys_%1").arg(physicalCount++));
            }
        }

        // 2. Release inputs (last uses), in declaration order. A pass may read one texture
        // under several names (lighting's inputTex and heightMap); release it once, or its
        // slot is handed to two textures that are live together.
        QSet<QString> released;
        for (const QString& inputId : pass.inputs) {
            const QString texId = aliases.value(inputId, inputId);
            if (released.contains(texId)) continue;
            released.insert(texId);
            if (isGlobal(texId)) continue;
            const auto it = lifetime.constFind(texId);
            if (it != lifetime.constEnd() && it->end == i) {
                const auto allocIt = allocations.constFind(texId);
                if (allocIt != allocations.constEnd()) {
                    freeList.append({allocIt.value(), i});
                }
            }
        }
    }

    return allocations;
}

} // namespace nm
