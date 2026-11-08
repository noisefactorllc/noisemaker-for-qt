#include "dim.h"

#include <QJsonObject>

namespace nm::dim {

bool referencesParam(const QJsonValue& d) {
    if (!d.isObject()) return false;
    const QJsonObject o = d.toObject();
    return o.contains(QStringLiteral("param")) || o.contains(QStringLiteral("screenDivide"));
}

QJsonValue scope(const QJsonValue& d, const QString& scopeSuffix, QMap<QString, QString>& scopedParamMap,
                  const QString& stateSizeScope, const QString& volumeSizeParam,
                  const SizeParamClaim& chainSizeParam) {
    if (!d.isObject()) return d;
    QJsonObject o = d.toObject();
    // `param` takes precedence, matching the reference's if/else-if chain
    // (scopeDimSpec checks `dimSpec.param !== undefined` before
    // `dimSpec.screenDivide !== undefined`).
    if (o.contains(QStringLiteral("param"))) {
        const QString original = o.value(QStringLiteral("param")).toString();
        QString scoped;
        if (original == QStringLiteral("volumeSize") && !volumeSizeParam.isEmpty()) {
            scoped = volumeSizeParam;
        } else if (original == QStringLiteral("stateSize") && !stateSizeScope.isEmpty()) {
            scoped = original + QLatin1Char('_') + stateSizeScope;
        } else {
            // Particle-scoped or inherited dimensions keep the base name;
            // everything else goes through the expander's size-param claim.
            scoped = chainSizeParam ? chainSizeParam(original + QLatin1Char('_') + scopeSuffix,
                                                    o.value(QStringLiteral("inherit")).toBool(false))
                                    : (original + QLatin1Char('_') + scopeSuffix);
        }
        // An inherited dimension reads the scope's value and never writes
        // this effect's own value over it.
        if (o.value(QStringLiteral("inherit")).toBool(false) == false) {
            scopedParamMap.insert(original, scoped);
        }
        o.insert(QStringLiteral("param"), scoped);
        return o;
    }
    if (o.contains(QStringLiteral("screenDivide"))) {
        const QString original = o.value(QStringLiteral("screenDivide")).toString();
        const QString scoped = chainSizeParam ? chainSizeParam(original + QLatin1Char('_') + scopeSuffix, false)
                                              : (original + QLatin1Char('_') + scopeSuffix);
        scopedParamMap.insert(original, scoped);
        o.insert(QStringLiteral("screenDivide"), scoped);
        return o;
    }
    return d;
}

} // namespace nm::dim
