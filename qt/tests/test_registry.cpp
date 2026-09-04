// Unit tests for nm::EffectRegistry (qt/noisemaker/compiler/effect_registry.{h,cpp}).
// Plain assert-style checks, no test framework dependency (matches
// test_lexer.cpp / test_parser.cpp convention).
//
// Every non-obvious assertion here traces back to a fact independently
// verified against the live reference oracle (NM_REFERENCE_ROOT tools/
// dump-registry.mjs) or a direct read of tools/dump-validate.mjs /
// tools/export-graph.mjs / the reference shaders/src/renderer/canvas.js
// before being hard-coded -- see task report "hazards encountered":
//   - starter status is the PASS-INPUT rule, not the JSON `starter` field
//     (mixer.channelCombine disagrees between the two: baked false,
//     derived true -- the one case in the whole 210-effect catalog).
//   - `ops`/starter registration is NAMESPACED KEYS ONLY; a bare func name
//     is never a valid getOp()/isStarterOp() query.
//   - vec4 -> "color" is the ONLY globals[key].type rewrite, applied at
//     REGISTRATION time (the JSON itself keeps "vec4" verbatim, e.g.
//     synth/remap.json's zone*_v* uniforms).
//
// RED (before effect_registry.cpp/enums.cpp existed): this binary failed
// to link ("undefined symbols: nm::EffectRegistry::..."). GREEN: all
// checks below print PASS and the process exits 0.

#include "../noisemaker/compiler/effect_registry.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <cstdio>

namespace {

int g_failures = 0;

void check(bool condition, const char* description) {
    if (condition) {
        std::printf("PASS: %s\n", description);
    } else {
        std::printf("FAIL: %s\n", description);
        ++g_failures;
    }
}

} // namespace

int main() {
    nm::EffectRegistry reg;
    reg.loadAll(nm::EffectRegistry::defaultDataRoot());

    // ==================================================================
    // load sanity
    // ==================================================================
    {
        check(reg.getOp(QStringLiteral("synth.noise")) != nullptr, "synth.noise op is registered");
        check(reg.getOp(QStringLiteral("filter.bc")) != nullptr, "filter.bc (hidden/deprecated) op is registered");
        check(reg.hasEffect(QStringLiteral("synth.noise")), "synth.noise effect lookup key present");
        check(reg.hasEffect(QStringLiteral("synth/noise")), "synth/noise (slash) effect lookup key present");
        check(reg.hasEffect(QStringLiteral("noise")), "bare 'noise' effect lookup key present (some effect wins it)");
    }

    // ==================================================================
    // namespaced-only ops/starter keys (reference: canvas.js
    // registerEffectWithRuntime / dump-registry.mjs / dump-validate.mjs
    // never call registerOp/registerStarterOps with a bare func name)
    // ==================================================================
    {
        check(reg.getOp(QStringLiteral("noise")) == nullptr, "bare 'noise' is NOT a valid getOp() key");
        check(reg.getOp(QStringLiteral("solid")) == nullptr, "bare 'solid' is NOT a valid getOp() key");
        check(!reg.isStarterOp(QStringLiteral("solid")),
              "isStarterOp('solid') is false for a bare name, even though synth.solid IS a starter "
              "(reference bug-for-bug: getStarterInfo's bare-name query never matches STARTER_OPS, "
              "which only ever holds namespaced keys -- verified against the oracle: "
              "'search synth\\nsolid(0.1,0.2,0.3)' with no write() produces ONLY S001, never S006)");
        check(reg.isStarterOp(QStringLiteral("synth.solid")), "isStarterOp('synth.solid') (namespaced) is true");
    }

    // ==================================================================
    // starter derivation: pass-input rule, NOT the baked JSON `starter`
    // field (the one verified disagreement in the whole catalog)
    // ==================================================================
    {
        check(reg.isStarterOp(QStringLiteral("mixer.channelCombine")),
              "mixer.channelCombine IS a starter by the pass-rule (its JSON `starter` field is false; "
              "it takes inputs via surface kwargs, not pipeline `inputs`)");
    }

    // ==================================================================
    // vec4 -> "color" registration-time rewrite (op arg `type`); the JSON
    // itself keeps "vec4" verbatim (never hand-edited/rewritten at rest)
    // ==================================================================
    {
        const nm::OpSpec* remap = reg.getOp(QStringLiteral("synth.remap"));
        check(remap != nullptr, "synth.remap op is registered");
        bool foundZoneVert = false;
        bool typeIsColor = false;
        if (remap) {
            for (const nm::ParamDef& pd : remap->args) {
                if (pd.name == QStringLiteral("zone0_v0")) {
                    foundZoneVert = true;
                    typeIsColor = (pd.type == QStringLiteral("color"));
                }
            }
        }
        check(foundZoneVert, "synth.remap has a zone0_v0 global");
        check(typeIsColor, "synth.remap zone0_v0 arg type is 'color' (registry rewrote vec4->color)");
    }

    // ==================================================================
    // args positional order matches globals DECLARATION order (Qt's
    // QJsonObject does NOT preserve parse order -- verified empirically:
    // parsing {"zebra":1,"apple":2,"mango":3,"banana":4} and iterating
    // yields alphabetical order, not source order -- so this exercises
    // the text-scanning key-order recovery directly)
    // ==================================================================
    {
        const nm::OpSpec* noise = reg.getOp(QStringLiteral("synth.noise"));
        check(noise != nullptr, "synth.noise op is registered (for order check)");
        if (noise) {
            // synth/noise.json's globals declaration order (see the file):
            // type, octaves, scaleX, scaleY, seed, wrap, ridges,
            // loopOffset, loopScale, speed, colorMode -- alphabetical would
            // be colorMode, loopOffset, loopScale, octaves, ridges, ...
            const QStringList expected = {
                QStringLiteral("type"),   QStringLiteral("octaves"),    QStringLiteral("scaleX"),
                QStringLiteral("scaleY"), QStringLiteral("seed"),       QStringLiteral("wrap"),
                QStringLiteral("ridges"), QStringLiteral("loopOffset"), QStringLiteral("loopScale"),
                QStringLiteral("speed"),  QStringLiteral("colorMode"),
            };
            bool orderMatches = (noise->args.size() == expected.size());
            if (orderMatches) {
                for (int i = 0; i < expected.size(); ++i) {
                    if (noise->args.at(i).name != expected.at(i)) {
                        orderMatches = false;
                        break;
                    }
                }
            }
            check(orderMatches, "synth.noise args preserve globals DECLARATION order (not alphabetical)");
        }
    }

    // ==================================================================
    // min/max/default/uniform presence (Undefined when absent)
    // ==================================================================
    {
        const nm::OpSpec* noise = reg.getOp(QStringLiteral("synth.noise"));
        check(noise != nullptr, "synth.noise op is registered (for min/max check)");
        if (noise) {
            const nm::ParamDef* octaves = nullptr;
            for (const nm::ParamDef& pd : noise->args) {
                if (pd.name == QStringLiteral("octaves")) octaves = &pd;
            }
            check(octaves != nullptr, "synth.noise has an 'octaves' arg");
            if (octaves) {
                check(octaves->hasMin() && octaves->minAsDouble() == 1.0, "octaves.min == 1");
                check(octaves->hasMax() && octaves->maxAsDouble() == 8.0, "octaves.max == 8");
                check(octaves->hasDefault() && octaves->defaultValue.toDouble() == 2.0, "octaves.default == 2");
            }
        }
    }

    // ==================================================================
    // choices with no explicit enum synthesize "<ns>.<func>.<key>" and
    // register into the PROJECT enum tree (canvas.js parity)
    // ==================================================================
    {
        const nm::OpSpec* noise = reg.getOp(QStringLiteral("synth.noise"));
        const nm::ParamDef* type = nullptr;
        if (noise) {
            for (const nm::ParamDef& pd : noise->args) {
                if (pd.name == QStringLiteral("type")) type = &pd;
            }
        }
        check(type != nullptr, "synth.noise has a 'type' arg");
        if (type) {
            check(type->hasEnumPath() && type->enumPathString() == QStringLiteral("synth.noise.type"),
                  "synth.noise.type synthesizes enumPath 'synth.noise.type'");
            check(type->hasChoices(), "synth.noise.type arg carries verbatim choices for the ops dump");
        }
        const QJsonValue simplex = reg.enums().tryGetHead(QStringLiteral("synth"));
        bool resolvesTo10 = false;
        if (simplex.isObject()) {
            const QJsonValue noiseNs = simplex.toObject().value(QStringLiteral("noise"));
            if (noiseNs.isObject()) {
                const QJsonValue typeNs = noiseNs.toObject().value(QStringLiteral("type"));
                if (typeNs.isObject()) {
                    const QJsonValue simplexLeaf = typeNs.toObject().value(QStringLiteral("simplex"));
                    resolvesTo10 = simplexLeaf.isObject()
                        && simplexLeaf.toObject().value(QStringLiteral("value")).toDouble() == 10.0;
                }
            }
        }
        check(resolvesTo10, "project enum tree resolves synth.noise.type.simplex -> 10");
    }

    // ==================================================================
    // choices ':' group-header entries are skipped for enum REGISTRATION
    // (loopOffset's choices include "Shapes:": null, "Directional:": null,
    // "Misc:": null) but kept VERBATIM in the dumped/stored `choices`
    // field (two different consumers, two different filtering rules)
    // ==================================================================
    {
        const nm::OpSpec* noise = reg.getOp(QStringLiteral("synth.noise"));
        const nm::ParamDef* loopOffset = nullptr;
        if (noise) {
            for (const nm::ParamDef& pd : noise->args) {
                if (pd.name == QStringLiteral("loopOffset")) loopOffset = &pd;
            }
        }
        check(loopOffset != nullptr, "synth.noise has a 'loopOffset' arg");
        if (loopOffset) {
            check(loopOffset->hasChoices() && loopOffset->choicesObject().contains(QStringLiteral("Shapes:")),
                  "loopOffset's verbatim choices still carry the 'Shapes:' group header");
        }
        const QJsonValue synthNs = reg.enums().tryGetHead(QStringLiteral("synth"));
        bool groupHeaderNotRegistered = true;
        if (synthNs.isObject()) {
            const QJsonValue noiseNs = synthNs.toObject().value(QStringLiteral("noise"));
            if (noiseNs.isObject()) {
                const QJsonValue loopOffsetNs = noiseNs.toObject().value(QStringLiteral("loopOffset"));
                if (loopOffsetNs.isObject()) {
                    groupHeaderNotRegistered = !loopOffsetNs.toObject().contains(QStringLiteral("Shapes:"));
                }
            }
        }
        check(groupHeaderNotRegistered, "'Shapes:' group header is NOT registered as an enum leaf");
    }

    // ==================================================================
    // std enum tree: fixed, always present regardless of any effect load
    // (channel/color/oscType/oscKind/midiMode/audioBand/palette)
    // ==================================================================
    {
        const QJsonValue oscKind = reg.enums().std().value(QStringLiteral("oscKind"));
        check(oscKind.isObject(), "std().oscKind is a subtree");
        const double noiseVal = oscKind.toObject().value(QStringLiteral("noise")).toObject()
                                     .value(QStringLiteral("value")).toDouble(-1);
        const double noise1dVal = oscKind.toObject().value(QStringLiteral("noise1d")).toObject()
                                       .value(QStringLiteral("value")).toDouble(-2);
        check(noiseVal == 5.0 && noise1dVal == 5.0, "oscKind.noise == oscKind.noise1d == 5 (alias)");

        const QJsonValue audioBand = reg.enums().std().value(QStringLiteral("audioBand"));
        const double rawVal = audioBand.toObject().value(QStringLiteral("raw")).toObject()
                                  .value(QStringLiteral("value")).toDouble(-1);
        check(rawVal == 4.0, "audioBand.raw == 4");

        const QJsonValue palette = reg.enums().std().value(QStringLiteral("palette"));
        check(palette.isObject(), "std().palette is a subtree");
        const double noneIdx = palette.toObject().value(QStringLiteral("none")).toObject()
                                    .value(QStringLiteral("value")).toDouble(-1);
        const double solarisIdx = palette.toObject().value(QStringLiteral("solaris")).toObject()
                                       .value(QStringLiteral("value")).toDouble(-1);
        check(noneIdx == 0.0, "palette.none == 0 (positional index, verified against share/palettes.json)");
        check(solarisIdx == 42.0, "palette.solaris == 42 (positional index, verified against share/palettes.json)");
    }

    // ==================================================================
    // paramAliases: non-empty maps only (empty maps are inert)
    // ==================================================================
    {
        QJsonObject kwargs;
        kwargs.insert(QStringLiteral("noiseType"), 3.0);
        const QStringList warnings = reg.resolveParamAliases(QStringLiteral("synth.noise"), kwargs);
        check(warnings.size() == 1, "resolveParamAliases produces one warning for one deprecated kwarg");
        check(!kwargs.contains(QStringLiteral("noiseType")) && kwargs.contains(QStringLiteral("type"))
                  && kwargs.value(QStringLiteral("type")).toDouble() == 3.0,
              "resolveParamAliases renames noiseType -> type in place, preserving the value");

        QJsonObject emptyAliasKwargs;
        emptyAliasKwargs.insert(QStringLiteral("brightness"), 1.0);
        const QStringList noWarnings = reg.resolveParamAliases(QStringLiteral("filter.bc"), emptyAliasKwargs);
        check(noWarnings.isEmpty(), "an effect with an EMPTY paramAliases map ({}) produces no warnings");
    }

    // ==================================================================
    // effect aliases: hidden + deprecatedBy
    // ==================================================================
    {
        const QString warning = reg.checkEffectAlias(QStringLiteral("filter.bc"));
        check(!warning.isEmpty() && warning.contains(QStringLiteral("'bc' is deprecated"))
                  && warning.contains(QStringLiteral("use 'adjust' instead")),
              "checkEffectAlias('filter.bc') warns to use 'adjust'");
        check(reg.checkEffectAlias(QStringLiteral("synth.noise")).isEmpty(),
              "checkEffectAlias('synth.noise') (no alias) returns empty");
    }

    // ==================================================================
    // define-map: globals carrying `define`
    // ==================================================================
    {
        const QJsonObject dm = reg.defineMap();
        const QJsonValue noiseDefs = dm.value(QStringLiteral("synth.noise"));
        check(noiseDefs.isObject() && noiseDefs.toObject().value(QStringLiteral("type")).toString()
                  == QStringLiteral("NOISE_TYPE"),
              "defineMap['synth.noise'].type == 'NOISE_TYPE'");
    }

    // ==================================================================
    // dumpSummary() surface shape (the check_registry.mjs comparison keys)
    // ==================================================================
    {
        const QJsonObject dump = reg.dumpSummary();
        check(dump.contains(QStringLiteral("ops")) && dump.contains(QStringLiteral("enums"))
                  && dump.contains(QStringLiteral("paramAliases")) && dump.contains(QStringLiteral("effectAliases"))
                  && dump.contains(QStringLiteral("effectKeys")),
              "dumpSummary() has exactly the five gate keys");
        check(dump.value(QStringLiteral("ops")).toObject().size() == 210, "dumpSummary().ops has 210 entries");
        const QJsonObject remapArg = dump.value(QStringLiteral("ops")).toObject().value(QStringLiteral("synth.remap"))
                                          .toObject();
        check(!remapArg.isEmpty(), "dumpSummary().ops has a synth.remap entry");
        const QJsonObject bcArg = dump.value(QStringLiteral("ops")).toObject().value(QStringLiteral("filter.bc")).toObject();
        bool brightnessHasNoEnumOrChoices = true;
        bool brightnessHasUniformMinMax = false;
        for (const QJsonValue& a : bcArg.value(QStringLiteral("args")).toArray()) {
            const QJsonObject ao = a.toObject();
            if (ao.value(QStringLiteral("name")).toString() == QStringLiteral("brightness")) {
                brightnessHasNoEnumOrChoices = !ao.contains(QStringLiteral("enum"))
                    && !ao.contains(QStringLiteral("enumPath")) && !ao.contains(QStringLiteral("choices"));
                brightnessHasUniformMinMax = ao.contains(QStringLiteral("uniform")) && ao.contains(QStringLiteral("min"))
                    && ao.contains(QStringLiteral("max"));
            }
        }
        check(brightnessHasNoEnumOrChoices,
              "filter.bc's brightness arg omits enum/enumPath/choices entirely (absent, not null)");
        check(brightnessHasUniformMinMax, "filter.bc's brightness arg carries uniform/min/max (all present)");
        check(dump.value(QStringLiteral("effectKeys")).toObject().value(QStringLiteral("synth.noise")).toString()
                  == QStringLiteral("synth.noise"),
              "effectKeys['synth.noise'] fingerprints to 'synth.noise'");
    }

    if (g_failures == 0) {
        std::printf("ALL PASS (test_registry)\n");
        return 0;
    }
    std::printf("%d FAILURE(S) (test_registry)\n", g_failures);
    return 1;
}
