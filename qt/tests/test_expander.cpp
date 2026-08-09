// Unit tests for nm::expand (qt/noisemaker/compiler/expander.{h,cpp}) and
// nm::compileGraphJson (dsl_compiler.{h,cpp}). Plain assert-style checks,
// no test framework dependency (matches test_lexer.cpp/test_registry.cpp
// convention).
//
// Every non-obvious numeric fixture here was ground-truthed against the
// live reference oracle (NM_REFERENCE_ROOT=... node tools/dump-expand.mjs
// qt/noisemaker/effects <probe>.dsl) BEFORE being hard-coded — see task
// report "TDD RED/GREEN evidence" for the exact probe commands and their
// raw output. In particular the define-SORT-ORDER test (classicNoisedeck/
// noise.json) empirically DISPROVES the task brief's own claim that
// defines are "sorted by define name" — the live oracle's program suffix
// is `__COLOR_MODE_6__LOOP_OFFSET_300__METRIC_0__REFRACT_MODE_2__NOISE_TYPE_10`,
// i.e. REFRACT_MODE before NOISE_TYPE, which is the GLOBAL-name order
// (colorMode<loopOffset<metric<refractMode<type), not the define-name
// order (which would put NOISE_TYPE before REFRACT_MODE). See expander.h's
// file header for the full analysis.
//
// RED (before dim.cpp/resources.cpp/expander.cpp/dsl_compiler.cpp
// existed): this binary failed to link ("undefined symbols: nm::expand,
// nm::compileGraphJson, ..."). GREEN: all checks below print PASS and the
// process exits 0.

#include "../noisemaker/compiler/dsl_compiler.h"
#include "../noisemaker/compiler/effect_registry.h"
#include "../noisemaker/compiler/expander.h"
#include "../noisemaker/compiler/lexer.h"
#include "../noisemaker/compiler/parser.h"
#include "../noisemaker/compiler/validator.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <cmath>
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

bool numEq(double a, double b) { return std::fabs(a - b) <= 1e-9 * std::max(1.0, std::max(std::fabs(a), std::fabs(b))); }

nm::EffectRegistry& registry() {
    static nm::EffectRegistry reg;
    static bool loaded = false;
    if (!loaded) {
        reg.loadAll(nm::EffectRegistry::defaultDataRoot());
        loaded = true;
    }
    return reg;
}

nm::ExpandResult expandSrc(const QString& src) {
    const QJsonArray tokens = nm::lex(src);
    const QJsonObject ast = nm::parse(tokens);
    const QJsonObject validated = nm::validate(ast, registry());
    return nm::expand(validated, registry());
}

const nm::ExpandedPass* findPass(const nm::ExpandResult& r, const QString& idSuffix) {
    for (const nm::ExpandedPass& p : r.passes) {
        if (p.id.endsWith(idSuffix)) return &p;
    }
    return nullptr;
}

QString findInput(const nm::ExpandedPass& pass, const QString& uniformName) {
    for (const auto& kv : pass.inputs) {
        if (kv.first == uniformName) return kv.second;
    }
    return QString();
}

QString findOutput(const nm::ExpandedPass& pass, const QString& attachment) {
    for (const auto& kv : pass.outputs) {
        if (kv.first == attachment) return kv.second;
    }
    return QString();
}

} // namespace

int main() {
    // ======================================================================
    // Define collection + SORT ORDER (reference/03 §4.5; expander.h file
    // header "PARITY-CRITICAL DEVIATION FROM THE TASK BRIEF").
    // Oracle probe: `search classicNoisedeck\nnoise().write(o0)\nrender(o0)`
    // ======================================================================
    {
        const QString src = QStringLiteral("search classicNoisedeck\nnoise().write(o0)\nrender(o0)\n");
        const nm::ExpandResult r = expandSrc(src);
        check(r.errors.isEmpty(), "classicNoisedeck.noise(): no expand errors");
        const nm::ExpandedPass* p0 = findPass(r, QStringLiteral("_pass_0"));
        check(p0 != nullptr, "classicNoisedeck.noise(): effect pass 0 exists");
        if (p0) {
            check(p0->program == QStringLiteral("node_0_noise__COLOR_MODE_6__LOOP_OFFSET_300__METRIC_0__REFRACT_MODE_2__NOISE_TYPE_10"),
                  "define suffix is sorted by GLOBAL name (colorMode<loopOffset<metric<refractMode<type), "
                  "NOT by define name (which would put NOISE_TYPE before REFRACT_MODE) -- oracle-verified");
            check(p0->uniforms.value(QStringLiteral("type")).toDouble() == 10.0,
                  "RAW pass.uniforms still carries 'type' (validator pre-fills step.args with EVERY "
                  "global's default, so the args-processing loop always sees it) -- only the "
                  "orchestrator's normalize step promotes/removes it");
        }
    }

    // ======================================================================
    // Define-vs-uniform routing (brief Step 1's named test): synth.noise
    // NOISE_TYPE (define, no .uniform) vs colorMode (choices+.uniform,
    // stays a runtime uniform, no .define). Oracle probe:
    // `search synth\nnoise(type: simplex, colorMode: mono).write(o0)\nrender(o0)`
    // ======================================================================
    {
        const QString src = QStringLiteral("search synth\nnoise(type: simplex, colorMode: mono).write(o0)\nrender(o0)\n");
        const nm::ExpandResult r = expandSrc(src);
        check(r.errors.isEmpty(), "synth.noise(type:simplex,colorMode:mono): no expand errors");
        const nm::ExpandedPass* p0 = findPass(r, QStringLiteral("_pass_0"));
        check(p0 != nullptr, "synth.noise: effect pass 0 exists");
        if (p0) {
            check(p0->program == QStringLiteral("node_0_noise__LOOP_OFFSET_300__NOISE_TYPE_10"),
                  "synth.noise program suffix: only NOISE_TYPE/LOOP_OFFSET (define-carrying globals) "
                  "contribute, colorMode does NOT (it has no .define)");
            check(p0->uniforms.value(QStringLiteral("type")).toDouble() == 10.0,
                  "raw pass.uniforms['type'] == 10 (simplex) -- present pre-normalization");
            check(p0->uniforms.value(QStringLiteral("colorMode")).toDouble() == 0.0,
                  "raw pass.uniforms['colorMode'] == 0 (mono) -- a genuine runtime uniform, "
                  "resolved via its OWN choices, never define-suffix-relevant");
        }

        // End-to-end through the orchestrator's normalize (dsl_compiler.cpp):
        // 'type'/'loopOffset' must be PROMOTED OUT of uniforms into defines
        // (by GLOBAL key, per export-graph.mjs normalizePass); 'colorMode'
        // must REMAIN a plain runtime uniform (no matching defineMap entry).
        try {
            const QJsonObject graph = nm::compileGraphJson(src, registry());
            const QJsonArray passes = graph.value(QStringLiteral("passes")).toArray();
            const QJsonObject effectPass = passes.at(0).toObject();
            const QJsonObject defines = effectPass.value(QStringLiteral("defines")).toObject();
            const QJsonObject uniforms = effectPass.value(QStringLiteral("uniforms")).toObject();
            check(defines.value(QStringLiteral("NOISE_TYPE")).toInt(-1) == 10,
                  "normalized graph: defines.NOISE_TYPE == 10 (promoted from uniforms.type)");
            check(defines.value(QStringLiteral("LOOP_OFFSET")).toInt(-1) == 300,
                  "normalized graph: defines.LOOP_OFFSET == 300 (default, promoted)");
            check(!uniforms.contains(QStringLiteral("type")), "normalized graph: uniforms no longer has 'type' (promoted away)");
            check(!uniforms.contains(QStringLiteral("loopOffset")), "normalized graph: uniforms no longer has 'loopOffset'");
            check(uniforms.contains(QStringLiteral("colorMode")) && uniforms.value(QStringLiteral("colorMode")).toDouble() == 0.0,
                  "normalized graph: uniforms.colorMode == 0 (mono) -- STAYS a runtime uniform, "
                  "never promoted (no defineMap entry for it)");
            check(!defines.contains(QStringLiteral("colorMode")) && !defines.contains(QStringLiteral("COLOR_MODE")),
                  "normalized graph: defines has no colorMode/COLOR_MODE entry (it isn't a define at all)");
        } catch (const std::exception& e) {
            check(false, (QStringLiteral("compileGraphJson threw: ") + e.what()).toUtf8().constData());
        }
    }

    // ======================================================================
    // Palette expansion (reference/03 §7). classicNoisedeck.noise()'s
    // default `palette:2` (1-based) -> PALETTES[1] = fiveG. Distinctive
    // non-round floats rule out "still just the plain vec3 default"
    // (paletteOffset/Amp/Freq/Phase/Mode are ALSO independently-declared
    // vec3/int globals with their OWN, different, defaults).
    // ======================================================================
    {
        const QString src = QStringLiteral("search classicNoisedeck\nnoise().write(o0)\nrender(o0)\n");
        const nm::ExpandResult r = expandSrc(src);
        const nm::ExpandedPass* p0 = findPass(r, QStringLiteral("_pass_0"));
        check(p0 != nullptr, "palette test: effect pass 0 exists");
        if (p0) {
            const QJsonArray amp = p0->uniforms.value(QStringLiteral("paletteAmp")).toArray();
            const QJsonArray phase = p0->uniforms.value(QStringLiteral("palettePhase")).toArray();
            check(amp.size() == 3 && numEq(amp.at(0).toDouble(), 0.56851584) && numEq(amp.at(1).toDouble(), 0.7740668)
                      && numEq(amp.at(2).toDouble(), 0.23485267),
                  "palette index 2 expands to fiveG's paletteAmp [.56851584,.7740668,.23485267] "
                  "(overwrites the plain vec3 default [.5,.5,.5])");
            check(phase.size() == 3 && numEq(phase.at(0).toDouble(), 0.727029) && numEq(phase.at(1).toDouble(), 0.08039695)
                      && numEq(phase.at(2).toDouble(), 0.10427457),
                  "palette index 2 expands to fiveG's palettePhase [.727029,.08039695,.10427457]");
            check(p0->uniforms.value(QStringLiteral("paletteMode")).toDouble() == 3.0,
                  "palette index 2 (fiveG) has classicNoisedeck mode 3 (rgb)");
        }
    }

    // ======================================================================
    // Input-lane threading (2D chain) — noise().blur() must thread
    // noise's node output into blur's inputTex, not a bare placeholder.
    // ======================================================================
    {
        const QString src = QStringLiteral("search synth, filter\nnoise().blur().write(o0)\nrender(o0)\n");
        const nm::ExpandResult r = expandSrc(src);
        check(r.errors.isEmpty(), "noise().blur(): no expand errors");
        const nm::ExpandedPass* noisePass = findPass(r, QStringLiteral("node_0_pass_0"));
        const nm::ExpandedPass* blurPass = findPass(r, QStringLiteral("node_1_pass_0"));
        check(noisePass != nullptr && blurPass != nullptr, "noise().blur(): both effect passes exist");
        if (noisePass && blurPass) {
            const QString noiseOut = findOutput(*noisePass, QStringLiteral("fragColor"));
            const QString blurIn = findInput(*blurPass, QStringLiteral("inputTex"));
            check(!noiseOut.isEmpty() && noiseOut == QStringLiteral("node_0_out"),
                  "noise's fragColor output registers as node_0_out (not the last step -- write() "
                  "builtin follows, so no last-pass-to-surface fusion here)");
            check(!blurIn.isEmpty() && blurIn == noiseOut,
                  "blur's inputTex resolves to EXACTLY noise's output virtual texture id (2D lane threading)");
        }
    }

    // ======================================================================
    // Agent-lane threading + texture spec expansion + scoped params
    // (reference/03 §6.1/§6.3). Oracle probe:
    // `search points, synth, render\nsolid().pointsEmit(stateSize: 128).physarum().pointsRender().write(o0)\nrender(o0)`
    // ======================================================================
    {
        const QString src = QStringLiteral(
            "search points, synth, render\nsolid().pointsEmit(stateSize: 128).physarum().pointsRender().write(o0)\nrender(o0)\n");
        const nm::ExpandResult r = expandSrc(src);
        check(r.errors.isEmpty(), "physarum agent chain: no expand errors");
        check(r.textureSpecs.contains(QStringLiteral("global_xyz_node_1")),
              "physarum agent chain: global_xyz scoped to its creating node (pointsEmit == node_1)");
        if (r.textureSpecs.contains(QStringLiteral("global_xyz_node_1"))) {
            const QJsonObject spec = r.textureSpecs.value(QStringLiteral("global_xyz_node_1")).toObject();
            const QJsonObject width = spec.value(QStringLiteral("width")).toObject();
            check(width.value(QStringLiteral("param")).toString() == QStringLiteral("stateSize_node_1"),
                  "global_xyz_node_1's width.param is scoped: 'stateSize' -> 'stateSize_node_1'");
            check(width.value(QStringLiteral("default")).toInt(-1) == 256,
                  "scoping rewrites ONLY the param name, not other DimSpec fields (default:256 preserved)");
            check(spec.value(QStringLiteral("format")).toString() == QStringLiteral("rgba32f"),
                  "global_xyz_node_1 format passes through unchanged (rgba32f)");
        }
        // Chain-scoped (non-particle) global texture: physarum's own pheromone trail.
        check(r.textureSpecs.contains(QStringLiteral("global_physarum_pheromone_chain_0")),
              "physarum's own global_physarum_pheromone texture is CHAIN-scoped (not particle-scoped: "
              "the name doesn't match the particle regex xyz|vel|rgba|points_trail|life_data)");
    }

    // ======================================================================
    // resolveEnum (std tree only) + UnsupportedDsl smoke check: verify the
    // exception type is nm::UnsupportedDsl (shared with validator.h), not
    // a new/duplicate type -- this catalog never actually triggers the
    // compute/MRT throw site (verified: no effect JSON declares entryPoint/
    // workgroups/storageBuffers/storageTextures on any pass), so this is a
    // compile-time/type check, not a behavioral one.
    // ======================================================================
    {
        bool isRuntimeError = false;
        try {
            throw nm::UnsupportedDsl(QStringLiteral("smoke"));
        } catch (const nm::UnsupportedDsl&) {
            isRuntimeError = true;
        }
        check(isRuntimeError, "nm::UnsupportedDsl (expander.h) is nm::UnsupportedDsl (validator.h) -- one shared type");
    }

    if (g_failures == 0) {
        std::printf("ALL PASS (test_expander)\n");
        return 0;
    }
    std::printf("%d FAILURE(S) (test_expander)\n", g_failures);
    return 1;
}
