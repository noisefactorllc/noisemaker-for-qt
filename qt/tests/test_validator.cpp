// Unit tests for nm::validate (qt/noisemaker/compiler/validator.{h,cpp}).
// Plain assert-style checks, no test framework dependency (matches
// test_lexer.cpp / test_parser.cpp / test_registry.cpp convention).
//
// Every non-trivial expected value here traces back to a small .dsl probe
// run through the LIVE reference oracle (NM_REFERENCE_ROOT=... node
// tools/dump-validate.mjs qt/noisemaker/effects <probe>.dsl) before being
// hard-coded -- see task report "TDD RED/GREEN evidence" and "hazards
// encountered". Several hazards this suite specifically locks in:
//   - isStarterOp's bare-name limitation: a starter chain with NO write()
//     produces S006 only when reached through an explicit from()-resolved
//     namespace, never for a plain bare call (verified: `search synth
//     \nsolid(0.1,0.2,0.3)` alone produces ONLY S001, never S006).
//   - "member"-typed params (filter.channel et al.) silently fall back to
//     default on an unresolved value -- NO diagnostic. Every OTHER
//     enum/choices-bearing param (declared plain "int"/"float"/"palette")
//     dispatches through the NUMERIC resolver instead and DOES push S003.
//   - diagnostic `location` is `{line}` only, never `{line,column}`.
//   - `nodeId` is present (possibly null) only for Subchain-triggered
//     diagnostics (the only AST node type with a literal `id` field).
//
// RED (before validator.cpp existed): this binary failed to link
// ("undefined symbols: nm::validate(...)"). GREEN: all checks below print
// PASS and the process exits 0.

#include "../noisemaker/compiler/ast.h"
#include "../noisemaker/compiler/diagnostics.h"
#include "../noisemaker/compiler/effect_registry.h"
#include "../noisemaker/compiler/lexer.h"
#include "../noisemaker/compiler/parser.h"
#include "../noisemaker/compiler/validator.h"

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

nm::EffectRegistry& registry() {
    static nm::EffectRegistry reg;
    static bool loaded = false;
    if (!loaded) {
        reg.loadAll(nm::EffectRegistry::defaultDataRoot());
        loaded = true;
    }
    return reg;
}

QJsonObject validateSrc(const QString& src) {
    return nm::validate(nm::parse(nm::lex(src)), registry());
}

// One chain step out of plans[0].chain, by 0-based index.
QJsonObject step(const QJsonObject& out, int planIdx, int stepIdx) {
    return out.value(QStringLiteral("plans")).toArray().at(planIdx).toObject()
        .value(QStringLiteral("chain")).toArray().at(stepIdx).toObject();
}

QJsonObject args(const QJsonObject& out, int planIdx, int stepIdx) {
    return step(out, planIdx, stepIdx).value(QStringLiteral("args")).toObject();
}

QJsonArray diags(const QJsonObject& out) { return out.value(QStringLiteral("diagnostics")).toArray(); }

bool anyDiagCode(const QJsonObject& out, const QString& code) {
    for (const QJsonValue& d : diags(out)) {
        if (d.toObject().value(QStringLiteral("code")).toString() == code) return true;
    }
    return false;
}

} // namespace

int main() {
    // ==================================================================
    // numeric arg resolution: clamp + S002, and pass-through when in range
    // ==================================================================
    {
        const QJsonObject out = validateSrc(QStringLiteral("search synth\nnoise(octaves: 20).write(o0)\nrender(o0)\n"));
        check(args(out, 0, 0).value(QStringLiteral("octaves")).toDouble() == 8.0, "octaves: 20 clamps to max 8");
        const QJsonArray ds = diags(out);
        check(ds.size() == 1 && ds.at(0).toObject().value(QStringLiteral("code")).toString() == QStringLiteral("S002"),
              "exactly one S002 diagnostic for the clamp");
        check(ds.at(0).toObject().value(QStringLiteral("message")).toString()
                  == QStringLiteral("Argument out of range for 'octaves' in noise() (got 20, clamped to 8)"),
              "S002 message text matches the oracle exactly (JS-style number formatting)");

        const QJsonObject ok = validateSrc(QStringLiteral("search synth\nnoise(octaves: 5).write(o0)\nrender(o0)\n"));
        check(args(ok, 0, 0).value(QStringLiteral("octaves")).toDouble() == 5.0, "octaves: 5 (in range) passes through unclamped");
        check(diags(ok).isEmpty(), "no diagnostics when the value is in range");
    }

    // ==================================================================
    // enum resolution: numeric-with-synthesized-enum path (int-typed
    // globals with `choices`, e.g. noise.type) -- DIFFERENT from the
    // dedicated "member" type (see below): resolves via def.enum on the
    // numeric resolver, pushes S003 on failure.
    // ==================================================================
    {
        const QJsonObject out = validateSrc(QStringLiteral("search synth\nnoise(type: simplex).write(o0)\nrender(o0)\n"));
        check(args(out, 0, 0).value(QStringLiteral("type")).toDouble() == 10.0, "type: simplex resolves to choices value 10");
        check(diags(out).isEmpty(), "no diagnostics on a valid enum choice");

        const QJsonObject bad = validateSrc(QStringLiteral("search synth\nnoise(type: bogus).write(o0)\nrender(o0)\n"));
        check(args(bad, 0, 0).value(QStringLiteral("type")).toDouble() == 10.0, "type: bogus (unresolvable) falls back to default 10");
        const QJsonArray bd = diags(bad);
        check(bd.size() == 1 && bd.at(0).toObject().value(QStringLiteral("code")).toString() == QStringLiteral("S003")
                  && bd.at(0).toObject().value(QStringLiteral("message")).toString()
                         == QStringLiteral("Variable used before assignment: 'bogus'"),
              "numeric-with-enum path pushes S003 on an unresolvable Ident (matches the oracle's exact message)");
    }

    // ==================================================================
    // dedicated "member" type (filter.channel, filter.palette,
    // filter3d.palette3d, synth.osc2d ONLY) -- resolves against a STD
    // enum via an explicit `enum` field; SILENTLY falls back on failure,
    // no diagnostic at all (verified against the oracle).
    // ==================================================================
    {
        const QJsonObject out = validateSrc(
            QStringLiteral("search filter, synth\nnoise().channel(channel: g).write(o0)\nrender(o0)\n"));
        check(args(out, 0, 1).value(QStringLiteral("channel")).toDouble() == 1.0, "channel: g resolves via stdEnums.channel.g == 1");
        check(diags(out).isEmpty(), "no diagnostics on a valid member value");

        const QJsonObject bad = validateSrc(
            QStringLiteral("search filter, synth\nnoise().channel(channel: bogus).write(o0)\nrender(o0)\n"));
        check(args(bad, 0, 1).value(QStringLiteral("channel")).toDouble() == 0.0,
              "channel: bogus (unresolvable) SILENTLY falls back to default 'channel.r' == 0");
        check(diags(bad).isEmpty(),
              "member-type dispatch pushes NO diagnostic on failure (unlike the numeric-with-enum path above)");
    }

    // ==================================================================
    // vec3 arg resolution (+ default passthrough)
    // ==================================================================
    {
        const QJsonObject out = validateSrc(
            QStringLiteral("search classicNoisedeck\nnoise(paletteOffset: vec3(0.1,0.2,0.3)).write(o0)\nrender(o0)\n"));
        const QJsonArray po = args(out, 0, 0).value(QStringLiteral("paletteOffset")).toArray();
        check(po.size() == 3 && po.at(0).toDouble() == 0.1 && po.at(1).toDouble() == 0.2 && po.at(2).toDouble() == 0.3,
              "vec3(0.1,0.2,0.3) resolves to [0.1,0.2,0.3]");

        const QJsonObject dflt = validateSrc(QStringLiteral("search classicNoisedeck\nnoise().write(o0)\nrender(o0)\n"));
        const QJsonArray poDflt = args(dflt, 0, 0).value(QStringLiteral("paletteOffset")).toArray();
        check(poDflt.size() == 3 && poDflt.at(0).toDouble() == 0.5 && poDflt.at(1).toDouble() == 0.5 && poDflt.at(2).toDouble() == 0.5,
              "paletteOffset defaults to [0.5,0.5,0.5] when omitted");
    }

    // ==================================================================
    // "numeric catch-all" for types outside the 8 explicitly dispatched
    // ones (vec2/mat3/palette/...): an array default passes through
    // verbatim; a bare Number override clamps as a SCALAR even though the
    // default is an array (verified against the oracle: synth.media).
    // ==================================================================
    {
        const QJsonObject dflt = validateSrc(QStringLiteral("search synth\nmedia().write(o0)\nrender(o0)\n"));
        const QJsonArray sz = args(dflt, 0, 0).value(QStringLiteral("imageSize")).toArray();
        check(sz.size() == 2 && sz.at(0).toDouble() == 1024.0 && sz.at(1).toDouble() == 1024.0,
              "vec2-typed imageSize keeps its [1024,1024] array default verbatim (falls through to the numeric resolver)");

        const QJsonObject scalar = validateSrc(QStringLiteral("search synth\nmedia(imageSize: 5).write(o0)\nrender(o0)\n"));
        check(args(scalar, 0, 0).value(QStringLiteral("imageSize")).toDouble() == 5.0,
              "a bare Number override on a vec2-typed param clamps as a SCALAR, not an array");
    }

    // ==================================================================
    // boolean arg resolution
    // ==================================================================
    {
        const QJsonObject out = validateSrc(QStringLiteral("search synth\nnoise(wrap: false).write(o0)\nrender(o0)\n"));
        check(args(out, 0, 0).value(QStringLiteral("wrap")).toBool(true) == false, "wrap: false resolves to false");
        check(diags(out).isEmpty(), "no diagnostics for a valid boolean literal");
    }

    // ==================================================================
    // color arg resolution (hex literal -> RGBA array; T8's parser never
    // emits Color.hex, so this always goes through Color.value)
    // ==================================================================
    {
        const QJsonObject out = validateSrc(QStringLiteral("search synth\nsolid(color: #ff0000).write(o0)\nrender(o0)\n"));
        const QJsonArray c = args(out, 0, 0).value(QStringLiteral("color")).toArray();
        check(c.size() == 4 && c.at(0).toDouble() == 1.0 && c.at(1).toDouble() == 0.0 && c.at(2).toDouble() == 0.0 && c.at(3).toDouble() == 1.0,
              "#ff0000 resolves to color [1,0,0,1]");

        // A bare Number positional arg cannot satisfy a color-typed param
        // (solid's sole global is "color", not separate r/g/b) -- falls
        // back to the default with S002 (verified against the oracle).
        const QJsonObject positional = validateSrc(QStringLiteral("search synth\nsolid(0.1,0.2,0.3).write(o0)\nrender(o0)\n"));
        const QJsonArray pc = args(positional, 0, 0).value(QStringLiteral("color")).toArray();
        check(pc.size() == 3 && pc.at(0).toDouble() == 0.5 && pc.at(1).toDouble() == 0.5 && pc.at(2).toDouble() == 0.5,
              "solid(0.1,0.2,0.3): a bare Number can't satisfy the single color-typed param, falls back to default [0.5,0.5,0.5]");
        check(anyDiagCode(positional, QStringLiteral("S002")), "S002 pushed for the out-of-type positional color arg");
    }

    // ==================================================================
    // surface arg resolution: read() passthrough AND an inline nested
    // chain (a second effect call used directly as a surface argument,
    // flattened into its own temp step)
    // ==================================================================
    {
        const QJsonObject viaRead = validateSrc(QStringLiteral(
            "search mixer, synth\nnoise().blendMode(tex: read(o0), mode: hardLight, mix: 0.5).write(o1)\nrender(o1)\n"));
        check(step(viaRead, 0, 1).value(QStringLiteral("op")).toString() == QStringLiteral("mixer.blendMode"), "blendMode step present");
        const QJsonObject texArg = args(viaRead, 0, 1).value(QStringLiteral("tex")).toObject();
        check(texArg.value(QStringLiteral("kind")).toString() == QStringLiteral("output") && texArg.value(QStringLiteral("name")).toString() == QStringLiteral("o0"),
              "tex: read(o0) resolves to {kind:output,name:o0} (no extra chain step)");

        const QJsonObject inline_ = validateSrc(QStringLiteral(
            "search mixer, synth\nnoise().blendMode(tex: noise(seed: 2), mode: hardLight, mix: 0.5).write(o1)\nrender(o1)\n"));
        // chain: [0]=noise(), [1]=noise(seed:2) (flattened FIRST, since arg
        // resolution recurses via processChain before the blendMode step's
        // own temp index is allocated), [2]=blendMode, [3]=_write.
        check(step(inline_, 0, 0).value(QStringLiteral("chain")).isUndefined(), "sanity: step() indexes the flattened chain array directly");
        const QJsonArray chain = inline_.value(QStringLiteral("plans")).toArray().first().toObject().value(QStringLiteral("chain")).toArray();
        check(chain.size() == 4, "inline surface chain: noise() + noise(seed:2) + blendMode + _write == 4 steps");
        check(chain.at(1).toObject().value(QStringLiteral("op")).toString() == QStringLiteral("synth.noise")
                  && chain.at(1).toObject().value(QStringLiteral("args")).toObject().value(QStringLiteral("seed")).toDouble() == 2.0,
              "the inline noise(seed:2) becomes its OWN temp step, allocated before blendMode's");
        const QJsonObject blendStep = chain.at(2).toObject();
        check(blendStep.value(QStringLiteral("op")).toString() == QStringLiteral("mixer.blendMode"), "blendMode is temp index 2");
        const QJsonObject texObj = blendStep.value(QStringLiteral("args")).toObject().value(QStringLiteral("tex")).toObject();
        check(texObj.value(QStringLiteral("kind")).toString() == QStringLiteral("temp") && texObj.value(QStringLiteral("index")).toInt(-1) == 1,
              "tex: noise(seed:2) resolves to {kind:temp,index:1} (points at the inline chain's own step)");
    }

    // ==================================================================
    // search-order resolution: a bare call resolves to the FIRST
    // namespace (in program order) that registers it
    // ==================================================================
    {
        const QJsonObject first = validateSrc(QStringLiteral("search classicNoisedeck, synth\nnoise().write(o0)\nrender(o0)\n"));
        check(step(first, 0, 0).value(QStringLiteral("op")).toString() == QStringLiteral("classicNoisedeck.noise"),
              "search classicNoisedeck, synth -> bare noise() resolves to classicNoisedeck.noise (first match)");

        const QJsonObject second = validateSrc(QStringLiteral("search synth, classicNoisedeck\nnoise().write(o0)\nrender(o0)\n"));
        check(step(second, 0, 0).value(QStringLiteral("op")).toString() == QStringLiteral("synth.noise"),
              "search synth, classicNoisedeck -> bare noise() resolves to synth.noise (order flipped, resolution follows)");
    }

    // ==================================================================
    // S006 (starter chain missing write()): the reference's own
    // isStarterOp bare-name limitation means this NEVER fires for a
    // normal bare call. It CAN fire through an explicit from()-resolved
    // namespace, but ONLY at the STATEMENT level: compileChainStatement's
    // own pre-write S006 check reads the RAW, un-substituted stmt.chain
    // (so the from()-attached `namespace` survives), whereas
    // _process_vars (`let x = ...`) checks `substitute(clone(v.expr))` --
    // and substitute's OWN Chain-rebuilding only copies
    // {type,name,args,kwargs} onto each mapped element, silently DROPPING
    // `namespace` in the process. So the identical from()-starter chain
    // produces S006 as a bare statement but NOT through a `let` binding
    // (both verified against the live oracle -- this is not a guess).
    // ==================================================================
    {
        const QJsonObject bare = validateSrc(QStringLiteral("search synth\nsolid(0.1,0.2,0.3)\n"));
        check(!anyDiagCode(bare, QStringLiteral("S006")), "a bare starter call with no write() does NOT produce S006 (reference bug-for-bug)");
        check(anyDiagCode(bare, QStringLiteral("S001")), "...it produces S001 'Chain must have explicit write()...' instead");

        const QJsonObject viaFromStmt = validateSrc(QStringLiteral("search filter\nfrom(synth, solid()).noise()\nrender(o0)\n"));
        check(anyDiagCode(viaFromStmt, QStringLiteral("S006")),
              "a STATEMENT-level chain starting with a from()-resolved starter DOES produce S006 "
              "(compileChainStatement checks the raw, un-substituted chain)");

        const QJsonObject viaFromLet = validateSrc(QStringLiteral("search filter\nlet x = from(synth, solid()).noise()\nrender(o0)\n"));
        check(!anyDiagCode(viaFromLet, QStringLiteral("S006")),
              "the IDENTICAL chain bound via `let` does NOT produce S006 -- substitute() drops the namespace field "
              "when rebuilding chain elements, so isStarterOp sees a bare (unnamespaced) name there");
    }

    // ==================================================================
    // diagnostic shape: location is {line} only (never column); nodeId is
    // present (possibly null) ONLY for a Subchain-triggered diagnostic
    // ==================================================================
    {
        const QJsonObject out = validateSrc(
            QStringLiteral("search filter\nbogus().subchain(){.solid()}.write(o0)\n"));
        const QJsonArray ds = diags(out);
        check(ds.size() == 3, "bogus().subchain(){...}.write(o0): 3 diagnostics (unknown effect, subchain-no-input, write-no-input)");
        bool foundSubchainDiag = false, foundWriteDiag = false;
        for (const QJsonValue& dv : ds) {
            const QJsonObject d = dv.toObject();
            if (d.value(QStringLiteral("code")).toString() == QStringLiteral("S005")
                && d.value(QStringLiteral("identifier")).toString() == QStringLiteral("[Subchain]")) {
                foundSubchainDiag = true;
                check(d.contains(QStringLiteral("nodeId")) && d.value(QStringLiteral("nodeId")).isNull(),
                      "Subchain-triggered diagnostic has nodeId:null present (Subchain nodes carry a literal 'id' field)");
                check(d.value(QStringLiteral("location")).toObject().size() == 1
                          && d.value(QStringLiteral("location")).toObject().contains(QStringLiteral("line")),
                      "location is {line} ONLY -- no 'column' key, ever");
            }
            if (d.value(QStringLiteral("code")).toString() == QStringLiteral("S005")
                && d.value(QStringLiteral("identifier")).toString() == QStringLiteral("[Write]")) {
                foundWriteDiag = true;
                check(!d.contains(QStringLiteral("nodeId")), "Write-triggered diagnostic has NO nodeId key at all (Write nodes have no 'id' field)");
            }
        }
        check(foundSubchainDiag && foundWriteDiag, "both the subchain-no-input and write-no-input diagnostics were found");
    }

    // ==================================================================
    // UnsupportedDsl: every one of the 7 logical fail-loud sites throws, even
    // though the reference itself fully resolves/interprets all of them
    // (verified: every one of these probes is `ok:true` against the live
    // oracle -- these are deliberate AOT-frontend divergences, not gaps).
    // ==================================================================
    auto expectUnsupported = [](const QString& src, const char* description) {
        bool threw = false;
        try {
            validateSrc(src);
        } catch (const nm::UnsupportedDsl&) {
            threw = true;
        } catch (...) {
            threw = false;
        }
        check(threw, description);
    };

    expectUnsupported(QStringLiteral("search synth\nif (1) {\n  noise().write(o0)\n}\nrender(o0)\n"),
                       "1/7: if/elif/else -> UnsupportedDsl");
    expectUnsupported(QStringLiteral("search synth\nbreak\n"), "2a/7: break -> UnsupportedDsl");
    expectUnsupported(QStringLiteral("search synth\ncontinue\n"), "2b/7: continue -> UnsupportedDsl");
    expectUnsupported(QStringLiteral("search synth\nreturn\n"), "2c/7: return -> UnsupportedDsl");
    expectUnsupported(QStringLiteral("search synth\nnoise(wrap: () => true).write(o0)\nrender(o0)\n"),
                       "3/7: Func boolean param -> UnsupportedDsl");
    expectUnsupported(QStringLiteral("search synth\nnoise(wrap: time).write(o0)\nrender(o0)\n"),
                       "4/7: state-value boolean param -> UnsupportedDsl");
    expectUnsupported(QStringLiteral("search synth\nnoise(type: time).write(o0)\nrender(o0)\n"),
                       "5/7: state-value member param -> UnsupportedDsl");
    expectUnsupported(QStringLiteral("search synth\nnoise(octaves: () => 5).write(o0)\nrender(o0)\n"),
                       "6/7: Func numeric param -> UnsupportedDsl");
    expectUnsupported(QStringLiteral("search synth\nnoise(octaves: time).write(o0)\nrender(o0)\n"),
                       "7/7: state-value numeric param -> UnsupportedDsl");

    // Oscillator (osc()) is explicitly NOT in the UnsupportedDsl set --
    // its descriptor is compiled for deterministic runtime evaluation.
    {
        const QJsonObject out = validateSrc(
            QStringLiteral("search synth\nnoise(scaleX: osc(min: 10, max: 90, speed: 2)).write(o0)\nrender(o0)\n"));
        const QJsonObject osc = args(out, 0, 0).value(QStringLiteral("scaleX")).toObject();
        check(osc.value(QStringLiteral("type")).toString() == QStringLiteral("Oscillator") && osc.value(QStringLiteral("oscType")).toDouble() == 0.0
                  && osc.value(QStringLiteral("min")).toDouble() == 1.0 && osc.value(QStringLiteral("max")).toDouble() == 1.0
                  && osc.value(QStringLiteral("speed")).toDouble() == 2.0,
              "osc(min:10,max:90,speed:2) resolves fully (NOT UnsupportedDsl); min/max clamp into 0..1 (both -> 1)");
        check(osc.value(QStringLiteral("_ast")).isObject(), "the resolved Oscillator value embeds its _ast subtree");
    }

    // ==================================================================
    // _varRef propagation: a let-bound variable used as a numeric arg
    // wraps the clamped value in {_varRef,value}; used as an Oscillator
    // source, the marker appears BOTH at the value's top level AND nested
    // inside its _ast copy (verified against the oracle).
    // ==================================================================
    {
        const QJsonObject out = validateSrc(QStringLiteral("search synth\nlet n = 33\nnoise(octaves: n).write(o0)\nrender(o0)\n"));
        const QJsonObject wrapped = args(out, 0, 0).value(QStringLiteral("octaves")).toObject();
        check(wrapped.value(QStringLiteral("_varRef")).toString() == QStringLiteral("n") && wrapped.value(QStringLiteral("value")).toDouble() == 8.0,
              "let n = 33; noise(octaves: n) -> {_varRef:'n', value:8} (clamped)");

        const QJsonObject viaOsc = validateSrc(
            QStringLiteral("search synth\nlet o = osc(min: 0.2, max: 0.8)\nnoise(scaleX: o).write(o0)\nrender(o0)\n"));
        const QJsonObject oscVal = args(viaOsc, 0, 0).value(QStringLiteral("scaleX")).toObject();
        check(oscVal.value(QStringLiteral("_varRef")).toString() == QStringLiteral("o"), "_varRef present at the Oscillator value's top level");
        check(oscVal.value(QStringLiteral("_ast")).toObject().value(QStringLiteral("_varRef")).toString() == QStringLiteral("o"),
              "_varRef is ALSO present nested inside _ast (same underlying node object in the reference)");
    }

    // ==================================================================
    // Nested automation descriptors: numeric fields may themselves be
    // oscillator/MIDI/audio sources; enum/string selector fields may not.
    // ==================================================================
    {
        const QJsonObject out = validateSrc(QStringLiteral(
            "search synth\n"
            "let rate = osc(type: oscKind.sine, min: 0.25, max: 0.75)\n"
            "let carrier = osc(type: oscKind.saw, speed: rate)\n"
            "noise(scaleX: carrier).write(o0)\nrender(o0)\n"));
        const QJsonObject carrier = args(out, 0, 0).value(QStringLiteral("scaleX")).toObject();
        const QJsonObject rate = carrier.value(QStringLiteral("speed")).toObject();
        check(diags(out).isEmpty(), "nested oscillator compiles without diagnostics");
        check(carrier.value(QStringLiteral("type")).toString() == QStringLiteral("Oscillator")
                  && rate.value(QStringLiteral("type")).toString() == QStringLiteral("Oscillator"),
              "oscillator speed preserves its nested automation descriptor");
        check(rate.value(QStringLiteral("_varRef")).toString() == QStringLiteral("rate"),
              "nested automation preserves the referenced variable name");
    }

    {
        const QJsonObject out = validateSrc(QStringLiteral(
            "search synth\n"
            "let movement = midi(channel: 1, name: \"Controller\", id: \"port-a\")\n"
            "let gate = audio(band: audioBand.vol, min: movement, channel: 2, "
            "name: \"Interface\", id: \"device-b\")\n"
            "noise(scaleX: gate).write(o0)\nrender(o0)\n"));
        const QJsonObject audio = args(out, 0, 0).value(QStringLiteral("scaleX")).toObject();
        check(diags(out).isEmpty(), "nested selected MIDI/audio descriptors compile without diagnostics");
        check(audio.value(QStringLiteral("type")).toString() == QStringLiteral("Audio")
                  && audio.value(QStringLiteral("min")).toObject().value(QStringLiteral("type")).toString()
                         == QStringLiteral("Midi"),
              "audio numeric bounds preserve a nested MIDI source");
        check(audio.value(QStringLiteral("name")).toString() == QStringLiteral("Interface")
                  && audio.value(QStringLiteral("id")).toString() == QStringLiteral("device-b")
                  && audio.value(QStringLiteral("channel")).toInt() == 2,
              "compiled audio descriptor preserves its selected-device identity");
    }

    {
        const QJsonObject invalidBand = validateSrc(QStringLiteral(
            "search synth\nlet movement = midi(channel: 1)\n"
            "noise(scaleX: audio(band: movement)).write(o0)\nrender(o0)\n"));
        check(anyDiagCode(invalidBand, QStringLiteral("S002")),
              "automation remains invalid for literal-only audio band");

        const QJsonObject invalidMin = validateSrc(QStringLiteral(
            "search synth\nnoise(scaleX: audio(band: audioBand.vol, min: \"bad\"))"
            ".write(o0)\nrender(o0)\n"));
        const QJsonObject audio = args(invalidMin, 0, 0).value(QStringLiteral("scaleX")).toObject();
        check(anyDiagCode(invalidMin, QStringLiteral("S001"))
                  && audio.value(QStringLiteral("_invalid")).toBool(),
              "invalid audio numeric fields diagnose and mark the descriptor invalid");
    }

    {
        const QJsonObject cycle = validateSrc(QStringLiteral(
            "search synth\n"
            "let first = osc(type: oscKind.sine, speed: second)\n"
            "let second = osc(type: oscKind.tri, speed: first)\n"
            "noise(scaleX: first).write(o0)\nrender(o0)\n"));
        bool foundCycle = false;
        for (const QJsonValue& diagnostic : diags(cycle)) {
            if (diagnostic.toObject().value(QStringLiteral("message")).toString().contains(
                    QStringLiteral("Automation cycle detected"))) {
                foundCycle = true;
            }
        }
        check(foundCycle, "automation reference cycle produces a diagnostic instead of recursing");
    }

    {
        const QJsonObject tooDeep = validateSrc(QStringLiteral(
            "search synth\n"
            "let rate9 = osc(type: oscKind.sine)\n"
            "let rate8 = osc(type: oscKind.sine, speed: rate9)\n"
            "let rate7 = osc(type: oscKind.sine, speed: rate8)\n"
            "let rate6 = osc(type: oscKind.sine, speed: rate7)\n"
            "let rate5 = osc(type: oscKind.sine, speed: rate6)\n"
            "let rate4 = osc(type: oscKind.sine, speed: rate5)\n"
            "let rate3 = osc(type: oscKind.sine, speed: rate4)\n"
            "let rate2 = osc(type: oscKind.sine, speed: rate3)\n"
            "let rate1 = osc(type: oscKind.sine, speed: rate2)\n"
            "let carrier = osc(type: oscKind.saw, speed: rate1)\n"
            "noise(scaleX: carrier).write(o0)\nrender(o0)\n"));
        bool foundDepth = false;
        for (const QJsonValue& diagnostic : diags(tooDeep)) {
            if (diagnostic.toObject().value(QStringLiteral("message")).toString().contains(
                    QStringLiteral("maximum depth of 8"))) {
                foundDepth = true;
            }
        }
        check(foundDepth, "automation nesting beyond eight levels produces the reference diagnostic");
    }

    // ==================================================================
    // vars / searchNamespaces / render pass through in the top-level shape
    // ==================================================================
    {
        const QJsonObject out = validateSrc(QStringLiteral("search synth\nlet n = 33\nnoise(octaves: n).write(o0)\nrender(o0)\n"));
        check(out.value(QStringLiteral("render")).toString() == QStringLiteral("o0"), "render name passes through");
        check(out.value(QStringLiteral("searchNamespaces")).toArray().size() == 1
                  && out.value(QStringLiteral("searchNamespaces")).toArray().first().toString() == QStringLiteral("synth"),
              "searchNamespaces echoes the program's search order");
        const QJsonArray vars = out.value(QStringLiteral("vars")).toArray();
        check(vars.size() == 1 && vars.first().toObject().value(QStringLiteral("name")).toString() == QStringLiteral("n"),
              "vars carries the original (unresolved) var declarations");

        const QJsonObject noVars = validateSrc(QStringLiteral("search synth\nnoise().write(o0)\nrender(o0)\n"));
        check(noVars.value(QStringLiteral("vars")).toArray().isEmpty(), "vars is [] (present, empty) when the program has no `let` bindings");
    }

    if (g_failures == 0) {
        std::printf("ALL PASS (test_validator)\n");
        return 0;
    }
    std::printf("%d FAILURE(S) (test_validator)\n", g_failures);
    return 1;
}
