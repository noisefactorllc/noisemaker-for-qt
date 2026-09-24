#include "parameters.h"

#include "../compiler/enums.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QStringList>

#include <cmath>

namespace nm {

namespace {

struct PaletteEntry {
    double amp[3];
    double freq[3];
    double offset[3];
    double phase[3];
    int mode;
};

// Verbatim values of palette-expansion.js PALETTES (1-based index order),
// printed from the reference module's own expandPalette(1..55).
const PaletteEntry kPalettes[] = {
    {{0.76, 0.88, 0.37}, {1.0, 1.0, 1.0}, {0.93, 0.97, 0.52}, {0.21, 0.41, 0.56}, 3},
    {{0.56851584, 0.7740668, 0.23485267}, {1.0, 1.0, 1.0}, {0.5, 0.5, 0.5}, {0.727029, 0.08039695, 0.10427457}, 3},
    {{0.5, 0.5, 0.5}, {1.0, 1.0, 1.0}, {0.5, 0.5, 0.5}, {0.3, 0.2, 0.2}, 3},
    {{0.45, 0.2, 0.1}, {1.0, 1.0, 1.0}, {0.7, 0.2, 0.2}, {0.5, 0.4, 0.0}, 3},
    {{0.09, 0.59, 0.48}, {1.0, 1.0, 1.0}, {0.2, 0.31, 0.98}, {0.88, 0.4, 0.33}, 3},
    {{0.5, 0.5, 0.5}, {1.0, 1.0, 1.0}, {0.1, 0.4, 0.7}, {0.1, 0.1, 0.1}, 3},
    {{0.5, 0.5, 0.5}, {1.0, 1.0, 1.0}, {0.5, 0.5, 0.5}, {0.0, 0.1, 0.2}, 3},
    {{0.7259015, 0.7004237, 0.9494409}, {1.0, 1.0, 1.0}, {0.63290054, 0.37883538, 0.29405284}, {0.0, 0.1, 0.2}, 3},
    {{0.94, 0.33, 0.27}, {1.0, 1.0, 1.0}, {0.74, 0.37, 0.73}, {0.44, 0.17, 0.88}, 3},
    {{1.0, 0.7, 1.0}, {1.0, 1.0, 1.0}, {1.0, 0.4, 0.9}, {0.4, 0.5, 0.6}, 3},
    {{0.51, 0.39, 0.41}, {1.0, 1.0, 1.0}, {0.59, 0.53, 0.94}, {0.15, 0.41, 0.46}, 3},
    {{0.0, 0.0, 0.51}, {1.0, 1.0, 1.0}, {0.0, 0.0, 0.43}, {0.0, 0.0, 0.36}, 1},
    {{0.83, 0.45, 0.19}, {1.0, 1.0, 1.0}, {0.79, 0.45, 0.35}, {0.28, 0.91, 0.61}, 3},
    {{0.5, 0.5, 0.5}, {1.0, 1.0, 1.0}, {0.5, 0.5, 0.5}, {0.0, 0.2, 0.25}, 3},
    {{0.5, 0.5, 0.5}, {1.0, 1.0, 1.0}, {0.22, 0.48, 0.62}, {0.1, 0.3, 0.2}, 3},
    {{0.02, 0.92, 0.76}, {1.0, 1.0, 1.0}, {0.51, 0.49, 0.51}, {0.71, 0.23, 0.66}, 1},
    {{0.5, 0.5, 0.5}, {2.0, 2.0, 2.0}, {0.5, 0.5, 0.5}, {1.0, 1.0, 1.0}, 3},
    {{0.79, 0.56, 0.22}, {1.0, 1.0, 1.0}, {0.96, 0.5, 0.49}, {0.15, 0.98, 0.87}, 3},
    {{0.75804377, 0.62868536, 0.2227562}, {1.0, 1.0, 1.0}, {0.35536355, 0.12935615, 0.17060602}, {0.0, 0.25, 0.5}, 3},
    {{0.79, 0.5, 0.23}, {1.0, 1.0, 1.0}, {0.75, 0.47, 0.45}, {0.08, 0.84, 0.16}, 3},
    {{0.7, 0.81, 0.73}, {1.0, 1.0, 1.0}, {0.1, 0.22, 0.27}, {0.99, 0.12, 0.94}, 3},
    {{0.5, 0.5, 0.5}, {0.0, 0.0, 1.0}, {0.5, 0.5, 0.5}, {0.5, 0.5, 0.5}, 3},
    {{0.5, 0.5, 0.5}, {0.0, 1.0, 1.0}, {0.5, 0.5, 0.5}, {0.5, 0.5, 0.5}, 3},
    {{0.5, 0.5, 0.5}, {0.0, 1.0, 0.0}, {0.5, 0.5, 0.5}, {0.5, 0.5, 0.5}, 3},
    {{0.5, 0.5, 0.5}, {1.0, 0.0, 1.0}, {0.5, 0.5, 0.5}, {0.5, 0.5, 0.5}, 3},
    {{0.5, 0.5, 0.5}, {1.0, 0.0, 0.0}, {0.5, 0.5, 0.5}, {0.5, 0.5, 0.5}, 3},
    {{0.5, 0.5, 0.5}, {1.0, 1.0, 0.0}, {0.5, 0.5, 0.5}, {0.5, 0.5, 0.5}, 3},
    {{0.74, 0.33, 0.09}, {1.0, 1.0, 1.0}, {0.62, 0.2, 0.2}, {0.2, 0.1, 0.0}, 3},
    {{0.56, 0.68, 0.39}, {1.0, 1.0, 1.0}, {0.72, 0.07, 0.62}, {0.25, 0.4, 0.41}, 3},
    {{0.78, 0.39, 0.07}, {1.0, 1.0, 1.0}, {0.0, 0.53, 0.33}, {0.94, 0.92, 0.9}, 3},
    {{0.5, 0.5, 0.5}, {1.0, 1.0, 1.0}, {0.2, 0.64, 0.62}, {0.15, 0.2, 0.3}, 3},
    {{0.5, 0.5, 0.5}, {1.0, 1.0, 1.0}, {0.64, 0.12, 0.84}, {0.1, 0.25, 0.15}, 3},
    {{0.42, 0.42, 0.04}, {1.0, 1.0, 1.0}, {0.47, 0.27, 0.27}, {0.41, 0.14, 0.11}, 3},
    {{0.65, 0.4, 0.11}, {1.0, 1.0, 1.0}, {0.72, 0.45, 0.08}, {0.71, 0.8, 0.84}, 3},
    {{0.62, 0.79, 0.11}, {1.0, 1.0, 1.0}, {0.22, 0.56, 0.17}, {0.15, 0.1, 0.25}, 3},
    {{0.5, 0.5, 0.5}, {1.0, 1.0, 1.0}, {0.41, 0.22, 0.67}, {0.2, 0.25, 0.2}, 3},
    {{0.5, 0.5, 0.5}, {1.0, 1.0, 1.0}, {0.5, 0.5, 0.5}, {0.25, 0.5, 0.75}, 3},
    {{0.6059281, 0.17591387, 0.17166573}, {1.0, 1.0, 1.0}, {0.5224456, 0.3864609, 0.36020845}, {0.0, 0.25, 0.5}, 3},
    {{0.6059281, 0.17591387, 0.17166573}, {2.0, 2.0, 2.0}, {0.5224456, 0.3864609, 0.36020845}, {0.0, 0.25, 0.5}, 3},
    {{0.42, 0.0, 0.0}, {2.0, 2.0, 2.0}, {0.45, 0.5, 0.42}, {0.63, 1.0, 1.0}, 2},
    {{0.5, 0.5, 0.5}, {1.0, 1.0, 1.0}, {0.83, 0.6, 0.63}, {0.3, 0.1, 0.0}, 3},
    {{0.5, 0.5, 0.5}, {1.0, 1.0, 1.0}, {0.6, 0.4, 0.1}, {0.3, 0.2, 0.1}, 3},
    {{0.46, 0.73, 0.19}, {1.0, 1.0, 1.0}, {0.27, 0.79, 0.78}, {0.27, 0.16, 0.04}, 2},
    {{0.67, 0.25, 0.27}, {1.0, 1.0, 1.0}, {0.74, 0.48, 0.46}, {0.07, 0.79, 0.39}, 3},
    {{0.9, 0.43, 0.34}, {1.0, 1.0, 1.0}, {0.56, 0.69, 0.32}, {0.03, 0.8, 0.4}, 3},
    {{0.73, 0.36, 0.52}, {1.0, 1.0, 1.0}, {0.78, 0.68, 0.15}, {0.74, 0.93, 0.28}, 3},
    {{1.0, 0.0, 0.8}, {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0}, {0.0, 0.5, 0.1}, 3},
    {{1.0, 0.25, 0.5}, {0.5, 0.5, 0.5}, {0.0, 0.0, 0.25}, {0.5, 0.0, 0.0}, 3},
    {{0.5, 0.5, 0.5}, {1.0, 1.0, 1.0}, {0.26, 0.57, 0.03}, {0.0, 0.1, 0.3}, 3},
    {{0.28, 0.08, 0.65}, {1.0, 1.0, 1.0}, {0.48, 0.6, 0.03}, {0.1, 0.15, 0.3}, 2},
    {{0.65, 0.93, 0.73}, {1.0, 1.0, 1.0}, {0.31, 0.21, 0.27}, {0.43, 0.45, 0.48}, 3},
    {{0.9, 0.76, 0.63}, {1.0, 1.0, 1.0}, {0.0, 0.19, 0.68}, {0.43, 0.23, 0.32}, 3},
    {{0.78, 0.63, 0.68}, {1.0, 1.0, 1.0}, {0.41, 0.03, 0.16}, {0.81, 0.61, 0.06}, 3},
    {{0.97, 0.74, 0.23}, {1.0, 1.0, 1.0}, {0.97, 0.38, 0.35}, {0.34, 0.41, 0.44}, 3},
    {{0.68, 0.79, 0.57}, {1.0, 1.0, 1.0}, {0.56, 0.35, 0.14}, {0.73, 0.9, 0.99}, 3},
};

QJsonArray vec3(const double (&v)[3]) {
    return QJsonArray{v[0], v[1], v[2]};
}

// canvas.js resolveEnumValue: walk a dotted path through the merged enum
// tree (project entries before std entries, as Validator::resolveEnum).
QJsonValue resolveEnumValue(const QString& path, const Enums* enums) {
    if (!enums) return QJsonValue(QJsonValue::Undefined);
    const QStringList segments = path.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (segments.isEmpty()) return QJsonValue(QJsonValue::Undefined);
    QJsonValue node = enums->tryGetHead(segments.first());
    for (int i = 1; i < segments.size(); ++i) {
        if (!node.isObject() || Enums::isLeaf(node)) return QJsonValue(QJsonValue::Undefined);
        node = node.toObject().value(segments.at(i));
        if (node.isUndefined()) return QJsonValue(QJsonValue::Undefined);
    }
    if (node.isDouble() || node.isBool()) return node;
    if (node.isObject() && node.toObject().contains(QStringLiteral("value"))) {
        return node.toObject().value(QStringLiteral("value"));
    }
    return QJsonValue(QJsonValue::Undefined);
}

// JS truthiness of a JSON value.
bool jsTruthy(const QJsonValue& value) {
    if (value.isBool()) return value.toBool();
    if (value.isDouble()) return value.toDouble() != 0.0 && !std::isnan(value.toDouble());
    if (value.isString()) return !value.toString().isEmpty();
    return value.isArray() || value.isObject();
}

// JS parseFloat on a JSON value: numbers pass, strings parse their leading
// decimal prefix; anything else is NaN (JSON cannot carry NaN, so null).
QJsonValue jsParseFloat(const QJsonValue& value) {
    if (value.isDouble()) return value;
    if (value.isString()) {
        static const QRegularExpression prefix(
            QStringLiteral("^\\s*([+-]?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][+-]?\\d+)?)"));
        const QRegularExpressionMatch match = prefix.match(value.toString());
        if (match.hasMatch()) return match.captured(1).toDouble();
    }
    return QJsonValue(QJsonValue::Null);
}

// JS parseInt(value, 10) on a string: leading optional sign and digits.
QJsonValue jsParseInt(const QJsonValue& value) {
    if (value.isString()) {
        static const QRegularExpression prefix(QStringLiteral("^\\s*([+-]?\\d+)"));
        const QRegularExpressionMatch match = prefix.match(value.toString());
        if (match.hasMatch()) return match.captured(1).toDouble();
    }
    if (value.isDouble()) return std::trunc(value.toDouble());
    return QJsonValue(QJsonValue::Null);
}

} // namespace

bool isAutomationControlled(const QJsonValue& value) {
    if (!value.isObject()) return false;
    const QJsonObject object = value.toObject();
    if (jsTruthy(object.value(QStringLiteral("_varRef")))) return true;
    QString type = object.value(QStringLiteral("type")).toString();
    if (type.isEmpty()) {
        type = object.value(QStringLiteral("_ast")).toObject().value(QStringLiteral("type")).toString();
    }
    return type == QStringLiteral("Oscillator") || type == QStringLiteral("Midi")
        || type == QStringLiteral("Audio");
}

QJsonObject expandPalette(double index) {
    const int count = static_cast<int>(sizeof(kPalettes) / sizeof(kPalettes[0]));
    if (!(index > 0) || index > count || index != std::floor(index)) {
        return {};
    }
    const PaletteEntry& entry = kPalettes[static_cast<int>(index) - 1];
    return QJsonObject{
        {QStringLiteral("paletteOffset"), vec3(entry.offset)},
        {QStringLiteral("paletteAmp"), vec3(entry.amp)},
        {QStringLiteral("paletteFreq"), vec3(entry.freq)},
        {QStringLiteral("palettePhase"), vec3(entry.phase)},
        {QStringLiteral("paletteMode"), entry.mode},
    };
}

QJsonValue convertParameterForUniform(const QJsonValue& value, const QJsonObject& spec, const Enums* enums) {
    if (spec.isEmpty()) return value;

    const QString type = spec.value(QStringLiteral("type")).toString();
    const QJsonValue enumBase = spec.value(QStringLiteral("enum")).isString()
        ? spec.value(QStringLiteral("enum"))
        : spec.value(QStringLiteral("enumPath"));
    if ((enumBase.isString() || type == QStringLiteral("member")) && value.isString()) {
        QJsonValue enumValue = resolveEnumValue(value.toString(), enums);
        if (enumValue.isUndefined() && enumBase.isString()) {
            enumValue = resolveEnumValue(enumBase.toString() + QLatin1Char('.') + value.toString(), enums);
        }
        if (!enumValue.isUndefined() && !enumValue.isNull()) return enumValue;
    }

    if (type == QStringLiteral("boolean") || type == QStringLiteral("button")) {
        return jsTruthy(value); // JS !!value
    }
    if (type == QStringLiteral("int")) {
        if (value.isBool()) return value.toBool() ? 1 : 0;
        if (value.isDouble()) return std::floor(value.toDouble() + 0.5); // Math.round
        return jsParseInt(value);
    }
    if (type == QStringLiteral("float")) {
        return jsParseFloat(value);
    }
    if (type == QStringLiteral("color")) {
        if (value.isArray()) {
            const QJsonArray in = value.toArray();
            QJsonArray out;
            for (int i = 0; i < 3 && i < in.size(); ++i) out.append(jsParseFloat(in.at(i)));
            while (out.size() < 3) out.append(0);
            return out;
        }
        if (value.isString() && value.toString().startsWith(QLatin1Char('#'))) {
            const QString hex = value.toString().mid(1);
            QJsonArray out;
            for (int i = 0; i < 3; ++i) {
                bool ok = false;
                const int channel = hex.mid(i * 2, 2).toInt(&ok, 16);
                out.append(ok ? QJsonValue(channel / 255.0) : QJsonValue(QJsonValue::Null));
            }
            return out;
        }
        return value;
    }
    if (type == QStringLiteral("vec3") || type == QStringLiteral("vec4")) {
        if (value.isArray()) {
            QJsonArray out;
            for (const QJsonValue& component : value.toArray()) out.append(jsParseFloat(component));
            return out;
        }
        return value;
    }
    return value;
}

} // namespace nm
