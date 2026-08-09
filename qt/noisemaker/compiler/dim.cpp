#include "dim.h"

#include <QJsonObject>

namespace nm::dim {

bool referencesParam(const QJsonValue& d) {
    if (!d.isObject()) return false;
    const QJsonObject o = d.toObject();
    return o.contains(QStringLiteral("param")) || o.contains(QStringLiteral("screenDivide"));
}

QJsonValue scope(const QJsonValue& d, const QString& scopeSuffix, QMap<QString, QString>& scopedParamMap) {
    if (!d.isObject()) return d;
    QJsonObject o = d.toObject();
    // `param` takes precedence, matching the reference's if/else-if chain
    // (scopeDimSpec checks `dimSpec.param !== undefined` before
    // `dimSpec.screenDivide !== undefined`).
    if (o.contains(QStringLiteral("param"))) {
        const QString original = o.value(QStringLiteral("param")).toString();
        const QString scoped = original + QLatin1Char('_') + scopeSuffix;
        scopedParamMap.insert(original, scoped);
        o.insert(QStringLiteral("param"), scoped);
        return o;
    }
    if (o.contains(QStringLiteral("screenDivide"))) {
        const QString original = o.value(QStringLiteral("screenDivide")).toString();
        const QString scoped = original + QLatin1Char('_') + scopeSuffix;
        scopedParamMap.insert(original, scoped);
        o.insert(QStringLiteral("screenDivide"), scoped);
        return o;
    }
    return d;
}

} // namespace nm::dim
