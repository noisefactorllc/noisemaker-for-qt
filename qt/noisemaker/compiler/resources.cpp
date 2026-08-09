#include "resources.h"

namespace nm {

namespace {
bool isGlobal(const QString& texId) {
    return texId.startsWith(QStringLiteral("global_"));
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
    for (int index = 0; index < passes.size(); ++index) {
        const PassIO& pass = passes.at(index);
        for (const QString& tex : pass.inputs) touch(lifetime, tex, index);
        for (const QString& tex : pass.outputs) touch(lifetime, tex, index);
    }
    return lifetime;
}

QMap<QString, QString> allocateResources(const QVector<PassIO>& passes) {
    const QMap<QString, TexLifetime> lifetime = analyzeLiveness(passes);
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

        // 2. Release inputs (last uses), in declaration order.
        for (const QString& texId : pass.inputs) {
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
