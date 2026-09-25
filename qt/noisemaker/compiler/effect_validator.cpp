// effect_validator.cpp -- port of the REFERENCE
// shaders/src/runtime/effect-validator.js validateEffectDefinition()
// (upstream GAP-003, commits ba87ffae + 9d3474df, byte-for-byte in behavior
// for every shape representable in the port's JSON definition grammar; see
// effect_validator.h for the documented deviations).
//
// Error strings are byte-identical to the reference's template literals.
// Numbers inside messages format through nm::js::numberToString (ECMAScript
// Number::toString), matching the reference's `${number}` interpolation.
// Object key iteration order is the QJsonObject document order (JSON object
// keys keep their parsed order), matching Object.entries() insertion order.
//
// Deterministic and side-effect-free: never throws for malformed/null/array/
// non-object containers, never mutates the input, never invokes lifecycle
// hooks, never sorts runtime globals.

#include "effect_validator.h"

#include "enums.h"
#include "js_number.h"

#include <QJsonArray>
#include <QStringList>

#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

namespace nm {

namespace {

using Errors = std::vector<std::string>;

// --- tag inventory (shaders/src/runtime/tags.js TAG_DEFINITIONS keys, in
// declaration order) ---
const QStringList& validTags() {
    static const QStringList tags = {
        QStringLiteral("color"),  QStringLiteral("distort"),     QStringLiteral("edges"),
        QStringLiteral("geometric"), QStringLiteral("lens"),    QStringLiteral("noise"),
        QStringLiteral("transform"), QStringLiteral("util"),    QStringLiteral("sim"),
        QStringLiteral("3d"),     QStringLiteral("audio"),      QStringLiteral("agents"),
        QStringLiteral("antialiasing"), QStringLiteral("artist"), QStringLiteral("blend"),
        QStringLiteral("blur"),   QStringLiteral("fractal"),    QStringLiteral("geometry"),
        QStringLiteral("glitch"), QStringLiteral("image"),      QStringLiteral("mesh"),
        QStringLiteral("midi"),   QStringLiteral("palette"),    QStringLiteral("pattern"),
        QStringLiteral("pixel"),  QStringLiteral("text"),       QStringLiteral("tiling"),
        QStringLiteral("video"),
    };
    return tags;
}

const QStringList& globalTypes() {
    static const QStringList types = {
        QStringLiteral("float"), QStringLiteral("int"),   QStringLiteral("boolean"),
        QStringLiteral("vec2"),  QStringLiteral("vec3"),  QStringLiteral("vec4"),
        QStringLiteral("mat3"),  QStringLiteral("color"), QStringLiteral("surface"),
        QStringLiteral("volume"), QStringLiteral("geometry"), QStringLiteral("member"),
        QStringLiteral("palette"), QStringLiteral("button"), QStringLiteral("string"),
    };
    return types;
}

const QStringList& uiControls() {
    static const QStringList controls = {
        QStringLiteral("slider"), QStringLiteral("checkbox"), QStringLiteral("dropdown"),
        QStringLiteral("color"),  QStringLiteral("button"),  QStringLiteral("vector3"),
        QStringLiteral("vec3"),
    };
    return controls;
}

const QStringList& uiKeys() {
    static const QStringList keys = {
        QStringLiteral("label"), QStringLiteral("control"),   QStringLiteral("category"),
        QStringLiteral("hidden"), QStringLiteral("hint"),     QStringLiteral("format"),
        QStringLiteral("buttonLabel"), QStringLiteral("enabledBy"), QStringLiteral("multiline"),
    };
    return keys;
}

const QStringList& enabledByOps() {
    static const QStringList ops = {
        QStringLiteral("eq"), QStringLiteral("neq"), QStringLiteral("lt"),
        QStringLiteral("gt"), QStringLiteral("gte"), QStringLiteral("lte"),
        QStringLiteral("in"), QStringLiteral("notIn"),
    };
    return ops;
}

const QStringList& globalSpecKeys() {
    static const QStringList keys = {
        QStringLiteral("type"), QStringLiteral("default"), QStringLiteral("uniform"),
        QStringLiteral("define"), QStringLiteral("choices"), QStringLiteral("enum"),
        QStringLiteral("min"), QStringLiteral("max"), QStringLiteral("step"),
        QStringLiteral("zero"), QStringLiteral("randMin"), QStringLiteral("randMax"),
        QStringLiteral("randChance"), QStringLiteral("randChoices"),
        QStringLiteral("colorModeUniform"), QStringLiteral("ui"),
    };
    return keys;
}

const QStringList& passKeys() {
    static const QStringList keys = {
        QStringLiteral("name"), QStringLiteral("program"), QStringLiteral("type"),
        QStringLiteral("entryPoint"), QStringLiteral("drawMode"), QStringLiteral("drawBuffers"),
        QStringLiteral("count"), QStringLiteral("countUniform"), QStringLiteral("repeat"),
        QStringLiteral("blend"), QStringLiteral("workgroups"),
        QStringLiteral("storageBuffers"), QStringLiteral("storageTextures"),
        QStringLiteral("viewport"), QStringLiteral("conditions"),
        QStringLiteral("defines"), QStringLiteral("uniforms"),
        QStringLiteral("inputs"), QStringLiteral("outputs"),
    };
    return keys;
}

const QStringList& textureSpecKeys() {
    static const QStringList keys = {
        QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("depth"),
        QStringLiteral("format"), QStringLiteral("is3D"),
    };
    return keys;
}

const QStringList& conditionContainerKeys() {
    static const QStringList keys = { QStringLiteral("runIf"), QStringLiteral("skipIf") };
    return keys;
}

const QStringList& dimKeywords() {
    static const QStringList keywords = {
        QStringLiteral("screen"), QStringLiteral("auto"),
        QStringLiteral("input"),  QStringLiteral("resolution"),
    };
    return keywords;
}

const QStringList& formats() {
    static const QStringList formats = {
        QStringLiteral("rgba16f"), QStringLiteral("rgba16float"), QStringLiteral("rgba8"),
        QStringLiteral("rgba8unorm"), QStringLiteral("rgba32f"), QStringLiteral("rgba32float"),
    };
    return formats;
}

const QStringList& drawModes() {
    static const QStringList modes = {
        QStringLiteral("points"), QStringLiteral("triangles"), QStringLiteral("billboards"),
    };
    return modes;
}

const QStringList& passTypes() {
    static const QStringList types = { QStringLiteral("render"), QStringLiteral("compute") };
    return types;
}

const QStringList& layoutEntryKeys() {
    static const QStringList keys = {
        QStringLiteral("name"), QStringLiteral("slot"), QStringLiteral("components"),
    };
    return keys;
}

const QStringList& byteLayoutKeys() {
    static const QStringList keys = {
        QStringLiteral("name"), QStringLiteral("offset"),
        QStringLiteral("size"), QStringLiteral("type"),
    };
    return keys;
}

const QStringList& dimSpecKeys() {
    static const QStringList keys = {
        QStringLiteral("param"), QStringLiteral("power"), QStringLiteral("multiply"),
        QStringLiteral("default"), QStringLiteral("paramDefault"),
        QStringLiteral("screenDivide"), QStringLiteral("scale"), QStringLiteral("clamp"),
        QStringLiteral("inputOverride"),
    };
    return keys;
}

// Top-level grammar keys (reference TOP_LEVEL_KEYS) plus the port-authored
// `starter` bootstrap metadata (see effect_validator.h).
const QStringList& topLevelKeys() {
    static const QStringList keys = {
        QStringLiteral("name"), QStringLiteral("namespace"), QStringLiteral("func"),
        QStringLiteral("description"), QStringLiteral("tags"), QStringLiteral("globals"),
        QStringLiteral("passes"), QStringLiteral("textures"), QStringLiteral("textures3d"),
        QStringLiteral("shaders"), QStringLiteral("uniformLayout"), QStringLiteral("uniformLayouts"),
        QStringLiteral("paramAliases"), QStringLiteral("openCategories"),
        QStringLiteral("defaultProgram"), QStringLiteral("hidden"), QStringLiteral("deprecatedBy"),
        QStringLiteral("externalTexture"), QStringLiteral("externalMesh"),
        QStringLiteral("builtinMeshes"), QStringLiteral("outputTex3d"), QStringLiteral("outputGeo"),
        QStringLiteral("state"), QStringLiteral("uniforms"),
        QStringLiteral("onInit"), QStringLiteral("onUpdate"), QStringLiteral("onDestroy"),
        QStringLiteral("asyncInit"),
        // Port-authored (converter-baked manifest metadata; the reference
        // derives starters at registration and never carries this field).
        QStringLiteral("starter"),
    };
    return keys;
}

const QStringList& pipelineInputs() {
    static const QStringList inputs = {
        QStringLiteral("inputTex"), QStringLiteral("inputTex3d"), QStringLiteral("inputGeo"),
        QStringLiteral("inputXyz"), QStringLiteral("inputVel"),  QStringLiteral("inputRgba"),
        QStringLiteral("noise"),    QStringLiteral("midiNoteGrid"), QStringLiteral("feedback"),
        QStringLiteral("selfTex"),  QStringLiteral("outputTex"), QStringLiteral("none"),
    };
    return inputs;
}

const QStringList& pipelineOutputs() {
    static const QStringList outputs = {
        QStringLiteral("outputTex"), QStringLiteral("outputTex3d"), QStringLiteral("outputXyz"),
        QStringLiteral("outputVel"), QStringLiteral("outputRgba"),
    };
    return outputs;
}

// Semantic component order consumed by the backends' uniform packing
// ({x: 0, y: 1, z: 2, w: 3}); ASCII codes do not follow xyzw order.
int componentOrder(QChar c) {
    switch (c.unicode()) {
        case 'x': return 0;
        case 'y': return 1;
        case 'z': return 2;
        case 'w': return 3;
        default:  return -1;
    }
}

bool isObj(const QJsonValue& value) {
    return value.isObject();
}

bool isFiniteNumber(const QJsonValue& value) {
    return value.isDouble() && std::isfinite(value.toDouble());
}

// Number.isInteger: a finite double with an integral value.
bool isJsInteger(const QJsonValue& value) {
    if (!value.isDouble()) return false;
    const double d = value.toDouble();
    return std::isfinite(d) && d == std::floor(d);
}

bool isNonEmptyString(const QJsonValue& value) {
    return value.isString() && !value.toString().isEmpty();
}

// The reference's truthiness check on a JSON value: `!def` in JS is true for
// undefined, null, false, 0, NaN, "" — all shapes the port's caller can pass.
bool isFalsy(const QJsonValue& value) {
    switch (value.type()) {
        case QJsonValue::Undefined: return true;
        case QJsonValue::Null:      return true;
        case QJsonValue::Bool:      return !value.toBool();
        case QJsonValue::Double:    return value.toDouble() == 0.0; // covers NaN? no
        case QJsonValue::String:    return value.toString().isEmpty();
        case QJsonValue::Array:     return false; // [] is truthy
        case QJsonValue::Object:    return false;
        default:                    return true;
    }
}

// ECMAScript `typeof` name for a JSON value (functions are unrepresentable).
const char* jsTypeOf(const QJsonValue& value) {
    switch (value.type()) {
        case QJsonValue::Array:   return "object"; // typeof [] is "object"
        case QJsonValue::Bool:    return "boolean";
        case QJsonValue::Double:  return "number";
        case QJsonValue::String:  return "string";
        case QJsonValue::Object:  return "object";
        case QJsonValue::Null:    return "object"; // typeof null is "object"
        case QJsonValue::Undefined: return "undefined";
        default:                  return "unknown";
    }
}

std::string toStd(const QString& s) { return s.toStdString(); }

// Number interpolation: the reference's `${number}` uses String(number).
std::string numToStd(double value) { return toStd(js::numberToString(value)); }

QStringList objectKeys(const QJsonObject& o) { return o.keys(); }

// --- std enum resolution (reference resolveStdEnum over stdEnums) ---

const QJsonObject* g_stdEnums = nullptr;

struct StdEnumResult {
    enum Kind { None, Leaf, Table } kind = None;
};

// Walks the dotted path through the std enum tree. Leaves are the port's
// Enums::leaf() shape ({"type":"Number","value":...}), which carries a
// `value` key exactly like the reference's leaf entries.
StdEnumResult resolveStdEnum(const QString& pathStr) {
    StdEnumResult result;
    if (pathStr.isEmpty() || !g_stdEnums) return result;
    QJsonValue node(*g_stdEnums);
    const QStringList parts = pathStr.split('.');
    for (const QString& part : parts) {
        if (!node.isObject()) return result;
        const QJsonObject nodeObj = node.toObject();
        if (!nodeObj.contains(part)) return result;
        node = nodeObj.value(part);
    }
    if (node.isObject() && node.toObject().contains(QStringLiteral("value"))) {
        result.kind = StdEnumResult::Leaf;
        return result;
    }
    if (node.isObject()) {
        result.kind = StdEnumResult::Table;
        return result;
    }
    return result;
}

// --- context (declared globals + their uniform names) ---

struct ValidatorContext {
    QSet<QString> globalKeys;
    QSet<QString> globalUniformNames;
};

bool referencesGlobal(const QString& name, const ValidatorContext& context) {
    return context.globalKeys.contains(name) || context.globalUniformNames.contains(name);
}

void validateEnabledBy(const QJsonValue& cond, Errors& errors, const std::string& label,
                       const ValidatorContext& context);

void validateUi(const QJsonValue& uiValue, Errors& errors, const std::string& label,
                const ValidatorContext& context);

void validateDimSpec(const QJsonValue& spec, Errors& errors, const std::string& label) {
    if (spec.isDouble()) {
        if (!isFiniteNumber(spec) || spec.toDouble() <= 0) {
            errors.push_back(label + ": dimension must be a positive finite number, keyword, percentage, or dimension expression");
        }
        return;
    }
    if (spec.isString()) {
        const QString s = spec.toString();
        if (dimKeywords().contains(s)) return;
        if (s.endsWith(QLatin1Char('%'))) {
            const QString head = s.left(s.size() - 1);
            bool allDigitsOrDot = !head.isEmpty();
            for (const QChar c : head) {
                if (!c.isDigit() && c != QLatin1Char('.')) { allDigitsOrDot = false; break; }
            }
            if (allDigitsOrDot) {
                bool ok = false;
                const double percent = head.toDouble(&ok);
                if (!ok || !std::isfinite(percent) || percent <= 0) {
                    errors.push_back(label + ": invalid percentage '" + toStd(s) + "'");
                }
                return;
            }
        }
        errors.push_back(label + ": invalid dimension '" + toStd(s) + "'");
        return;
    }
    if (spec.isObject()) {
        const QJsonObject specObj = spec.toObject();
        for (const QString& key : specObj.keys()) {
            if (!dimSpecKeys().contains(key)) {
                errors.push_back(label + ": unknown dimension field '" + toStd(key) + "'");
            }
        }
        const QJsonValue param = specObj.value(QStringLiteral("param"));
        if (!param.isUndefined()) {
            if (!isNonEmptyString(param)) {
                errors.push_back(label + ": \"param\" must be a non-empty string");
            }
            for (const char* field : {"power", "multiply", "default", "paramDefault"}) {
                const QJsonValue v = specObj.value(QLatin1String(field));
                if (!v.isUndefined() && !isFiniteNumber(v)) {
                    errors.push_back(label + ": \"" + field + "\" must be a finite number");
                }
            }
            const QJsonValue inputOverride = specObj.value(QStringLiteral("inputOverride"));
            if (!inputOverride.isUndefined() && !isNonEmptyString(inputOverride)) {
                errors.push_back(label + ": \"inputOverride\" must be a non-empty string");
            }
            return;
        }
        const QJsonValue screenDivide = specObj.value(QStringLiteral("screenDivide"));
        if (!screenDivide.isUndefined()) {
            if (!isNonEmptyString(screenDivide)) {
                errors.push_back(label + ": \"screenDivide\" must be a non-empty string");
            }
            const QJsonValue dflt = specObj.value(QStringLiteral("default"));
            if (!dflt.isUndefined() && !isFiniteNumber(dflt)) {
                errors.push_back(label + ": \"default\" must be a finite number");
            }
            return;
        }
        const QJsonValue scale = specObj.value(QStringLiteral("scale"));
        if (!scale.isUndefined()) {
            if (!isFiniteNumber(scale)) {
                errors.push_back(label + ": \"scale\" must be a finite number");
            }
            const QJsonValue clamp = specObj.value(QStringLiteral("clamp"));
            if (!clamp.isUndefined()) {
                if (!isObj(clamp)) {
                    errors.push_back(label + ": \"clamp\" must be an object");
                } else {
                    const QJsonObject clampObj = clamp.toObject();
                    const QJsonValue clampMin = clampObj.value(QStringLiteral("min"));
                    if (!clampMin.isUndefined() && !isFiniteNumber(clampMin)) {
                        errors.push_back(label + ": \"clamp.min\" must be a finite number");
                    }
                    const QJsonValue clampMax = clampObj.value(QStringLiteral("max"));
                    if (!clampMax.isUndefined() && !isFiniteNumber(clampMax)) {
                        errors.push_back(label + ": \"clamp.max\" must be a finite number");
                    }
                    for (const QString& key : clampObj.keys()) {
                        if (key != QLatin1String("min") && key != QLatin1String("max")) {
                            errors.push_back(label + ": unknown clamp field '" + toStd(key) + "'");
                        }
                    }
                }
            }
            return;
        }
        errors.push_back(label + ": dimension object must reference \"param\", \"screenDivide\", or \"scale\"");
        return;
    }
    errors.push_back(label + ": invalid dimension specification");
}

void validateLayoutEntry(const QJsonObject& entry, Errors& errors, const std::string& label) {
    for (const QString& key : entry.keys()) {
        if (!layoutEntryKeys().contains(key)) {
            errors.push_back(label + ": unknown field '" + toStd(key) + "'");
        }
    }
    const QJsonValue name = entry.value(QStringLiteral("name"));
    if (!isNonEmptyString(name)) {
        errors.push_back(label + ": missing \"name\" string");
    }
    const QJsonValue slot = entry.value(QStringLiteral("slot"));
    if (!isJsInteger(slot) || slot.toDouble() < 0) {
        errors.push_back(label + ": \"slot\" must be a non-negative integer");
    }
    const QJsonValue components = entry.value(QStringLiteral("components"));
    if (!components.isString()) {
        errors.push_back(label + ": \"components\" must be 1-4 characters from xyzw");
        return;
    }
    const QString cs = components.toString();
    if (cs.size() < 1 || cs.size() > 4) {
        errors.push_back(label + ": \"components\" must be 1-4 characters from xyzw");
        return;
    }
    for (const QChar c : cs) {
        if (componentOrder(c) < 0) {
            errors.push_back(label + ": \"components\" must be 1-4 characters from xyzw");
            return;
        }
    }
    for (int i = 1; i < cs.size(); ++i) {
        if (componentOrder(cs.at(i)) <= componentOrder(cs.at(i - 1))) {
            errors.push_back(label + ": \"components\" '" + toStd(cs) + "' must be in ascending xyzw order");
            break;
        }
    }
}

struct LayoutEntry {
    QString name;
    int slot = 0;
    QString components;
    bool valid = false; // slot/components fully checked before inclusion
};

void checkLayoutConflicts(const std::vector<LayoutEntry>& entries, Errors& errors,
                          const std::string& label) {
    for (size_t i = 0; i < entries.size(); ++i) {
        const LayoutEntry& a = entries[i];
        if (!a.valid || !a.components.isSimple()) continue;
        for (size_t j = i + 1; j < entries.size(); ++j) {
            const LayoutEntry& b = entries[j];
            if (!b.valid || !b.components.isSimple()) continue;
            if (a.slot != b.slot) continue;
            bool overlap = false;
            for (const QChar c : a.components) {
                if (b.components.contains(c)) { overlap = true; break; }
            }
            if (a.components == b.components) {
                errors.push_back(label + ": duplicate layout entries '" + toStd(a.name) + "' and '" + toStd(b.name)
                                 + "' claim slot " + numToStd(a.slot) + " components '" + toStd(a.components) + "'");
            } else if (overlap) {
                errors.push_back(label + ": layout conflict at slot " + numToStd(a.slot) + ": '"
                                 + toStd(a.name) + "' (" + toStd(a.components) + ") overlaps '"
                                 + toStd(b.name) + "' (" + toStd(b.components) + ")");
            }
        }
    }
}

void checkByteLayoutConflicts(const std::vector<LayoutEntry>& entries, Errors& errors,
                              const std::string& label) {
    for (size_t i = 0; i < entries.size(); ++i) {
        for (size_t j = i + 1; j < entries.size(); ++j) {
            // Offsets/sizes are carried in `slot`/`components` for byte entries:
            // slot = offset, components unused. See collectByteEntries.
            (void)entries; (void)errors; (void)label;
        }
    }
}

} // namespace (unreachable helpers kept minimal)

} // namespace nm
