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
#include <QSet>
#include <QStringList>

#include <cmath>
#include <map>
#include <set>
#include <vector>

namespace nm {

namespace {

using Errors = std::vector<std::string>;

// --- tag inventory (shaders/src/runtime/tags.js VALID_TAGS, in declaration
// order) ---
const QStringList& validTags() {
    static const QStringList tags = {
        QStringLiteral("color"),  QStringLiteral("distort"),      QStringLiteral("edges"),
        QStringLiteral("geometric"), QStringLiteral("lens"),      QStringLiteral("noise"),
        QStringLiteral("transform"), QStringLiteral("util"),      QStringLiteral("sim"),
        QStringLiteral("3d"),     QStringLiteral("audio"),        QStringLiteral("agents"),
        QStringLiteral("antialiasing"), QStringLiteral("artist"), QStringLiteral("blend"),
        QStringLiteral("blur"),   QStringLiteral("fractal"),      QStringLiteral("geometry"),
        QStringLiteral("glitch"), QStringLiteral("image"),        QStringLiteral("mesh"),
        QStringLiteral("midi"),   QStringLiteral("palette"),      QStringLiteral("pattern"),
        QStringLiteral("pixel"),  QStringLiteral("text"),         QStringLiteral("tiling"),
        QStringLiteral("video"),
    };
    return tags;
}

const QStringList& globalTypes() {
    static const QStringList types = {
        QStringLiteral("float"), QStringLiteral("int"),     QStringLiteral("boolean"),
        QStringLiteral("vec2"),  QStringLiteral("vec3"),   QStringLiteral("vec4"),
        QStringLiteral("mat3"),  QStringLiteral("color"),  QStringLiteral("surface"),
        QStringLiteral("volume"), QStringLiteral("geometry"), QStringLiteral("member"),
        QStringLiteral("palette"), QStringLiteral("button"), QStringLiteral("string"),
    };
    return types;
}

const QStringList& uiControls() {
    static const QStringList controls = {
        QStringLiteral("slider"), QStringLiteral("checkbox"), QStringLiteral("dropdown"),
        QStringLiteral("color"),  QStringLiteral("button"),   QStringLiteral("vector3"),
        QStringLiteral("vec3"),
    };
    return controls;
}

const QStringList& uiKeys() {
    static const QStringList keys = {
        QStringLiteral("label"), QStringLiteral("control"),   QStringLiteral("category"),
        QStringLiteral("hidden"), QStringLiteral("hint"),    QStringLiteral("format"),
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

// The reference's `!def` falsy check. QJsonValue cannot represent NaN, so the
// JS-only NaN case is structurally impossible here; every representable falsy
// shape (undefined, null, false, 0, "") is covered.
bool isFalsy(const QJsonValue& value) {
    switch (value.type()) {
        case QJsonValue::Undefined: return true;
        case QJsonValue::Null:      return true;
        case QJsonValue::Bool:      return !value.toBool();
        case QJsonValue::Double:    return value.toDouble() == 0.0;
        case QJsonValue::String:    return value.toString().isEmpty();
        case QJsonValue::Array:     return false; // [] is truthy
        case QJsonValue::Object:    return false;
        default:                    return true;
    }
}

// ECMAScript `typeof` name for a JSON value. JSON cannot represent functions,
// so the reference's `typeof def !== 'function'` input branch is unreachable
// in the port (see effect_validator.h).
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

// JS String(value) over a JSON value (the reference's `${expr}` interpolation
// where the operand is not statically known to be a string).
std::string jsStringOf(const QJsonValue& value) {
    if (value.isString()) return toStd(value.toString());
    if (value.isDouble()) return numToStd(value.toDouble());
    if (value.isBool()) return value.toBool() ? "true" : "false";
    if (value.isNull()) return "null";
    if (value.isUndefined()) return "undefined";
    if (value.isArray()) {
        // String(array) is Array.prototype.join: null and undefined elements
        // render as the empty string, other elements via String(element).
        const QJsonArray array = value.toArray();
        std::string joined;
        for (const QJsonValue& element : array) {
            if (!joined.empty()) joined += ",";
            if (!element.isNull() && !element.isUndefined()) joined += jsStringOf(element);
        }
        return joined;
    }
    return "[object Object]";
}

// --- std enum resolution (reference resolveStdEnum over std_enums.js) ---

// Injectable std-enum tree. Defaults to the live nm::Enums::std() table on
// first use (the reference imports std_enums.js directly).
QJsonObject const* g_stdEnumsOverride = nullptr;

const QJsonObject& stdEnumTable() {
    static const QJsonObject defaultTable = Enums().std();
    return g_stdEnumsOverride ? *g_stdEnumsOverride : defaultTable;
}

struct StdEnumResult {
    enum Kind { None, Leaf, Table } kind = None;
};

// Walks the dotted path through the std enum tree. Leaves carry a `value` key
// exactly like the reference's {type:'Number', value} entries (the port's
// Enums::leaf() shape, see enums.h).
StdEnumResult resolveStdEnum(const QString& pathStr) {
    StdEnumResult result;
    if (pathStr.isEmpty()) return result;
    QJsonValue node(stdEnumTable());
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

// Forward declarations for the mutually recursive ui/enabledBy pair.
void validateEnabledBy(const QJsonValue& cond, Errors& errors, const std::string& label,
                       const ValidatorContext& context);
void validateUi(const QJsonValue& uiValue, Errors& errors, const std::string& label,
                const ValidatorContext& context);

// --- validateDimSpec: reference lines 117-200 ---
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
        // PERCENT_PATTERN ^[\d.]+$ on the head before '%'; parseFloat over the
        // whole spec then re-checks finiteness/positivity.
        if (s.endsWith(QLatin1Char('%'))) {
            const QString head = s.left(s.size() - 1);
            bool percentPattern = !head.isEmpty();
            for (const QChar c : head) {
                if (!c.isDigit() && c != QLatin1Char('.')) { percentPattern = false; break; }
            }
            if (percentPattern) {
                // parseFloat(s) stops at the '%'; the head matched ^[\d.]+$, so
                // parseFloat parses the longest numeric prefix of the head
                // (e.g. "0.5.3%" -> 0.5, NaN cases can't occur here).
                double percent = 0;
                bool ok = false;
                for (int cut = head.size(); cut > 0 && !ok; --cut) {
                    percent = head.left(cut).toDouble(&ok);
                }
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
        if (!specObj.value(QStringLiteral("param")).isUndefined()) {
            const QJsonValue param = specObj.value(QStringLiteral("param"));
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
        if (!specObj.value(QStringLiteral("screenDivide")).isUndefined()) {
            const QJsonValue screenDivide = specObj.value(QStringLiteral("screenDivide"));
            if (!isNonEmptyString(screenDivide)) {
                errors.push_back(label + ": \"screenDivide\" must be a non-empty string");
            }
            const QJsonValue dflt = specObj.value(QStringLiteral("default"));
            if (!dflt.isUndefined() && !isFiniteNumber(dflt)) {
                errors.push_back(label + ": \"default\" must be a finite number");
            }
            return;
        }
        if (!specObj.value(QStringLiteral("scale")).isUndefined()) {
            const QJsonValue scale = specObj.value(QStringLiteral("scale"));
            if (!isFiniteNumber(scale)) {
                errors.push_back(label + ": \"scale\" must be a finite number");
            }
            const QJsonValue clamp = specObj.value(QStringLiteral("clamp"));
            if (!clamp.isUndefined()) {
                if (!clamp.isObject()) {
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

// --- validateLayoutEntry: reference lines 285-311 ---
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
    // ^[xyzw]{1,4}$
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

// A normalized slot/components claim used by checkLayoutConflicts.
struct SlotClaim {
    QString name;
    int slot = 0;
    QString components;
};

void checkLayoutConflicts(const std::vector<SlotClaim>& entries, Errors& errors,
                          const std::string& label) {
    for (size_t i = 0; i < entries.size(); ++i) {
        for (size_t j = i + 1; j < entries.size(); ++j) {
            const SlotClaim& a = entries[i];
            const SlotClaim& b = entries[j];
            if (a.slot != b.slot) continue;
            bool overlap = false;
            for (const QChar c : a.components) {
                if (b.components.contains(c)) { overlap = true; break; }
            }
            if (a.components == b.components) {
                errors.push_back(label + ": duplicate layout entries '" + toStd(a.name) + "' and '"
                                 + toStd(b.name) + "' claim slot " + numToStd(a.slot)
                                 + " components '" + toStd(a.components) + "'");
            } else if (overlap) {
                errors.push_back(label + ": layout conflict at slot " + numToStd(a.slot) + ": '"
                                 + toStd(a.name) + "' (" + toStd(a.components) + ") overlaps '"
                                 + toStd(b.name) + "' (" + toStd(b.components) + ")");
            }
        }
    }
}

// --- validateUniformLayout: reference lines 208-279 ---
void validateUniformLayout(const QJsonValue& layout, Errors& errors, const std::string& label) {
    if (layout.isArray()) {
        const QJsonArray arr = layout.toArray();
        std::vector<SlotClaim> entries;
        for (int i = 0; i < arr.size(); ++i) {
            const QJsonValue entry = arr.at(i);
            const std::string entryLabel = label + "[" + numToStd(i) + "]";
            if (entry.isObject()) {
                const QJsonObject obj = entry.toObject();
                validateLayoutEntry(obj, errors, entryLabel);
                const QJsonValue slot = obj.value(QStringLiteral("slot"));
                if (isJsInteger(slot)) {
                    SlotClaim claim;
                    claim.name = obj.value(QStringLiteral("name")).toString();
                    claim.slot = static_cast<int>(slot.toDouble());
                    claim.components = obj.value(QStringLiteral("components")).toString();
                    entries.push_back(claim);
                }
            } else {
                // validateLayoutEntry's non-object message.
                errors.push_back(entryLabel + ": layout entry must be an object");
            }
        }
        checkLayoutConflicts(entries, errors, label);
        return;
    }
    if (!layout.isObject()) {
        errors.push_back(label + ": must be an object or array layout");
        return;
    }
    const QJsonObject layoutObj = layout.toObject();
    if (layoutObj.value(QStringLiteral("type")) == QLatin1String("byte")) {
        const QJsonValue inner = layoutObj.value(QStringLiteral("layout"));
        if (!inner.isArray()) {
            errors.push_back(label + ": byte layout requires a \"layout\" array");
            return;
        }
        for (const QString& key : layoutObj.keys()) {
            if (key != QLatin1String("type") && key != QLatin1String("layout")) {
                errors.push_back(label + ": unknown byte-layout field '" + toStd(key) + "'");
            }
        }
        const QJsonArray arr = inner.toArray();
        for (int i = 0; i < arr.size(); ++i) {
            const QJsonValue entry = arr.at(i);
            const std::string entryLabel = label + ".layout[" + numToStd(i) + "]";
            if (!entry.isObject()) {
                errors.push_back(entryLabel + ": entry must be an object");
                continue;
            }
            const QJsonObject obj = entry.toObject();
            for (const QString& key : obj.keys()) {
                if (!byteLayoutKeys().contains(key) && key != QLatin1String("components")) {
                    errors.push_back(entryLabel + ": unknown field '" + toStd(key) + "'");
                }
            }
            const QJsonValue name = obj.value(QStringLiteral("name"));
            if (!isNonEmptyString(name)) {
                errors.push_back(entryLabel + ": missing \"name\" string");
            }
            const QJsonValue offset = obj.value(QStringLiteral("offset"));
            if (!isJsInteger(offset) || offset.toDouble() < 0) {
                errors.push_back(entryLabel + ": \"offset\" must be a non-negative integer");
            }
            const QJsonValue size = obj.value(QStringLiteral("size"));
            if (!isJsInteger(size) || size.toDouble() <= 0) {
                errors.push_back(entryLabel + ": \"size\" must be a positive integer");
            }
            const QJsonValue type = obj.value(QStringLiteral("type"));
            if (!isNonEmptyString(type)) {
                errors.push_back(entryLabel + ": missing \"type\" string");
            }
        }
        // Byte-layout duplicate/overlap diagnosis over fully valid entries
        // (reference checkByteLayoutConflicts).
        for (int i = 0; i < arr.size(); ++i) {
            if (!arr.at(i).isObject()) continue;
            const QJsonObject a = arr.at(i).toObject();
            const QJsonValue aName = a.value(QStringLiteral("name"));
            const QJsonValue aOffset = a.value(QStringLiteral("offset"));
            const QJsonValue aSize = a.value(QStringLiteral("size"));
            if (!isNonEmptyString(aName) || !isJsInteger(aOffset) || aOffset.toDouble() < 0 ||
                !isJsInteger(aSize) || aSize.toDouble() <= 0) continue;
            for (int j = i + 1; j < arr.size(); ++j) {
                if (!arr.at(j).isObject()) continue;
                const QJsonObject b = arr.at(j).toObject();
                const QJsonValue bName = b.value(QStringLiteral("name"));
                const QJsonValue bOffset = b.value(QStringLiteral("offset"));
                const QJsonValue bSize = b.value(QStringLiteral("size"));
                if (!isNonEmptyString(bName) || !isJsInteger(bOffset) || bOffset.toDouble() < 0 ||
                    !isJsInteger(bSize) || bSize.toDouble() <= 0) continue;
                if (aName.toString() == bName.toString()) {
                    errors.push_back(label + ": duplicate byte-layout entries '" + toStd(aName.toString())
                                     + "' (offsets " + numToStd(aOffset.toDouble()) + " and "
                                     + numToStd(bOffset.toDouble()) + ")");
                    continue;
                }
                const double aStart = aOffset.toDouble();
                const double aEnd = aStart + aSize.toDouble();
                const double bStart = bOffset.toDouble();
                const double bEnd = bStart + bSize.toDouble();
                if (aStart < bEnd && bStart < aEnd) {
                    errors.push_back(label + ": byte layout conflict: '" + toStd(aName.toString())
                                     + "' (offset " + numToStd(aStart) + ", size " + numToStd(aSize.toDouble())
                                     + ") overlaps '" + toStd(bName.toString()) + "' (offset "
                                     + numToStd(bStart) + ", size " + numToStd(bSize.toDouble()) + ")");
                }
            }
        }
        return;
    }
    // Named-key map form: validateLayoutEntry over {name, ...spec}.
    std::vector<SlotClaim> entries;
    for (auto it = layoutObj.begin(); it != layoutObj.end(); ++it) {
        const std::string entryLabel = label + "['" + toStd(it.key()) + "']";
        if (!it.value().isObject()) {
            errors.push_back(entryLabel + ": layout entry must be an object");
            continue;
        }
        QJsonObject spec = it.value().toObject();
        spec.insert(QStringLiteral("name"), it.key());
        validateLayoutEntry(spec, errors, entryLabel);
        const QJsonValue slot = it.value().toObject().value(QStringLiteral("slot"));
        if (isJsInteger(slot)) {
            SlotClaim claim;
            claim.name = it.key();
            claim.slot = static_cast<int>(slot.toDouble());
            claim.components = it.value().toObject().value(QStringLiteral("components")).toString();
            entries.push_back(claim);
        }
    }
    checkLayoutConflicts(entries, errors, label);
}

// --- validateEnabledBy: reference lines 355-406 ---
void validateEnabledBy(const QJsonValue& cond, Errors& errors, const std::string& label,
                       const ValidatorContext& context) {
    if (cond.isString()) {
        const QString s = cond.toString();
        if (!context.globalKeys.contains(s)) {
            errors.push_back(label + ": enabledBy references unknown global '" + toStd(s) + "'");
        }
        return;
    }
    if (!cond.isObject()) {
        errors.push_back(label + ": \"enabledBy\" must be a global name or condition object");
        return;
    }
    const QJsonObject obj = cond.toObject();
    if (!obj.value(QStringLiteral("and")).isUndefined() ||
        !obj.value(QStringLiteral("or")).isUndefined()) {
        for (const QString& key : obj.keys()) {
            if (key != QLatin1String("and") && key != QLatin1String("or")) {
                errors.push_back(label + ": unknown enabledBy field '" + toStd(key) + "'");
            }
        }
        for (const char* branch : {"and", "or"}) {
            const QJsonValue v = obj.value(QLatin1String(branch));
            if (!v.isUndefined()) {
                if (!v.isArray()) {
                    errors.push_back(label + ": \"enabledBy." + branch + "\" must be an array");
                } else {
                    for (const QJsonValue& sub : v.toArray()) {
                        validateEnabledBy(sub, errors, label, context);
                    }
                }
            }
        }
        return;
    }
    for (const QString& key : obj.keys()) {
        if (key != QLatin1String("param") && !enabledByOps().contains(key)) {
            errors.push_back(label + ": unknown enabledBy field '" + toStd(key) + "'");
        }
    }
    const QJsonValue param = obj.value(QStringLiteral("param"));
    if (!isNonEmptyString(param)) {
        errors.push_back(label + ": \"enabledBy\" requires a \"param\" string");
        return;
    }
    if (!context.globalKeys.contains(param.toString())) {
        errors.push_back(label + ": enabledBy references unknown global '" + toStd(param.toString()) + "'");
    }
    bool hasOp = false;
    for (const QString& op : enabledByOps()) {
        if (!obj.value(op).isUndefined()) { hasOp = true; break; }
    }
    if (!hasOp) {
        errors.push_back(label + ": \"enabledBy\" requires one of eq/neq/lt/gt/in/notIn");
    }
    const QJsonValue in = obj.value(QStringLiteral("in"));
    if (!in.isUndefined() && !in.isArray()) {
        errors.push_back(label + ": \"enabledBy.in\" must be an array");
    }
    const QJsonValue notIn = obj.value(QStringLiteral("notIn"));
    if (!notIn.isUndefined() && !notIn.isArray()) {
        errors.push_back(label + ": \"enabledBy.notIn\" must be an array");
    }
}

// --- validateUi: reference lines 408-441 ---
void validateUi(const QJsonValue& uiValue, Errors& errors, const std::string& label,
                const ValidatorContext& context) {
    if (!uiValue.isObject()) {
        errors.push_back(label + ": must be an object");
        return;
    }
    const QJsonObject ui = uiValue.toObject();
    for (const QString& key : ui.keys()) {
        if (!uiKeys().contains(key)) {
            errors.push_back(label + ": unknown field '" + toStd(key) + "'");
        }
    }
    const QJsonValue labelV = ui.value(QStringLiteral("label"));
    if (!labelV.isUndefined() && !isNonEmptyString(labelV)) {
        errors.push_back(label + ": \"label\" must be a non-empty string");
    }
    const QJsonValue control = ui.value(QStringLiteral("control"));
    if (!control.isUndefined() && !(control.isBool() && !control.toBool()) &&
        !uiControls().contains(control.toString())) {
        errors.push_back(label + ": unknown control '" + toStd(control.toString()) + "'");
    }
    const QJsonValue category = ui.value(QStringLiteral("category"));
    if (!category.isUndefined() && !isNonEmptyString(category)) {
        errors.push_back(label + ": \"category\" must be a non-empty string");
    }
    const QJsonValue hidden = ui.value(QStringLiteral("hidden"));
    if (!hidden.isUndefined() && !hidden.isBool()) {
        errors.push_back(label + ": \"hidden\" must be a boolean");
    }
    const QJsonValue multiline = ui.value(QStringLiteral("multiline"));
    if (!multiline.isUndefined() && !multiline.isBool()) {
        errors.push_back(label + ": \"multiline\" must be a boolean");
    }
    for (const char* key : {"hint", "format", "buttonLabel"}) {
        const QJsonValue v = ui.value(QLatin1String(key));
        if (!v.isUndefined() && !isNonEmptyString(v)) {
            errors.push_back(label + ": \"" + key + "\" must be a non-empty string");
        }
    }
    const QJsonValue enabledBy = ui.value(QStringLiteral("enabledBy"));
    if (!enabledBy.isUndefined()) {
        validateEnabledBy(enabledBy, errors, label, context);
    }
}

// HEX_COLOR ^#[0-9a-fA-F]{6}$
bool isHexColor(const QString& s) {
    if (s.size() != 7 || s.at(0) != QLatin1Char('#')) return false;
    for (int i = 1; i < 7; ++i) {
        const char16_t c = s.at(i).unicode();
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    return true;
}

// --- validateDefault: reference lines 443-500 ---
void validateDefault(const QJsonObject& spec, Errors& errors, const std::string& label) {
    const QJsonValue typeV = spec.value(QStringLiteral("type"));
    const QString type = typeV.isString() ? typeV.toString() : QString();
    const QJsonValue value = spec.value(QStringLiteral("default"));

    if (type == QLatin1String("float") || type == QLatin1String("palette") ||
        type == QLatin1String("button")) {
        if (!isFiniteNumber(value)) {
            errors.push_back(label + ": \"default\" must be a finite number");
        }
    } else if (type == QLatin1String("int")) {
        if (!isFiniteNumber(value) || !isJsInteger(value)) {
            errors.push_back(label + ": \"default\" must be a finite integer");
        }
    } else if (type == QLatin1String("boolean")) {
        if (!value.isBool()) {
            errors.push_back(label + ": \"default\" must be a boolean");
        }
    } else if (type == QLatin1String("vec2") || type == QLatin1String("vec3") ||
               type == QLatin1String("vec4") || type == QLatin1String("mat3")) {
        const int dims = type == QLatin1String("vec2") ? 2 : type == QLatin1String("vec3") ? 3
                       : type == QLatin1String("vec4") ? 4 : 9;
        bool ok = value.isArray();
        if (ok) {
            const QJsonArray arr = value.toArray();
            ok = arr.size() == dims;
            for (const QJsonValue& v : arr) {
                if (!isFiniteNumber(v)) { ok = false; break; }
            }
        }
        if (!ok) {
            errors.push_back(label + ": \"default\" must be an array of " + std::to_string(dims)
                             + " finite numbers");
        }
    } else if (type == QLatin1String("color")) {
        if (value.isArray()) {
            const QJsonArray arr = value.toArray();
            bool ok = arr.size() == 3;
            for (const QJsonValue& v : arr) {
                if (!isFiniteNumber(v)) { ok = false; break; }
            }
            if (!ok) {
                errors.push_back(label + ": \"default\" must be a 3-component color array");
            }
        } else if (!isNonEmptyString(value) || !isHexColor(value.toString())) {
            errors.push_back(label + ": \"default\" must be a 3-component color array or '#rrggbb' string");
        }
    } else if (type == QLatin1String("string")) {
        if (!value.isString()) {
            errors.push_back(label + ": \"default\" must be a string");
        }
    } else if (type == QLatin1String("surface") || type == QLatin1String("volume") ||
               type == QLatin1String("geometry") || type == QLatin1String("member")) {
        if (!value.isString()) {
            errors.push_back(label + ": \"default\" must be a string");
        } else if (type == QLatin1String("member")) {
            const StdEnumResult resolved = resolveStdEnum(value.toString());
            if (resolved.kind != StdEnumResult::Leaf) {
                errors.push_back(label + ": \"default\" '" + toStd(value.toString())
                                 + "' does not resolve to a std enum value");
            }
        }
    }
    // Unknown type already reported separately (validateGlobals).
}

// rangeDims: reference lines 507-513.
int rangeDims(const QString& type) {
    if (type == QLatin1String("vec2")) return 2;
    if (type == QLatin1String("vec3") || type == QLatin1String("color")) return 3;
    if (type == QLatin1String("vec4")) return 4;
    if (type == QLatin1String("mat3")) return 9;
    return 1;
}

// --- validateRangeBounds: reference lines 522-576 ---
void validateRangeBounds(const QJsonObject& spec, Errors& errors, const std::string& label) {
    const QJsonValue typeV = spec.value(QStringLiteral("type"));
    const QString type = typeV.isString() ? typeV.toString() : QString();
    const int dims = rangeDims(type);

    // min/max bounds: scalar broadcasts, arrays must match dims exactly.
    bool haveMin = false, haveMax = false;
    std::vector<double> minVals, maxVals;
    for (const char* field : {"min", "max"}) {
        const QJsonValue value = spec.value(QLatin1String(field));
        if (value.isUndefined()) continue;
        if (isFiniteNumber(value)) {
            if (field[0] == 'm' && field[1] == 'i') { haveMin = true; minVals = {value.toDouble()}; }
            else { haveMax = true; maxVals = {value.toDouble()}; }
        } else if (value.isArray()) {
            const QJsonArray arr = value.toArray();
            bool allFinite = true;
            for (const QJsonValue& v : arr) {
                if (!isFiniteNumber(v)) { allFinite = false; break; }
            }
            if (static_cast<int>(arr.size()) == dims && allFinite) {
                std::vector<double> vals;
                for (const QJsonValue& v : arr) vals.push_back(v.toDouble());
                if (field[0] == 'm' && field[1] == 'i') { haveMin = true; minVals = vals; }
                else { haveMax = true; maxVals = vals; }
            } else {
                errors.push_back(label + ": \"" + field + "\" must be an array of " + std::to_string(dims)
                                 + " finite numbers for type '"
                                 + (type.isEmpty() ? "unknown" : toStd(type)) + "'");
            }
        } else {
            errors.push_back(label + ": \"" + field + "\" must be a finite number or an array of "
                             + std::to_string(dims) + " finite numbers");
        }
    }

    if (haveMin && haveMax) {
        const bool sameForm = (minVals.size() == 1) == (maxVals.size() == 1);
        if (!sameForm) {
            errors.push_back(label + ": \"min\" and \"max\" must both be scalars or both be arrays");
        } else {
            for (size_t i = 0; i < minVals.size(); ++i) {
                if (minVals[i] > maxVals[i]) {
                    errors.push_back(label + ": \"min\" must not exceed \"max\"");
                    break;
                }
            }
        }
    }

    // Default containment: broadcast scalar bounds, compare componentwise.
    const QJsonValue dflt = spec.value(QStringLiteral("default"));
    if (dflt.isArray() && haveMin && haveMax) {
        const QJsonArray arr = dflt.toArray();
        bool allFinite = true;
        for (const QJsonValue& v : arr) {
            if (!isFiniteNumber(v)) { allFinite = false; break; }
        }
        if (allFinite) {
            for (int i = 0; i < arr.size(); ++i) {
                const double mn = minVals.size() == 1 ? minVals[0] : minVals[i];
                const double mx = maxVals.size() == 1 ? maxVals[0] : maxVals[i];
                if (arr.at(i).toDouble() < mn || arr.at(i).toDouble() > mx) {
                    QStringList parts;
                    for (const QJsonValue& v : arr) parts << js::numberToString(v.toDouble());
                    errors.push_back(label + ": default [" + toStd(parts.join(QStringLiteral(", ")))
                                     + "] is outside the declared range");
                    break;
                }
            }
        }
    } else if (isFiniteNumber(dflt) && haveMin && haveMax &&
               minVals.size() == 1 && maxVals.size() == 1) {
        if (dflt.toDouble() < minVals[0] || dflt.toDouble() > maxVals[0]) {
            errors.push_back(label + ": default " + numToStd(dflt.toDouble()) + " is outside the declared range ["
                             + numToStd(minVals[0]) + ", " + numToStd(maxVals[0]) + "]");
        }
    }
}

// --- validateGlobals: reference lines 578-688 ---
void validateGlobals(const QJsonValue& globals, Errors& errors, const ValidatorContext& context) {
    if (globals.isUndefined() || globals.isNull()) {
        return;
    }
    if (!globals.isObject()) {
        errors.push_back("\"globals\" must be an object");
        return;
    }
    const QJsonObject globalsObj = globals.toObject();

    std::map<std::string, std::string> uniformOwners; // uniform name -> global key
    for (auto it = globalsObj.begin(); it != globalsObj.end(); ++it) {
        const std::string key = toStd(it.key());
        const std::string label = "Global '" + key + "'";
        if (!it.value().isObject()) {
            errors.push_back(label + ": must be an object");
            continue;
        }
        const QJsonObject spec = it.value().toObject();

        for (const QString& field : spec.keys()) {
            if (!globalSpecKeys().contains(field)) {
                errors.push_back(label + ": unknown field '" + toStd(field) + "'");
            }
        }

        const QJsonValue typeV = spec.value(QStringLiteral("type"));
        if (typeV.isUndefined() || typeV.isNull() ||
            (typeV.isBool() && !typeV.toBool()) ||
            (typeV.isString() && typeV.toString().isEmpty()) ||
            (typeV.isDouble() && typeV.toDouble() == 0.0)) {
            errors.push_back(label + ": Missing \"type\"");
        } else if (!typeV.isString() || !globalTypes().contains(typeV.toString())) {
            // String(spec.type) over a JSON value.
            std::string typeStr;
            if (typeV.isString()) typeStr = toStd(typeV.toString());
            else if (typeV.isDouble()) typeStr = numToStd(typeV.toDouble());
            else if (typeV.isBool()) typeStr = typeV.toBool() ? "true" : "false";
            else if (typeV.isNull()) typeStr = "null";
            else typeStr = "[object Object]";
            errors.push_back(label + ": Unknown type '" + typeStr + "'");
        }

        if (!spec.value(QStringLiteral("default")).isUndefined()) {
            validateDefault(spec, errors, label);
        }

        if (!spec.value(QStringLiteral("min")).isUndefined() ||
            !spec.value(QStringLiteral("max")).isUndefined()) {
            validateRangeBounds(spec, errors, label);
        }

        for (const char* field : {"step", "zero", "randMin", "randMax", "randChance"}) {
            const QJsonValue v = spec.value(QLatin1String(field));
            if (!v.isUndefined() && !isFiniteNumber(v)) {
                errors.push_back(label + ": \"" + field + "\" must be a finite number");
            }
        }
        const QJsonValue randChoices = spec.value(QStringLiteral("randChoices"));
        if (!randChoices.isUndefined()) {
            bool ok = randChoices.isArray();
            if (ok) {
                for (const QJsonValue& v : randChoices.toArray()) {
                    if (!isFiniteNumber(v)) { ok = false; break; }
                }
            }
            if (!ok) {
                errors.push_back(label + ": \"randChoices\" must be an array of finite numbers");
            }
        }

        const QJsonValue uniform = spec.value(QStringLiteral("uniform"));
        if (!uniform.isUndefined() && !isNonEmptyString(uniform)) {
            errors.push_back(label + ": \"uniform\" must be a non-empty string");
        } else if (isNonEmptyString(uniform)) {
            const std::string uniformName = toStd(uniform.toString());
            const auto owner = uniformOwners.find(uniformName);
            if (owner != uniformOwners.end()) {
                errors.push_back(label + ": uniform '" + uniformName + "' conflicts with global '"
                                 + owner->second + "'");
            } else {
                uniformOwners.emplace(uniformName, key);
            }
        }

        const QJsonValue define = spec.value(QStringLiteral("define"));
        if (!define.isUndefined() && !isNonEmptyString(define)) {
            errors.push_back(label + ": \"define\" must be a non-empty string");
        }

        const QJsonValue colorModeUniform = spec.value(QStringLiteral("colorModeUniform"));
        if (!colorModeUniform.isUndefined() && !isNonEmptyString(colorModeUniform)) {
            errors.push_back(label + ": \"colorModeUniform\" must be a non-empty string");
        }

        const QJsonValue choices = spec.value(QStringLiteral("choices"));
        if (!choices.isUndefined()) {
            if (!choices.isObject()) {
                errors.push_back(label + ": \"choices\" must be an object mapping names to values");
            } else {
                const QJsonObject choicesObj = choices.toObject();
                std::vector<double> numeric;
                const bool stringType = typeV.toString() == QLatin1String("string");
                for (auto cit = choicesObj.begin(); cit != choicesObj.end(); ++cit) {
                    const QJsonValue value = cit.value();
                    if (value.isNull()) continue; // Section headers in dropdown menus.
                    if (stringType) {
                        if (!value.isString()) {
                            errors.push_back(label + ": choices['" + toStd(cit.key())
                                             + "'] must be a string for type 'string'");
                        }
                        continue;
                    }
                    if (!isFiniteNumber(value)) {
                        errors.push_back(label + ": choices['" + toStd(cit.key())
                                         + "'] must be a number or null");
                    } else {
                        numeric.push_back(value.toDouble());
                    }
                }
                const QJsonValue dflt = spec.value(QStringLiteral("default"));
                if (!numeric.empty() && isFiniteNumber(dflt)) {
                    bool found = false;
                    for (const double v : numeric) {
                        if (v == dflt.toDouble()) { found = true; break; }
                    }
                    if (!found) {
                        errors.push_back(label + ": default " + numToStd(dflt.toDouble())
                                         + " is not among the declared choice values");
                    }
                }
            }
        }

        const QJsonValue enumPath = spec.value(QStringLiteral("enum"));
        if (!enumPath.isUndefined()) {
            if (!isNonEmptyString(enumPath)) {
                errors.push_back(label + ": \"enum\" must be a non-empty string");
            } else {
                const StdEnumResult resolved = resolveStdEnum(enumPath.toString());
                if (resolved.kind != StdEnumResult::Table) {
                    errors.push_back(label + ": enum '" + toStd(enumPath.toString())
                                     + "' does not resolve to a std enum table");
                }
            }
        }

        const QJsonValue ui = spec.value(QStringLiteral("ui"));
        if (!ui.isUndefined()) {
            validateUi(ui, errors, label + ".ui", context);
        }
    }
}

// --- validateTextureMap: reference lines 690-726 ---
void validateTextureMap(const QJsonValue& textures, Errors& errors, const std::string& containerName) {
    if (textures.isUndefined()) {
        return;
    }
    if (!textures.isObject()) {
        errors.push_back("\"" + containerName + "\" must be an object");
        return;
    }
    const QJsonObject texturesObj = textures.toObject();
    for (auto it = texturesObj.begin(); it != texturesObj.end(); ++it) {
        const std::string label = "Texture '" + toStd(it.key()) + "'";
        if (!it.value().isObject()) {
            errors.push_back(label + ": must be an object");
            continue;
        }
        const QJsonObject spec = it.value().toObject();
        for (const QString& key : spec.keys()) {
            if (!textureSpecKeys().contains(key)) {
                errors.push_back(label + ": unknown field '" + toStd(key) + "'");
            }
        }
        for (const char* dim : {"width", "height"}) {
            const QJsonValue v = spec.value(QLatin1String(dim));
            if (!v.isUndefined()) {
                validateDimSpec(v, errors, label + "." + dim);
            }
        }
        const QJsonValue depth = spec.value(QStringLiteral("depth"));
        if (!depth.isUndefined() && (!isFiniteNumber(depth) || depth.toDouble() <= 0)) {
            errors.push_back(label + ": \"depth\" must be a positive finite number");
        }
        const QJsonValue format = spec.value(QStringLiteral("format"));
        if (!format.isUndefined() &&
            (!format.isString() || !formats().contains(format.toString()))) {
            errors.push_back(label + ": unknown format '" + jsStringOf(format) + "'");
        }
        const QJsonValue is3D = spec.value(QStringLiteral("is3D"));
        if (!is3D.isUndefined() && !is3D.isBool()) {
            errors.push_back(label + ": \"is3D\" must be a boolean");
        }
    }
}

// --- validatePass: reference lines 732-931 ---
void validatePass(const QJsonObject& source, const QJsonObject& pass, int index, Errors& errors,
                  const ValidatorContext& context) {
    const std::string label = "Pass " + numToStd(index);

    if (!pass.contains(QStringLiteral("program")) || !isNonEmptyString(pass.value(QStringLiteral("program")))) {
        errors.push_back(label + ": Missing \"program\" string");
    }

    for (const QString& key : pass.keys()) {
        if (!passKeys().contains(key)) {
            errors.push_back(label + ": unknown field '" + toStd(key) + "'");
        }
    }

    const QJsonValue name = pass.value(QStringLiteral("name"));
    if (!name.isUndefined() && !isNonEmptyString(name)) {
        errors.push_back(label + ": \"name\" must be a non-empty string");
    }
    const QJsonValue entryPoint = pass.value(QStringLiteral("entryPoint"));
    if (!entryPoint.isUndefined() && !isNonEmptyString(entryPoint)) {
        errors.push_back(label + ": \"entryPoint\" must be a non-empty string");
    }
    const QJsonValue type = pass.value(QStringLiteral("type"));
    if (!type.isUndefined() && !passTypes().contains(type.toString())) {
        errors.push_back(label + ": unknown pass type '" + jsStringOf(type) + "'");
    }
    const QJsonValue drawMode = pass.value(QStringLiteral("drawMode"));
    if (!drawMode.isUndefined() && !drawModes().contains(drawMode.toString())) {
        errors.push_back(label + ": unknown drawMode '" + jsStringOf(drawMode) + "'");
    }
    const QJsonValue drawBuffers = pass.value(QStringLiteral("drawBuffers"));
    if (!drawBuffers.isUndefined() && (!isJsInteger(drawBuffers) || drawBuffers.toDouble() < 1)) {
        errors.push_back(label + ": \"drawBuffers\" must be a positive integer");
    }
    const QJsonValue count = pass.value(QStringLiteral("count"));
    if (!count.isUndefined()) {
        if (count.isString()) {
            const QString cs = count.toString();
            if (cs != QLatin1String("auto") && cs != QLatin1String("screen") && cs != QLatin1String("input")) {
                errors.push_back(label + ": unknown count '" + toStd(cs) + "'");
            }
        } else if (!isJsInteger(count) || count.toDouble() < 1) {
            errors.push_back(label + ": \"count\" must be a positive integer, 'auto', 'screen', or 'input'");
        }
    }
    const QJsonValue countUniform = pass.value(QStringLiteral("countUniform"));
    if (!countUniform.isUndefined()) {
        if (!isNonEmptyString(countUniform)) {
            errors.push_back(label + ": \"countUniform\" must be a non-empty string");
        } else if (!referencesGlobal(countUniform.toString(), context)) {
            errors.push_back(label + ": countUniform '" + toStd(countUniform.toString())
                             + "' does not reference a declared global");
        }
    }
    const QJsonValue repeat = pass.value(QStringLiteral("repeat"));
    if (!repeat.isUndefined()) {
        if (repeat.isString()) {
            if (repeat.toString().isEmpty()) {
                errors.push_back(label + ": \"repeat\" string must name a uniform");
            }
        } else if (!isJsInteger(repeat) || repeat.toDouble() < 1) {
            errors.push_back(label + ": \"repeat\" must be a positive integer or a uniform name string");
        }
    }
    const QJsonValue blend = pass.value(QStringLiteral("blend"));
    if (!blend.isUndefined()) {
        bool ok = blend.isBool();
        if (!ok && blend.isArray()) {
            const QJsonArray arr = blend.toArray();
            ok = arr.size() == 2;
            for (const QJsonValue& v : arr) {
                if (!isNonEmptyString(v)) { ok = false; break; }
            }
        }
        if (!ok) {
            errors.push_back(label + ": \"blend\" must be a boolean or [src, dst] factor strings");
        }
    }
    const QJsonValue workgroups = pass.value(QStringLiteral("workgroups"));
    if (!workgroups.isUndefined()) {
        bool ok = workgroups.isArray();
        if (ok) {
            const QJsonArray arr = workgroups.toArray();
            ok = arr.size() >= 1 && arr.size() <= 3;
            for (const QJsonValue& v : arr) {
                if (!isFiniteNumber(v) && !isNonEmptyString(v)) { ok = false; break; }
            }
        }
        if (!ok) {
            errors.push_back(label + ": \"workgroups\" must be an array of 1-3 numbers or uniform names");
        }
    }
    const QJsonValue storageBuffers = pass.value(QStringLiteral("storageBuffers"));
    if (!storageBuffers.isUndefined() && !storageBuffers.isObject()) {
        errors.push_back(label + ": \"storageBuffers\" must be an object");
    }
    const QJsonValue storageTextures = pass.value(QStringLiteral("storageTextures"));
    if (!storageTextures.isUndefined() && !storageTextures.isObject()) {
        errors.push_back(label + ": \"storageTextures\" must be an object");
    }
    const QJsonValue viewport = pass.value(QStringLiteral("viewport"));
    if (!viewport.isUndefined()) {
        if (!viewport.isObject()) {
            errors.push_back(label + ": \"viewport\" must be an object");
        } else {
            const QJsonObject viewportObj = viewport.toObject();
            for (const QString& key : viewportObj.keys()) {
                if (key == QLatin1String("width") || key == QLatin1String("height")) {
                    validateDimSpec(viewportObj.value(key), errors, label + ".viewport." + toStd(key));
                } else if (key == QLatin1String("x") || key == QLatin1String("y") ||
                           key == QLatin1String("w") || key == QLatin1String("h")) {
                    if (!isFiniteNumber(viewportObj.value(key))) {
                        errors.push_back(label + ".viewport." + toStd(key) + " must be a finite number");
                    }
                } else {
                    errors.push_back(label + ".viewport: unknown field '" + toStd(key) + "'");
                }
            }
        }
    }
    const QJsonValue conditions = pass.value(QStringLiteral("conditions"));
    if (!conditions.isUndefined()) {
        if (!conditions.isObject()) {
            errors.push_back(label + ": \"conditions\" must be an object");
        } else {
            const QJsonObject conditionsObj = conditions.toObject();
            for (const QString& key : conditionsObj.keys()) {
                if (!conditionContainerKeys().contains(key)) {
                    errors.push_back(label + ".conditions: unknown field '" + toStd(key) + "'");
                }
            }
            for (const QString& listKey : conditionContainerKeys()) {
                const QJsonValue listV = conditionsObj.value(listKey);
                if (listV.isUndefined()) continue;
                if (!listV.isArray()) {
                    errors.push_back(label + ".conditions." + toStd(listKey) + " must be an array");
                    continue;
                }
                for (const QJsonValue& conditionV : listV.toArray()) {
                    if (!conditionV.isObject()) {
                        errors.push_back(label + ".conditions." + toStd(listKey)
                                         + ": condition must be an object");
                        continue;
                    }
                    const QJsonObject condition = conditionV.toObject();
                    for (const QString& key : condition.keys()) {
                        if (key != QLatin1String("uniform") && key != QLatin1String("equals")) {
                            errors.push_back(label + ".conditions." + toStd(listKey)
                                             + ": unknown condition field '" + toStd(key) + "'");
                        }
                    }
                    const QJsonValue uniform = condition.value(QStringLiteral("uniform"));
                    if (!isNonEmptyString(uniform)) {
                        errors.push_back(label + ".conditions." + toStd(listKey)
                                         + ": \"uniform\" must be a non-empty string");
                    } else if (!referencesGlobal(uniform.toString(), context)) {
                        errors.push_back(label + ".conditions." + toStd(listKey) + ": uniform '"
                                         + toStd(uniform.toString()) + "' does not reference a declared global");
                    }
                    if (condition.value(QStringLiteral("equals")).isUndefined()) {
                        errors.push_back(label + ".conditions." + toStd(listKey)
                                         + ": condition requires an \"equals\" value");
                    }
                }
            }
        }
    }

    const QJsonValue uniforms = pass.value(QStringLiteral("uniforms"));
    if (!uniforms.isUndefined()) {
        if (!uniforms.isObject()) {
            errors.push_back(label + ": \"uniforms\" must be an object");
        } else {
            const QJsonObject uniformsObj = uniforms.toObject();
            for (auto it = uniformsObj.begin(); it != uniformsObj.end(); ++it) {
                // Numeric literals are preserved; strings name runtime uniforms.
                const QJsonValue value = it.value();
                if (!isFiniteNumber(value) && !isNonEmptyString(value)) {
                    errors.push_back(label + ": uniforms['" + toStd(it.key())
                                     + "'] must be a finite number or a non-empty string");
                }
            }
        }
    }

    const QJsonValue defines = pass.value(QStringLiteral("defines"));
    if (!defines.isUndefined()) {
        if (!defines.isObject()) {
            errors.push_back(label + ": \"defines\" must be an object");
        } else {
            const QJsonObject definesObj = defines.toObject();
            for (auto it = definesObj.begin(); it != definesObj.end(); ++it) {
                const QJsonValue value = it.value();
                if (!value.isString() && !isFiniteNumber(value)) {
                    errors.push_back(label + ": defines['" + toStd(it.key())
                                     + "'] must be a string or finite number");
                }
            }
        }
    }

    QSet<QString> declaredTextures;
    {
        const QJsonObject textures = source.value(QStringLiteral("textures")).toObject();
        for (auto it = textures.begin(); it != textures.end(); ++it) {
            declaredTextures.insert(it.key());
        }
        const QJsonObject textures3d = source.value(QStringLiteral("textures3d")).toObject();
        for (auto it = textures3d.begin(); it != textures3d.end(); ++it) {
            declaredTextures.insert(it.key());
        }
    }

    const QJsonValue inputs = pass.value(QStringLiteral("inputs"));
    if (!inputs.isUndefined()) {
        if (!inputs.isObject()) {
            errors.push_back(label + ": \"inputs\" must be an object");
        } else {
            const QJsonObject inputsObj = inputs.toObject();
            const QJsonValue externalTexture = source.value(QStringLiteral("externalTexture"));
            for (auto it = inputsObj.begin(); it != inputsObj.end(); ++it) {
                const QJsonValue texRef = it.value();
                if (!isNonEmptyString(texRef)) {
                    errors.push_back(label + ": inputs['" + toStd(it.key())
                                     + "'] must be a non-empty texture reference string");
                    continue;
                }
                const QString ref = texRef.toString();
                if (pipelineInputs().contains(ref)) continue;
                // /^o[0-7]$/
                if (ref.size() == 2 && ref.at(0) == QLatin1Char('o') &&
                    ref.at(1).isDigit() && ref.at(1).unicode() <= '7') continue;
                if (ref.startsWith(QLatin1String("global_"))) continue;
                if (declaredTextures.contains(ref)) continue;
                if (context.globalKeys.contains(ref)) continue;
                if (externalTexture.isString() && ref == externalTexture.toString()) continue;
                errors.push_back(label + ": inputs['" + toStd(it.key())
                                 + "'] references unsupported texture '" + toStd(ref) + "'");
            }
        }
    }

    const QJsonValue outputs = pass.value(QStringLiteral("outputs"));
    if (!outputs.isUndefined()) {
        if (!outputs.isObject()) {
            errors.push_back(label + ": \"outputs\" must be an object");
        } else {
            const QJsonObject outputsObj = outputs.toObject();
            for (auto it = outputsObj.begin(); it != outputsObj.end(); ++it) {
                const QJsonValue texRef = it.value();
                if (!isNonEmptyString(texRef)) {
                    errors.push_back(label + ": outputs['" + toStd(it.key())
                                     + "'] must be a non-empty texture reference string");
                    continue;
                }
                const QString ref = texRef.toString();
                if (pipelineOutputs().contains(ref)) continue;
                if (ref.startsWith(QLatin1String("global_"))) continue;
                if (declaredTextures.contains(ref)) continue;
                errors.push_back(label + ": outputs['" + toStd(it.key())
                                 + "'] references unsupported output '" + toStd(ref) + "'");
            }
        }
    }
}

} // namespace

void setEffectValidatorStdEnums(const QJsonObject& stdEnums) {
    g_stdEnumsOverride = &stdEnums;
}

std::vector<std::string> validateEffectDefinition(const QJsonValue& def) {
    Errors errors;

    if (isFalsy(def)) {
        return {"Effect definition is null or undefined"};
    }
    if (def.isArray()) {
        return {"Effect definition must be a plain object or Effect instance, not an array"};
    }
    if (!def.isObject()) {
        return {std::string("Effect definition must be a plain object or Effect instance, not ")
                + jsTypeOf(def)};
    }

    // The port's JSON grammar cannot carry Effect instances or subclass
    // constructors, so the plain-object path always applies: source == def
    // and top-level unknown-field diagnosis always runs (effect_validator.h).
    const QJsonObject source = def.toObject();

    ValidatorContext context;
    const QJsonValue globalsV = source.value(QStringLiteral("globals"));
    if (globalsV.isObject()) {
        const QJsonObject globalsObj = globalsV.toObject();
        for (auto it = globalsObj.begin(); it != globalsObj.end(); ++it) {
            context.globalKeys.insert(it.key());
            if (it.value().isObject()) {
                const QJsonValue uniform = it.value().toObject().value(QStringLiteral("uniform"));
                if (isNonEmptyString(uniform)) {
                    context.globalUniformNames.insert(uniform.toString());
                }
            }
        }
    }

    // --- name (existing message preserved) ---
    if (!isNonEmptyString(source.value(QStringLiteral("name")))) {
        errors.push_back("Missing or invalid \"name\" property");
    }

    // --- simple typed metadata ---
    const QJsonValue namespaceV = source.value(QStringLiteral("namespace"));
    if (!namespaceV.isUndefined() && !isNonEmptyString(namespaceV)) {
        errors.push_back("\"namespace\" must be a non-empty string");
    }
    const QJsonValue funcV = source.value(QStringLiteral("func"));
    if (!funcV.isUndefined() && !isNonEmptyString(funcV)) {
        errors.push_back("\"func\" must be a non-empty string");
    }
    const QJsonValue description = source.value(QStringLiteral("description"));
    if (!description.isUndefined() && !description.isString()) {
        errors.push_back("\"description\" must be a string");
    }
    const QJsonValue tags = source.value(QStringLiteral("tags"));
    if (!tags.isUndefined()) {
        if (!tags.isArray()) {
            errors.push_back("\"tags\" must be an array of tag strings");
        } else {
            for (const QJsonValue& tag : tags.toArray()) {
                if (!isNonEmptyString(tag)) {
                    errors.push_back("\"tags\" must contain non-empty strings");
                } else if (!validTags().contains(tag.toString())) {
                    errors.push_back("Unknown tag '" + toStd(tag.toString()) + "'");
                }
            }
        }
    }
    const QJsonValue openCategories = source.value(QStringLiteral("openCategories"));
    if (!openCategories.isUndefined()) {
        bool ok = openCategories.isArray();
        if (ok) {
            for (const QJsonValue& c : openCategories.toArray()) {
                if (!c.isString()) { ok = false; break; }
            }
        }
        if (!ok) {
            errors.push_back("\"openCategories\" must be an array of strings");
        }
    }
    const QJsonValue defaultProgram = source.value(QStringLiteral("defaultProgram"));
    if (!defaultProgram.isUndefined() && !defaultProgram.isString()) {
        errors.push_back("\"defaultProgram\" must be a string");
    }
    const QJsonValue hidden = source.value(QStringLiteral("hidden"));
    if (!hidden.isUndefined() && !hidden.isBool()) {
        errors.push_back("\"hidden\" must be a boolean");
    }
    const QJsonValue deprecatedBy = source.value(QStringLiteral("deprecatedBy"));
    if (!deprecatedBy.isUndefined() && !isNonEmptyString(deprecatedBy)) {
        errors.push_back("\"deprecatedBy\" must be a non-empty string");
    }
    const QJsonValue externalTexture = source.value(QStringLiteral("externalTexture"));
    if (!externalTexture.isUndefined() && !isNonEmptyString(externalTexture)) {
        errors.push_back("\"externalTexture\" must be a non-empty string");
    }
    const QJsonValue externalMesh = source.value(QStringLiteral("externalMesh"));
    if (!externalMesh.isUndefined() && !isNonEmptyString(externalMesh)) {
        errors.push_back("\"externalMesh\" must be a non-empty string");
    }
    const QJsonValue builtinMeshes = source.value(QStringLiteral("builtinMeshes"));
    if (!builtinMeshes.isUndefined()) {
        if (builtinMeshes.isArray()) {
            // Port-authored array form emitted by tools/convert-definitions.mjs
            // (effects/render/meshLoader.json): [{name, path}].
            const QJsonArray arr = builtinMeshes.toArray();
            for (int i = 0; i < arr.size(); ++i) {
                const QJsonValue entry = arr.at(i);
                if (!entry.isObject()) {
                    errors.push_back("builtinMeshes[" + numToStd(i)
                                     + "] must be an object with \"name\" and \"path\" strings");
                    continue;
                }
                const QJsonObject obj = entry.toObject();
                if (!isNonEmptyString(obj.value(QStringLiteral("name")))) {
                    errors.push_back("builtinMeshes[" + numToStd(i)
                                     + "] must have a non-empty \"name\" string");
                }
                if (!isNonEmptyString(obj.value(QStringLiteral("path")))) {
                    errors.push_back("builtinMeshes[" + numToStd(i)
                                     + "] must have a non-empty \"path\" string");
                }
            }
        } else if (!builtinMeshes.isObject()) {
            errors.push_back("\"builtinMeshes\" must be an object");
        } else {
            // Reference map form ({name: "path"}).
            const QJsonObject meshObj = builtinMeshes.toObject();
            for (auto it = meshObj.begin(); it != meshObj.end(); ++it) {
                if (!isNonEmptyString(it.value())) {
                    errors.push_back("builtinMeshes['" + toStd(it.key())
                                     + "'] must be a non-empty string");
                }
            }
        }
    }
    const QJsonValue outputTex3d = source.value(QStringLiteral("outputTex3d"));
    if (!outputTex3d.isUndefined() && !isNonEmptyString(outputTex3d)) {
        errors.push_back("\"outputTex3d\" must be a non-empty string");
    }
    const QJsonValue outputGeo = source.value(QStringLiteral("outputGeo"));
    if (!outputGeo.isUndefined() && !isNonEmptyString(outputGeo)) {
        errors.push_back("\"outputGeo\" must be a non-empty string");
    }

    // --- lifecycle hooks must be functions ---
    // JSON cannot encode functions (effect_validator.h): any present hook
    // value means the converter dropped a function the reference keeps, so it
    // always receives the reference's non-function message.
    for (const char* hook : {"onInit", "onUpdate", "onDestroy", "asyncInit"}) {
        const QJsonValue value = source.value(QLatin1String(hook));
        if (!value.isUndefined()) {
            errors.push_back(std::string("\"") + hook + "\" must be a function");
        }
    }

    // --- globals ---
    validateGlobals(globalsV, errors, context);

    // --- passes ---
    const QJsonValue passes = source.value(QStringLiteral("passes"));
    if (!passes.isArray() || passes.toArray().isEmpty()) {
        errors.push_back("Missing or empty \"passes\" array");
    } else {
        const QJsonArray passesArr = passes.toArray();
        for (int index = 0; index < passesArr.size(); ++index) {
            const QJsonValue passV = passesArr.at(index);
            if (passV.isObject()) {
                validatePass(source, passV.toObject(), index, errors, context);
            } else {
                errors.push_back("Pass " + numToStd(index) + ": must be an object");
            }
        }
    }

    // --- textures ---
    validateTextureMap(source.value(QStringLiteral("textures")), errors, "textures");
    validateTextureMap(source.value(QStringLiteral("textures3d")), errors, "textures3d");

    // --- shaders ---
    const QJsonValue shaders = source.value(QStringLiteral("shaders"));
    if (!shaders.isUndefined()) {
        if (!shaders.isObject()) {
            errors.push_back("\"shaders\" must be an object mapping program names to shader maps");
        } else {
            const QJsonObject shadersObj = shaders.toObject();
            for (auto it = shadersObj.begin(); it != shadersObj.end(); ++it) {
                if (!it.value().isObject()) {
                    errors.push_back("shaders['" + toStd(it.key()) + "'] must be an object");
                }
            }
        }
    }

    // --- uniform layouts ---
    const QJsonValue uniformLayout = source.value(QStringLiteral("uniformLayout"));
    if (!uniformLayout.isUndefined()) {
        validateUniformLayout(uniformLayout, errors, "uniformLayout");
    }
    const QJsonValue uniformLayouts = source.value(QStringLiteral("uniformLayouts"));
    if (!uniformLayouts.isUndefined()) {
        if (!uniformLayouts.isObject()) {
            errors.push_back("\"uniformLayouts\" must be an object mapping program names to layouts");
        } else {
            const QJsonObject layoutsObj = uniformLayouts.toObject();
            for (auto it = layoutsObj.begin(); it != layoutsObj.end(); ++it) {
                validateUniformLayout(it.value(),
                                      errors, "uniformLayouts['" + toStd(it.key()) + "']");
            }
        }
    }

    // --- param aliases ---
    const QJsonValue paramAliases = source.value(QStringLiteral("paramAliases"));
    if (!paramAliases.isUndefined()) {
        if (!paramAliases.isObject()) {
            errors.push_back("\"paramAliases\" must be an object mapping aliases to global names");
        } else {
            const QJsonObject aliasesObj = paramAliases.toObject();
            for (auto it = aliasesObj.begin(); it != aliasesObj.end(); ++it) {
                const QJsonValue target = it.value();
                if (!isNonEmptyString(target)) {
                    errors.push_back("paramAliases['" + toStd(it.key()) + "'] must be a non-empty string");
                } else if (!context.globalKeys.contains(target.toString())) {
                    errors.push_back("paramAliases['" + toStd(it.key()) + "'] references unknown global '"
                                     + toStd(target.toString()) + "'");
                }
            }
        }
    }

    // --- top-level unknown-field diagnosis (plain definition objects only) ---
    for (const QString& key : source.keys()) {
        if (!topLevelKeys().contains(key)) {
            errors.push_back("Unknown definition field '" + toStd(key) + "'");
        }
    }

    return errors;
}

std::vector<std::string> validateEffectDefinition(const QJsonObject& def) {
    return validateEffectDefinition(QJsonValue(def));
}

} // namespace nm
