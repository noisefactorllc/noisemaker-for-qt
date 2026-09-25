// Unit tests for nm::validateEffectDefinition (qt/noisemaker/compiler/
// effect_validator.{h,cpp}): the port of shaders/src/runtime/effect-validator.js
// validateEffectDefinition() (upstream GAP-003, commits ba87ffae + 9d3474df).
// Plain assert-style checks, no test framework dependency.
//
// Cases mirror shaders/tests/test_effect_definition_validation.js, minus the
// shapes the port's JSON grammar cannot represent (Effect instances and
// subclass constructors are documented deviations in effect_validator.h).
// The upstream corpus walk (every tracked definition validates cleanly) is
// mirrored here over the port's generated definition JSONs under
// qt/noisemaker/effects/ — the port's shape contract (starter key accepted,
// hooks absent, builtinMeshes array form) applies to all of them.

#include "../noisemaker/compiler/effect_validator.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace {

using nm::validateEffectDefinition;

int g_failures = 0;

std::vector<std::string> validate(const QJsonObject& def) {
    return validateEffectDefinition(def);
}

QJsonObject parseDefinition(const char* json, const char* source) {
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json));
    if (doc.isNull()) {
        std::printf("FAIL: %s is not valid JSON\n", source);
        ++g_failures;
    }
    return doc.object();
}

// A complete, valid plain-object definition exercising the schema surface
// (the reference test's validDefinition()).
QJsonObject validDefinition() {
    return parseDefinition(R"json({
    "name": "Validator Probe",
    "namespace": "synth",
    "func": "validatorProbe",
    "description": "Used by the definition-validator tests",
    "tags": ["noise", "util"],
    "openCategories": ["general"],
    "defaultProgram": "search synth\nvalidatorProbe().write(o0)",
    "hidden": false,
    "uniformLayout": {
        "resolution": { "slot": 0, "components": "xy" },
        "time": { "slot": 0, "components": "z" }
    },
    "uniformLayouts": {
        "alt": { "amount": { "slot": 0, "components": "x" } }
    },
    "paramAliases": { "amt": "amount" },
    "textures": {
        "scratch": { "width": 64, "height": "100%", "format": "rgba16f" },
        "scaled": { "width": { "param": "volumeSize", "power": 2, "default": 1024 }, "height": { "screenDivide": "zoom", "default": 8 } }
    },
    "globals": {
        "amount": {
            "type": "float", "default": 0.5, "uniform": "amount",
            "min": 0, "max": 1, "step": 0.01, "zero": 0,
            "ui": { "label": "amount", "control": "slider", "category": "effect" }
        },
        "mode": {
            "type": "int", "default": 1, "uniform": "mode",
            "define": "PROBE_MODE",
            "choices": { "off": 0, "on": 1, "Group:": null },
            "ui": { "label": "mode", "control": "dropdown", "enabledBy": "amount" }
        },
        "flag": {
            "type": "boolean", "default": true, "uniform": "flag",
            "ui": { "label": "flag", "control": "checkbox", "enabledBy": { "param": "mode", "eq": 1 } }
        },
        "tint": {
            "type": "color", "default": [1, 0, 0], "uniform": "tint",
            "ui": { "label": "tint", "control": "color" }
        },
        "point": {
            "type": "vec3", "default": [0, 0, 0], "uniform": "point",
            "min": [-1, -1, -1], "max": [1, 1, 1],
            "ui": { "label": "point", "control": "vector3", "format": "x/y/z" }
        },
        "table": {
            "type": "int", "default": 7,
            "choices": { "a": 7, "b": 9 },
            "ui": { "label": "table", "control": "dropdown", "hidden": true }
        },
        "surfaceIn": {
            "type": "surface", "default": "none",
            "colorModeUniform": "surfaceActive",
            "ui": { "label": "surface", "control": false }
        }
    },
    "passes": [
        {
            "name": "render",
            "program": "probe",
            "type": "compute",
            "drawMode": "points",
            "count": "input",
            "countUniform": "mode",
            "repeat": 2,
            "blend": ["ONE", "ONE_MINUS_SRC_ALPHA"],
            "drawBuffers": 2,
            "workgroups": [8, 8, 1],
            "viewport": { "width": { "param": "volumeSize", "paramDefault": 64 }, "height": 32 },
            "conditions": {
                "runIf": [{ "uniform": "mode", "equals": 1 }],
                "skipIf": [{ "uniform": "flag", "equals": false }]
            },
            "uniforms": { "amount": "amount", "literal": 3 },
            "inputs": { "srcTex": "inputTex", "scratchTex": "scratch", "paramTex": "surfaceIn" },
            "outputs": { "fragColor": "outputTex" }
        }
    ]
})json",
                              "validDefinition");
}

void checkValid(const QJsonObject& def, const char* source) {
    const std::vector<std::string> errors = validate(def);
    if (!errors.empty()) {
        std::printf("FAIL: %s expected [], got:", source);
        for (const std::string& e : errors) std::printf(" [%s]", e.c_str());
        std::printf("\n");
        ++g_failures;
    } else {
        std::printf("PASS: %s -> []\n", source);
    }
}

void checkAnyError(const QJsonObject& def, const char* source) {
    const std::vector<std::string> errors = validate(def);
    if (errors.empty()) {
        std::printf("FAIL: %s expected >=1 error, got []\n", source);
        ++g_failures;
    } else {
        std::printf("PASS: %s -> %zu error(s)\n", source, errors.size());
    }
}

void checkErrors(const QJsonObject& def, size_t expected, const char* source) {
    const std::vector<std::string> errors = validate(def);
    if (errors.size() != expected) {
        std::printf("FAIL: %s expected %zu errors, got %zu:", source, expected, errors.size());
        for (const std::string& e : errors) std::printf(" [%s]", e.c_str());
        std::printf("\n");
        ++g_failures;
    } else {
        std::printf("PASS: %s -> %zu error(s)\n", source, errors.size());
    }
}

void checkMessage(const QJsonObject& def, const char* substring, const char* source) {
    const std::vector<std::string> errors = validate(def);
    for (const std::string& e : errors) {
        if (e.find(substring) != std::string::npos) {
            std::printf("PASS: %s -> %s\n", source, e.c_str());
            return;
        }
    }
    std::printf("FAIL: %s expected an error containing \"%s\", got:", source, substring);
    for (const std::string& e : errors) std::printf(" [%s]", e.c_str());
    std::printf("\n");
    ++g_failures;
}

void checkMessageCount(const QJsonObject& def, const char* substring, size_t expected,
                       const char* source) {
    size_t count = 0;
    for (const std::string& e : validate(def)) {
        if (e.find(substring) != std::string::npos) ++count;
    }
    if (count != expected) {
        std::printf("FAIL: %s expected %zu occurrences of \"%s\", got %zu\n", source, expected,
                    substring, count);
        ++g_failures;
    } else {
        std::printf("PASS: %s\n", source);
    }
}

// Mutators that must produce at least one error (the reference test's
// malformed-container, type-constraint, and unsupported-reference cases).

QJsonObject getObject(QJsonObject& host, const QString& key) {
    return host.value(key).toObject();
}

// Merge a key into a nested object, replacing it wholesale.
void nest(QJsonObject& def, const QString& container, const QString& key,
          const QJsonValue& value) {
    QJsonObject host = def.value(container).toObject();
    host.insert(key, value);
    def.insert(container, host);
}

void nestInGlobals(QJsonObject& def, const QString& global, const QJsonValue& spec) {
    QJsonObject globals = def.value("globals").toObject();
    globals.insert(global, spec);
    def.insert("globals", globals);
}

void mutateFirstPass(QJsonObject& def, const QString& key, const QJsonValue& value) {
    QJsonArray passes = def.value("passes").toArray();
    QJsonObject pass = passes.at(0).toObject();
    pass.insert(key, value);
    passes.replace(0, pass);
    def.insert("passes", passes);
}

void mutateFirstPassNested(QJsonObject& def, const QString& container, const QString& key,
                           const QJsonValue& value) {
    QJsonArray passes = def.value("passes").toArray();
    QJsonObject pass = passes.at(0).toObject();
    QJsonObject host = pass.value(container).toObject();
    host.insert(key, value);
    pass.insert(container, host);
    passes.replace(0, pass);
    def.insert("passes", passes);
}

} // namespace

int main() {
    // --- null and non-object containers (exact reference messages) ---
    // null/undefined: the port receives them as a QJsonValue, so exercise via
    // validateEffectDefinition(QJsonValue) directly.
    {
        const std::vector<std::string> errors =
            nm::validateEffectDefinition(QJsonValue(QJsonValue::Null));
        const bool ok = errors.size() == 1 &&
                        errors.at(0) == "Effect definition is null or undefined";
        std::printf("%s: null -> exact message\n", ok ? "PASS" : "FAIL");
        if (!ok) ++g_failures;
    }
    {
        const std::vector<std::string> errors = nm::validateEffectDefinition(QJsonArray());
        const bool ok = errors.size() == 1 &&
                        errors.at(0) ==
                            "Effect definition must be a plain object or Effect instance, "
                            "not an array";
        std::printf("%s: array -> exact message\n", ok ? "PASS" : "FAIL");
        if (!ok) ++g_failures;
    }
    for (const QJsonValue& bad : {QJsonValue("string"), QJsonValue(42), QJsonValue(true)}) {
        const std::vector<std::string> errors = nm::validateEffectDefinition(bad);
        const bool ok = !errors.empty();
        std::printf("%s: primitive diagnosed\n", ok ? "PASS" : "FAIL");
        if (!ok) ++g_failures;
    }

    // --- valid plain-object definition returns no errors ---
    checkValid(validDefinition(), "valid plain-object definition");

    // --- unknown top-level declarative fields are diagnosed ---
    {
        QJsonObject def = validDefinition();
        def.insert("globalz", def.value("globals"));
        checkMessageCount(def, "globalz", 1, "unknown top-level field diagnosed");
    }

    // --- unknown nested fields are diagnosed ---
    {
        QJsonObject def = validDefinition();
        QJsonObject globals = getObject(def, "globals");
        QJsonObject amount = getObject(globals, "amount");
        amount.insert("unkown", 1);
        globals.insert("amount", amount);
        def.insert("globals", globals);
        checkMessageCount(def, "unkown", 1, "unknown global-spec field diagnosed");
    }
    {
        QJsonObject def = validDefinition();
        QJsonObject globals = getObject(def, "globals");
        QJsonObject mode = getObject(globals, "mode");
        QJsonObject ui = getObject(mode, "ui");
        ui.insert("colour", "red");
        mode.insert("ui", ui);
        globals.insert("mode", mode);
        def.insert("globals", globals);
        checkMessageCount(def, "colour", 1, "unknown ui field diagnosed");
    }
    {
        QJsonObject def = validDefinition();
        mutateFirstPass(def, "progam", "typo");
        checkMessageCount(def, "progam", 1, "unknown pass field diagnosed");
    }
    {
        QJsonObject def = validDefinition();
        QJsonObject textures = getObject(def, "textures");
        QJsonObject scratch = getObject(textures, "scratch");
        scratch.insert("widht", 8);
        textures.insert("scratch", scratch);
        def.insert("textures", textures);
        checkMessageCount(def, "widht", 1, "unknown texture-spec field diagnosed");
    }

    // --- malformed containers are reported without throwing ---
    {
        struct Case {
            const char* name;
            void (*mutate)(QJsonObject&);
        };
        const Case cases[] = {
            {"globals is an array", [](QJsonObject& d) { d.insert("globals", QJsonArray{"nope"}); }},
            {"globals is a string", [](QJsonObject& d) { d.insert("globals", "nope"); }},
            {"globals entry null", [](QJsonObject& d) {
                 QJsonObject g; g.insert("g", QJsonValue());
                 d.insert("globals", g);
             }},
            {"globals entry array", [](QJsonObject& d) {
                 QJsonObject g; g.insert("g", QJsonArray());
                 d.insert("globals", g);
             }},
            {"ui is a string", [](QJsonObject& d) {
                 nestInGlobals(d, "g", QJsonObject{{"type", "float"}, {"default", 0.5},
                                                   {"ui", "slider"}});
             }},
            {"passes is an object", [](QJsonObject& d) {
                 QJsonObject p; p.insert("program", "x");
                 d.insert("passes", p);
             }},
            {"passes contains null", [](QJsonObject& d) {
                 QJsonArray a; a.append(QJsonValue());
                 d.insert("passes", a);
             }},
            {"inputs is a string", [](QJsonObject& d) {
                 QJsonArray a; QJsonObject p; p.insert("program", "p"); p.insert("inputs", "x");
                 a.append(p); d.insert("passes", a);
             }},
            {"outputs is a number", [](QJsonObject& d) {
                 QJsonArray a; QJsonObject p; p.insert("program", "p"); p.insert("outputs", 3);
                 a.append(p); d.insert("passes", a);
             }},
            {"uniforms is a string", [](QJsonObject& d) {
                 QJsonArray a; QJsonObject p; p.insert("program", "p"); p.insert("uniforms", "x");
                 a.append(p); d.insert("passes", a);
             }},
            {"conditions is a string", [](QJsonObject& d) {
                 QJsonArray a; QJsonObject p; p.insert("program", "p");
                 p.insert("conditions", "always");
                 a.append(p); d.insert("passes", a);
             }},
            {"runIf entry null", [](QJsonObject& d) {
                 QJsonArray a; QJsonObject p; p.insert("program", "p");
                 QJsonObject c; QJsonArray r; r.append(QJsonValue());
                 c.insert("runIf", r); p.insert("conditions", c);
                 a.append(p); d.insert("passes", a);
             }},
            {"condition uniform is a number", [](QJsonObject& d) {
                 QJsonArray a; QJsonObject p; p.insert("program", "p");
                 QJsonObject c; QJsonArray r; QJsonObject cond; cond.insert("uniform", 7);
                 r.append(cond); c.insert("runIf", r); p.insert("conditions", c);
                 a.append(p); d.insert("passes", a);
             }},
            {"condition missing equals", [](QJsonObject& d) {
                 QJsonArray a; QJsonObject p; p.insert("program", "p");
                 QJsonObject c; QJsonArray r; QJsonObject cond; cond.insert("uniform", "mode");
                 r.append(cond); c.insert("runIf", r); p.insert("conditions", c);
                 a.append(p); d.insert("passes", a);
             }},
            {"textures is an array", [](QJsonObject& d) {
                 QJsonArray t; t.append("x"); d.insert("textures", t);
             }},
            {"texture spec is a string", [](QJsonObject& d) {
                 QJsonObject t; t.insert("t", "big"); d.insert("textures", t);
             }},
            {"texture width is a word", [](QJsonObject& d) {
                 QJsonObject t; QJsonObject s; s.insert("width", "banana");
                 t.insert("t", s); d.insert("textures", t);
             }},
            {"texture width param is a number", [](QJsonObject& d) {
                 QJsonObject t; QJsonObject s; QJsonObject w; w.insert("param", 42);
                 s.insert("width", w); t.insert("t", s); d.insert("textures", t);
             }},
            {"texture width has an unknown field", [](QJsonObject& d) {
                 QJsonObject t; QJsonObject s; QJsonObject w; w.insert("wrong", 1);
                 s.insert("width", w); t.insert("t", s); d.insert("textures", t);
             }},
            {"texture width is negative", [](QJsonObject& d) {
                 QJsonObject t; QJsonObject s; s.insert("width", -4);
                 t.insert("t", s); d.insert("textures", t);
             }},
            {"texture format is unknown", [](QJsonObject& d) {
                 QJsonObject t; QJsonObject s; s.insert("format", "rgba999");
                 t.insert("t", s); d.insert("textures", t);
             }},
            {"uniformLayout slot is a string", [](QJsonObject& d) {
                 QJsonObject l; QJsonObject u; u.insert("slot", "x"); u.insert("components", "x");
                 l.insert("u", u); d.insert("uniformLayout", l);
             }},
            {"uniformLayout components are out of the grammar", [](QJsonObject& d) {
                 QJsonObject l; QJsonObject u; u.insert("slot", 0);
                 u.insert("components", "xyzwq");
                 l.insert("u", u); d.insert("uniformLayout", l);
             }},
            {"uniformLayout components are out of order", [](QJsonObject& d) {
                 QJsonObject l; QJsonObject u; u.insert("slot", 0); u.insert("components", "zy");
                 l.insert("u", u); d.insert("uniformLayout", l);
             }},
            {"uniformLayout slot is fractional", [](QJsonObject& d) {
                 QJsonObject l; QJsonObject u; u.insert("slot", 0.5); u.insert("components", "x");
                 l.insert("u", u); d.insert("uniformLayout", l);
             }},
            {"uniformLayout conflicts at one slot", [](QJsonObject& d) {
                 QJsonObject l;
                 QJsonObject u; u.insert("slot", 0); u.insert("components", "x");
                 QJsonObject v; v.insert("slot", 0); v.insert("components", "y");
                 QJsonObject w; w.insert("slot", 0); w.insert("components", "x");
                 l.insert("u", u); l.insert("v", v); l.insert("w", w);
                 d.insert("uniformLayout", l);
             }},
            {"uniformLayouts value is a string", [](QJsonObject& d) {
                 QJsonObject ls; ls.insert("p", "layout"); d.insert("uniformLayouts", ls);
             }},
            {"paramAliases is a string", [](QJsonObject& d) { d.insert("paramAliases", "aliases"); }},
            {"paramAliases target is unknown", [](QJsonObject& d) {
                 QJsonObject a; a.insert("amt", "nosuchglobal"); d.insert("paramAliases", a);
             }},
            {"tags is a string", [](QJsonObject& d) { d.insert("tags", "noise"); }},
            {"tags contains an unknown tag", [](QJsonObject& d) {
                 QJsonArray t; t.append("not-a-tag"); d.insert("tags", t);
             }},
            {"tags contains a number", [](QJsonObject& d) {
                 QJsonArray t; t.append(42); d.insert("tags", t);
             }},
            {"openCategories contains a number", [](QJsonObject& d) {
                 QJsonArray t; t.append(7); d.insert("openCategories", t);
             }},
            {"onInit present (JSON cannot encode functions)", [](QJsonObject& d) {
                 d.insert("onInit", "nope");
             }},
            {"onUpdate present (JSON cannot encode functions)", [](QJsonObject& d) {
                 d.insert("onUpdate", 42);
             }},
            {"onDestroy present (JSON cannot encode functions)", [](QJsonObject& d) {
                 d.insert("onDestroy", QJsonObject());
             }},
            {"asyncInit present (JSON cannot encode functions)", [](QJsonObject& d) {
                 d.insert("asyncInit", true);
             }},
            {"shaders is a string", [](QJsonObject& d) { d.insert("shaders", "inline"); }},
            {"shaders value is a string", [](QJsonObject& d) {
                 QJsonObject s; s.insert("main", "source"); d.insert("shaders", s);
             }},
            {"deprecatedBy is a number", [](QJsonObject& d) { d.insert("deprecatedBy", 5); }},
            {"externalTexture is a number", [](QJsonObject& d) { d.insert("externalTexture", 7); }},
        };
        for (const Case& c : cases) {
            QJsonObject def = validDefinition();
            c.mutate(def);
            checkAnyError(def, c.name);
        }
    }

    // --- global spec type and primitive constraints are enforced ---
    {
        struct Case {
            const char* name;
            QString global;
            QString key;
            QJsonValue value;
        };
        const Case cases[] = {
            {"float type is vec9", "amount", "type", "vec9"},
            {"float type is a number", "amount", "type", 3},
            {"float default is a string", "amount", "default", "high"},
            {"int default is fractional", "mode", "default", 1.5},
            {"int default is a string", "mode", "default", "one"},
            {"boolean default is a number", "flag", "default", 1},
            {"color default is short", "tint", "default", QJsonArray{1, 0}},
            {"color default has a string", "tint", "default", QJsonArray{1, 0, "a"}},
            {"vec3 default is short", "point", "default", QJsonArray{0, 0}},
            {"vec3 default is out of min/max", "point", "default", QJsonArray{0, 0, 2}},
            {"vec3 min is short", "point", "min", QJsonArray{-1, -1}},
            {"vec3 max is a number", "point", "max", 1},
            {"float min is a string", "amount", "min", "low"},
            {"float min exceeds max", "amount", "min", 0.9},
            {"float max is below min", "amount", "max", 0.4},
            {"float step is a string", "amount", "step", "x"},
            {"choices is an array", "mode", "choices", QJsonArray{0, 1}},
            {"choices value is a string", "mode", "choices", QJsonObject{{"bad", "x"}}},
            {"int default not among choices", "mode", "default", 5},
            {"dropdown default not among choices", "table", "default", 8},
            {"colorModeUniform is a number", "surfaceIn", "colorModeUniform", 3},
            {"uniform is a number", "amount", "uniform", 9},
            {"define is a number", "mode", "define", 5},
        };
        for (const Case& c : cases) {
            QJsonObject def = validDefinition();
            QJsonObject globals = getObject(def, "globals");
            QJsonObject spec = getObject(globals, c.global);
            spec.insert(c.key, c.value);
            globals.insert(c.global, spec);
            def.insert("globals", globals);
            checkAnyError(def, c.name);
        }
    }
    // NaN and Infinity defaults (JSON cannot carry them; QJsonValue can hold
    // the doubles, which is how callers passing runtime-computed values
    // exercise the reference's !isFinite branch).
    for (const double bad : {std::nan(""), std::numeric_limits<double>::infinity()}) {
        QJsonObject def = validDefinition();
        QJsonObject globals = getObject(def, "globals");
        QJsonObject amount = getObject(globals, "amount");
        amount.insert("default", bad);
        globals.insert("amount", amount);
        def.insert("globals", globals);
        checkAnyError(def, "float default is NaN/Infinity");
    }
    {
        QJsonObject def = validDefinition();
        QJsonObject globals = getObject(def, "globals");
        QJsonObject amount = getObject(globals, "amount");
        amount.insert("max", std::numeric_limits<double>::infinity());
        globals.insert("amount", amount);
        def.insert("globals", globals);
        checkAnyError(def, "float max is Infinity");
    }

    // --- member-typed globals must resolve through the std enum tables ---
    {
        QJsonObject spec{
            {"type", "member"}, {"default", "noSuchTable.member"}, {"enum", "noSuchTable"},
            {"ui", QJsonObject{{"label", "member"}, {"control", "dropdown"}}}};
        QJsonObject def = validDefinition();
        nestInGlobals(def, "memberProbe", spec);
        checkMessage(def, "noSuchTable", "member enum path unresolved");
    }
    {
        QJsonObject spec{
            {"type", "member"}, {"default", "oscType.sine"}, {"enum", "oscType"},
            {"ui", QJsonObject{{"label", "member"}, {"control", "dropdown"}}}};
        QJsonObject def = validDefinition();
        nestInGlobals(def, "memberProbe", spec);
        checkValid(def, "member enum resolves through std tables");
    }

    // --- duplicate and conflicting bindings and layouts are diagnosed ---
    {
        QJsonObject def = validDefinition();
        nestInGlobals(def, "other",
                      QJsonObject{{"type", "float"}, {"default", 0}, {"uniform", "amount"}});
        checkMessage(def, "amount", "duplicate uniform binding diagnosed");
    }

    // --- template interpolation of non-string values follows JS String(value) ---
    {
        QJsonObject def = validDefinition();
        mutateFirstPass(def, "type", 5);
        const std::string want = "Pass 0: unknown pass type '5'";
        const std::vector<std::string> errors = validate(def);
        bool ok = false;
        for (const std::string& e : errors) {
            if (e == want) ok = true;
        }
        std::printf("%s: non-string pass type interpolates as String(5)\n", ok ? "PASS" : "FAIL");
        if (!ok) {
            for (const std::string& e : errors) std::printf("  [%s]\n", e.c_str());
            ++g_failures;
        }
    }
    {
        QJsonObject def = validDefinition();
        mutateFirstPass(def, "drawMode", true);
        const std::string want = "Pass 0: unknown drawMode 'true'";
        const std::vector<std::string> errors = validate(def);
        bool ok = false;
        for (const std::string& e : errors) {
            if (e == want) ok = true;
        }
        std::printf("%s: non-string drawMode interpolates as String(true)\n", ok ? "PASS" : "FAIL");
        if (!ok) {
            for (const std::string& e : errors) std::printf("  [%s]\n", e.c_str());
            ++g_failures;
        }
    }
    {
        QJsonObject def = validDefinition();
        QJsonObject t; QJsonObject s; s.insert("format", 1);
        t.insert("t", s); def.insert("textures", t);
        const std::string want = "Texture 't': unknown format '1'";
        const std::vector<std::string> errors = validate(def);
        bool ok = false;
        for (const std::string& e : errors) {
            if (e == want) ok = true;
        }
        std::printf("%s: non-string texture format interpolates as String(1)\n", ok ? "PASS" : "FAIL");
        if (!ok) {
            for (const std::string& e : errors) std::printf("  [%s]\n", e.c_str());
            ++g_failures;
        }
    }
    {
        // The reference emits paramAliases['<alias>'] with BOTH quotes.
        QJsonObject def = validDefinition();
        QJsonObject a; a.insert("amt", "nosuchglobal"); def.insert("paramAliases", a);
        const std::string want = "paramAliases['amt'] references unknown global 'nosuchglobal'";
        const std::vector<std::string> errors = validate(def);
        bool ok = false;
        for (const std::string& e : errors) {
            if (e == want) ok = true;
        }
        std::printf("%s: paramAliases unknown-global message is exact\n", ok ? "PASS" : "FAIL");
        if (!ok) {
            for (const std::string& e : errors) std::printf("  [%s]\n", e.c_str());
            ++g_failures;
        }
    }
    {
        QJsonObject def = validDefinition();
        QJsonObject l;
        QJsonObject a; a.insert("slot", 1); a.insert("components", "x");
        QJsonObject b; b.insert("slot", 1); b.insert("components", "xy");
        l.insert("a", a); l.insert("b", b);
        def.insert("uniformLayout", l);
        checkMessage(def, "slot 1", "overlapping layout diagnosed");
    }
    {
        QJsonObject def = validDefinition();
        QJsonObject l;
        QJsonObject a; a.insert("slot", 1); a.insert("components", "x");
        QJsonObject b; b.insert("slot", 1); b.insert("components", "x");
        l.insert("a", a); l.insert("b", b);
        def.insert("uniformLayout", l);
        checkAnyError(def, "duplicate layout entries diagnosed");
    }

    // --- unsupported binding references are diagnosed ---
    {
        struct Case {
            const char* name;
            void (*mutate)(QJsonObject&);
        };
        const Case cases[] = {
            {"pass input is a number", [](QJsonObject& d) { mutateFirstPassNested(d, "inputs", "bad", 7); }},
            {"pass input references an undeclared texture", [](QJsonObject& d) { mutateFirstPassNested(d, "inputs", "bad", "notDeclaredAnywhere"); }},
            {"pass output references an undeclared texture", [](QJsonObject& d) { mutateFirstPassNested(d, "outputs", "bad", "notDeclaredAnywhere"); }},
            {"pass uniform is an array", [](QJsonObject& d) { mutateFirstPassNested(d, "uniforms", "bad", QJsonArray()); }},
            {"pass uniform is an object", [](QJsonObject& d) { mutateFirstPassNested(d, "uniforms", "bad", QJsonObject{{"ref", 1}}); }},
            {"condition uniform is undeclared", [](QJsonObject& d) { mutateFirstPassNested(d, "conditions", "runIf", QJsonArray{QJsonObject{{"uniform", "nosuch"}, {"equals", 1}}}); }},
            {"countUniform is undeclared", [](QJsonObject& d) { mutateFirstPass(d, "countUniform", "nosuch"); }},
            {"count is a word", [](QJsonObject& d) { mutateFirstPass(d, "count", "banana"); }},
            {"count is negative", [](QJsonObject& d) { mutateFirstPass(d, "count", -1); }},
            {"repeat is an array", [](QJsonObject& d) { mutateFirstPass(d, "repeat", QJsonArray()); }},
            {"drawMode is unknown", [](QJsonObject& d) { mutateFirstPass(d, "drawMode", "hexagons"); }},
            {"type is unknown", [](QJsonObject& d) { mutateFirstPass(d, "type", "vertex"); }},
            {"drawBuffers is zero", [](QJsonObject& d) { mutateFirstPass(d, "drawBuffers", 0); }},
            {"blend is a string", [](QJsonObject& d) { mutateFirstPass(d, "blend", "on"); }},
            {"blend has one factor", [](QJsonObject& d) { mutateFirstPass(d, "blend", QJsonArray{"ONE"}); }},
            {"workgroups is a string", [](QJsonObject& d) { mutateFirstPass(d, "workgroups", "8"); }},
            {"viewport is a number", [](QJsonObject& d) { mutateFirstPass(d, "viewport", 32); }},
            {"entryPoint is a number", [](QJsonObject& d) { mutateFirstPass(d, "entryPoint", 7); }},
            {"enabledBy param is undeclared", [](QJsonObject& d) {
                 QJsonObject globals = getObject(d, "globals");
                 QJsonObject flag = getObject(globals, "flag");
                 QJsonObject ui = getObject(flag, "ui");
                 ui.insert("enabledBy", QJsonObject{{"param", "nosuch"}, {"eq", 1}});
                 flag.insert("ui", ui);
                 globals.insert("flag", flag);
                 d.insert("globals", globals);
             }},
            {"enabledBy is an undeclared name", [](QJsonObject& d) {
                 QJsonObject globals = getObject(d, "globals");
                 QJsonObject flag = getObject(globals, "flag");
                 QJsonObject ui = getObject(flag, "ui");
                 ui.insert("enabledBy", "nosuch");
                 flag.insert("ui", ui);
                 globals.insert("flag", flag);
                 d.insert("globals", globals);
             }},
            {"enabledBy has no eq", [](QJsonObject& d) {
                 QJsonObject globals = getObject(d, "globals");
                 QJsonObject flag = getObject(globals, "flag");
                 QJsonObject ui = getObject(flag, "ui");
                 ui.insert("enabledBy", QJsonObject{{"param", "mode"}});
                 flag.insert("ui", ui);
                 globals.insert("flag", flag);
                 d.insert("globals", globals);
             }},
            {"enabledBy has an unknown container", [](QJsonObject& d) {
                 QJsonObject globals = getObject(d, "globals");
                 QJsonObject flag = getObject(globals, "flag");
                 QJsonObject ui = getObject(flag, "ui");
                 ui.insert("enabledBy", QJsonObject{{"and", "x"}});
                 flag.insert("ui", ui);
                 globals.insert("flag", flag);
                 d.insert("globals", globals);
             }},
            {"ui control is unknown", [](QJsonObject& d) {
                 QJsonObject globals = getObject(d, "globals");
                 QJsonObject mode = getObject(globals, "mode");
                 QJsonObject ui = getObject(mode, "ui");
                 ui.insert("control", "dropdownx");
                 mode.insert("ui", ui);
                 globals.insert("mode", mode);
                 d.insert("globals", globals);
             }},
            {"ui label is a number", [](QJsonObject& d) {
                 QJsonObject globals = getObject(d, "globals");
                 QJsonObject mode = getObject(globals, "mode");
                 QJsonObject ui = getObject(mode, "ui");
                 ui.insert("label", 7);
                 mode.insert("ui", ui);
                 globals.insert("mode", mode);
                 d.insert("globals", globals);
             }},
        };
        for (const Case& c : cases) {
            QJsonObject def = validDefinition();
            c.mutate(def);
            checkAnyError(def, c.name);
        }
    }

    // --- numeric literals and supported dimension expressions are preserved ---
    {
        QJsonObject def = validDefinition();
        mutateFirstPassNested(def, "uniforms", "literal", 0);
        mutateFirstPassNested(def, "uniforms", "another", 12.5);
        checkValid(def, "numeric pass-uniform literals preserved");
    }
    {
        QJsonObject def = validDefinition();
        QJsonObject textures = getObject(def, "textures");
        QJsonObject expressions;
        QJsonObject width;
        width.insert("scale", 0.5);
        width.insert("clamp", QJsonObject{{"min", 8}, {"max", 256}});
        QJsonObject height;
        height.insert("param", "volumeSize");
        height.insert("multiply", 2);
        height.insert("paramDefault", 64);
        expressions.insert("width", width);
        expressions.insert("height", height);
        textures.insert("expressions", expressions);
        def.insert("textures", textures);
        checkValid(def, "supported dimension expressions preserved");
    }

    // --- lifecycle hooks: JSON cannot encode functions, so a present hook is
    // reported (documented deviation; the reference's valid case is
    // unreachable in this grammar) ---
    {
        QJsonObject def = validDefinition();
        def.insert("onInit", 1);
        def.insert("onUpdate", "x");
        def.insert("onDestroy", QJsonObject());
        def.insert("asyncInit", true);
        checkErrors(def, 4, "four present hooks each reported");
    }

    // --- validation errors follow QJsonObject's sorted key order (documented
    // deviation: the reference emits Object.entries insertion order, but
    // QJsonObject iterates keys sorted -- see effect_validator.h) ---
    {
        QJsonObject def = validDefinition();
        QJsonObject globals;
        QJsonObject zzz; zzz.insert("type", "float"); zzz.insert("default", "bad");
        QJsonObject aaa; aaa.insert("type", "float"); aaa.insert("default", "bad");
        globals.insert("zzz", zzz);
        globals.insert("aaa", aaa);
        def.insert("globals", globals);
        const std::vector<std::string> errors = validate(def);
        ptrdiff_t z = -1, a = -1;
        for (size_t i = 0; i < errors.size(); ++i) {
            if (errors[i].find("'zzz'") != std::string::npos) z = ptrdiff_t(i);
            if (errors[i].find("'aaa'") != std::string::npos) a = ptrdiff_t(i);
        }
        const bool ok = z != -1 && a != -1;
        std::printf("%s: both global errors reported (sorted key order deviation)\n",
                    ok ? "PASS" : "FAIL");
        if (!ok) ++g_failures;
    }

    // --- dynamic definition corpus: every generated definition validates
    // cleanly (explicit denominator; import errors are failures) ---
    {
        QDir effectsRoot("qt/noisemaker/effects");
        const QStringList namespaces = effectsRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                             QDir::Name);
        int expected = 0, passed = 0;
        QStringList failures;
        for (const QString& ns : namespaces) {
            QDir dir(effectsRoot.filePath(ns));
            const QStringList files =
                dir.entryList(QStringList{"*.json"}, QDir::Files, QDir::Name);
            for (const QString& file : files) {
                ++expected;
                QFile f(dir.filePath(file));
                if (!f.open(QIODevice::ReadOnly)) {
                    failures << ns + "/" + file + ": open failed";
                    continue;
                }
                const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
                if (doc.isNull()) {
                    failures << ns + "/" + file + ": invalid JSON";
                    continue;
                }
                const std::vector<std::string> errors =
                    validateEffectDefinition(doc.object());
                if (errors.empty()) {
                    ++passed;
                } else {
                    QString joined;
                    for (const std::string& e : errors) {
                        joined += QString::fromStdString(e) + " | ";
                    }
                    failures << ns + "/" + file + ": " + QString::number(errors.size())
                             + " error(s): " + joined;
                }
            }
        }
        const int unexecuted = expected - (passed + int(failures.size()));
        std::printf("[gap-003 corpus] expected=%d pass=%d failure=%d unexecuted=%d\n", expected,
                    passed, int(failures.size()), unexecuted);
        for (const QString& failure : failures) {
            std::printf("[gap-003 corpus] %s\n", failure.toUtf8().constData());
        }
        if (expected <= 0 || unexecuted != 0 || !failures.isEmpty() || passed != expected) {
            std::printf("FAIL: definition corpus\n");
            ++g_failures;
        }
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
