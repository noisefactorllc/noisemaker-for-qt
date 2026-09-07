// Unit tests for nm::parse (qt/noisemaker/compiler/parser.{h,cpp}).
// Plain assert-style checks, no test framework dependency (matches
// test_graph_load.cpp / test_lexer.cpp convention).
//
// Covers the T8 brief's named parser basics (chain, subchain,
// named/positional exclusivity, vec/bracket literals) plus the other
// hazards read out of shaders/src/lang/parser.js and empirically
// cross-checked against the reference oracle (NM_REFERENCE_ROOT
// tools/dump-ast.mjs) before being hard-coded -- see task report.
//
// RED (before parser.cpp has a real definition): this binary fails to
// LINK ("undefined symbols: nm::parse(...)"). GREEN: all checks below
// print PASS and the process exits 0.

#include "../noisemaker/compiler/ast.h"
#include "../noisemaker/compiler/diagnostics.h"
#include "../noisemaker/compiler/lexer.h"
#include "../noisemaker/compiler/parser.h"

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

QJsonObject parseSrc(const QString& src) {
    return nm::parse(nm::lex(src));
}

bool numEq(double a, double b) {
    return std::abs(a - b) <= 1e-12 * std::max({1.0, std::abs(a), std::abs(b)});
}

} // namespace

int main() {
    // ==================================================================
    // chain
    // ==================================================================
    {
        const QJsonObject prog = parseSrc(QStringLiteral("search synth\nsolid(0.1,0.2,0.3).write(o0)\nrender(o0)\n"));
        check(prog.value(QStringLiteral("type")).toString() == nm::NodeKind::Program, "Program root type");
        const QJsonArray plans = prog.value(QStringLiteral("plans")).toArray();
        check(plans.size() == 1, "one plan (chain statement)");
        const QJsonObject stmt = plans.at(0).toObject();
        check(!stmt.contains(QStringLiteral("type")), "chain-statement wrapper has NO type key");
        const QJsonArray chain = stmt.value(QStringLiteral("chain")).toArray();
        check(chain.size() == 2, "chain has 2 elements: Call + Write");
        check(chain.at(0).toObject().value(QStringLiteral("type")).toString() == nm::NodeKind::Call, "chain[0] is Call");
        check(chain.at(0).toObject().value(QStringLiteral("name")).toString() == QStringLiteral("solid"),
              "chain[0].name == solid");
        check(chain.at(1).toObject().value(QStringLiteral("type")).toString() == nm::NodeKind::Write, "chain[1] is Write");
        check(stmt.value(QStringLiteral("write")).toObject().value(QStringLiteral("name")).toString()
                  == QStringLiteral("o0"),
              "top-level write shortcut == chain's terminal Write surface");
        check(stmt.value(QStringLiteral("write3d")).isNull(), "write3d is null when chain has no write3d");
        check(prog.value(QStringLiteral("render")).toObject().value(QStringLiteral("name")).toString()
                  == QStringLiteral("o0"),
              "render(o0) parsed");

        const QJsonObject nsMeta = prog.value(QStringLiteral("namespace")).toObject();
        check(nsMeta.value(QStringLiteral("searchOrder")).toArray().size() == 1
                  && nsMeta.value(QStringLiteral("searchOrder")).toArray().at(0).toString() == QStringLiteral("synth"),
              "namespace.searchOrder == ['synth']");
        check(nsMeta.value(QStringLiteral("default")).toObject().value(QStringLiteral("name")).toString()
                  == QStringLiteral("synth"),
              "namespace.default.name == synth");
    }
    {
        // Only the LAST write()/write3d() in a chain sets the top-level
        // shortcut; every write() call still appears as its own chain
        // element regardless of position (empirically verified).
        const QJsonObject prog =
            parseSrc(QStringLiteral("search synth,filter\nnoise().write(o0).invert().write(o1)\nrender(o1)\n"));
        const QJsonArray chain = prog.value(QStringLiteral("plans")).toArray().at(0).toObject().value(QStringLiteral("chain")).toArray();
        check(chain.size() == 4, "mid-chain write: 4 chain elements (Call,Write,Call,Write)");
        check(chain.at(1).toObject().value(QStringLiteral("type")).toString() == nm::NodeKind::Write, "chain[1] mid-chain Write present");
        const QJsonObject stmt = prog.value(QStringLiteral("plans")).toArray().at(0).toObject();
        check(stmt.value(QStringLiteral("write")).toObject().value(QStringLiteral("name")).toString() == QStringLiteral("o1"),
              "top-level write reflects the LAST write in the chain, not the first");
    }
    {
        // Comments are legal around chain dots (leadingComments), but NOT
        // inside a block before a statement (parser has no collectComments
        // call there) -- verified against the oracle: this throws.
        bool threw = false;
        try {
            parseSrc(QStringLiteral("search synth\nif (1) {\n  // comment\n  solid(0.1,0.2,0.3).write(o0)\n}\nrender(o0)\n"));
        } catch (const nm::DslSyntaxError&) {
            threw = true;
        }
        check(threw, "a comment immediately before a statement INSIDE a block is a syntax error (not collected there)");
    }
    {
        const QJsonObject prog = parseSrc(QStringLiteral(
            "search synth\n// leading comment\nsolid(0.1,0.2,0.3)\n  // pre-dot comment\n  .write(o0)\n// trailing comment\nrender(o0)\n// after render\n"));
        const QJsonObject stmt = prog.value(QStringLiteral("plans")).toArray().at(0).toObject();
        check(stmt.value(QStringLiteral("leadingComments")).toArray().size() == 1, "top-level statement leadingComments captured");
        const QJsonArray chain = stmt.value(QStringLiteral("chain")).toArray();
        check(chain.at(1).toObject().value(QStringLiteral("leadingComments")).toArray().at(0).toString()
                  == QStringLiteral("// pre-dot comment"),
              "pre-dot comment attaches to the following chain element");
        check(prog.value(QStringLiteral("render")).toObject().value(QStringLiteral("leadingComments")).toArray().size() == 1,
              "comment immediately before render() attaches to the render node");
        check(prog.value(QStringLiteral("trailingComments")).toArray().at(0).toString() == QStringLiteral("// after render"),
              "comment after render() lands in Program.trailingComments");
    }

    // ==================================================================
    // subchain
    // ==================================================================
    {
        const QJsonObject prog = parseSrc(
            QStringLiteral("search filter\nnoise().subchain(name: \"fx\", id: \"sc1\") { .invert() }.write(o0)\nrender(o0)\n"));
        const QJsonArray chain = prog.value(QStringLiteral("plans")).toArray().at(0).toObject().value(QStringLiteral("chain")).toArray();
        check(chain.size() == 3, "chain: Call, Subchain, Write");
        const QJsonObject sub = chain.at(1).toObject();
        check(sub.value(QStringLiteral("type")).toString() == nm::NodeKind::Subchain, "subchain node type");
        check(sub.value(QStringLiteral("name")).toString() == QStringLiteral("fx"), "subchain kwarg name");
        check(sub.value(QStringLiteral("id")).toString() == QStringLiteral("sc1"), "subchain kwarg id");
        check(sub.value(QStringLiteral("body")).toArray().size() == 1, "subchain body has 1 call");
        check(sub.value(QStringLiteral("body")).toArray().at(0).toObject().value(QStringLiteral("name")).toString()
                  == QStringLiteral("invert"),
              "subchain body call name");
        check(sub.contains(QStringLiteral("loc")), "subchain has a loc");
    }
    {
        // Positional STRING shorthand for name -- same code path as the
        // name: kwarg (verified against the oracle).
        const QJsonObject prog =
            parseSrc(QStringLiteral("search filter\nnoise().subchain(\"myname\") { .invert() }.write(o0)\nrender(o0)\n"));
        const QJsonObject sub = prog.value(QStringLiteral("plans")).toArray().at(0).toObject().value(QStringLiteral("chain")).toArray().at(1).toObject();
        check(sub.value(QStringLiteral("name")).toString() == QStringLiteral("myname"), "positional string -> subchain.name");
        check(sub.value(QStringLiteral("id")).isNull(), "id defaults to null when omitted");
    }
    {
        // JS falsy-OR quirk (`kwargs.name?.value || null`): an explicitly
        // empty-string name/id STILL becomes JSON null, not "". Verified
        // against the reference oracle -- do not "fix" this.
        const QJsonObject prog = parseSrc(
            QStringLiteral("search filter\nnoise().subchain(name: \"\", id: \"x\") { .invert() }.write(o0)\nrender(o0)\n"));
        const QJsonObject sub = prog.value(QStringLiteral("plans")).toArray().at(0).toObject().value(QStringLiteral("chain")).toArray().at(1).toObject();
        check(sub.value(QStringLiteral("name")).isNull(), "subchain name: \"\" (empty string) becomes null (JS falsy-OR quirk, ported verbatim)");
        check(sub.value(QStringLiteral("id")).toString() == QStringLiteral("x"), "non-empty id kwarg unaffected");
    }
    {
        bool threw = false;
        try {
            parseSrc(QStringLiteral("search filter\nnoise().subchain() {}.write(o0)\nrender(o0)\n"));
        } catch (const nm::DslSyntaxError&) {
            threw = true;
        }
        check(threw, "empty subchain body throws DslSyntaxError");
    }

    // ==================================================================
    // named/positional exclusivity
    // ==================================================================
    {
        bool threw = false;
        try {
            parseSrc(QStringLiteral("search synth\nsolid(a: 1, 2).write(o0)\nrender(o0)\n"));
        } catch (const nm::DslSyntaxError&) {
            threw = true;
        }
        check(threw, "keyword arg followed by positional arg throws (cannot mix)");
    }
    {
        bool threw = false;
        try {
            parseSrc(QStringLiteral("search synth\nsolid(1, a: 2).write(o0)\nrender(o0)\n"));
        } catch (const nm::DslSyntaxError&) {
            threw = true;
        }
        check(threw, "positional arg followed by keyword arg throws (cannot mix)");
    }
    {
        const QJsonObject prog = parseSrc(QStringLiteral("search synth\nsolid(r: 0.1, g: 0.2, b: 0.3).write(o0)\nrender(o0)\n"));
        const QJsonObject call = prog.value(QStringLiteral("plans")).toArray().at(0).toObject().value(QStringLiteral("chain")).toArray().at(0).toObject();
        check(call.value(QStringLiteral("args")).toArray().isEmpty(), "all-kwargs call has an empty args array");
        check(call.value(QStringLiteral("kwargs")).toObject().value(QStringLiteral("g")).toObject().value(QStringLiteral("value")).toDouble() == 0.2,
              "kwargs preserved by name");
    }

    // ==================================================================
    // vec / bracket literals
    // ==================================================================
    {
        const QJsonObject prog = parseSrc(QStringLiteral("search synth\nlet x = [1, 2, 3]\nsolid(0.1,0.2,0.3).write(o0)\nrender(o0)\n"));
        const QJsonArray vars = prog.value(QStringLiteral("vars")).toArray();
        check(vars.size() == 1, "one var");
        const QJsonObject expr = vars.at(0).toObject().value(QStringLiteral("expr")).toObject();
        check(expr.value(QStringLiteral("type")).toString() == nm::NodeKind::ArrayLiteral, "[1,2,3] -> ArrayLiteral");
        const QJsonArray elements = expr.value(QStringLiteral("elements")).toArray();
        check(elements.size() == 3, "3 elements");
        check(elements.at(0).toObject().value(QStringLiteral("value")).toDouble() == 1
                  && elements.at(2).toObject().value(QStringLiteral("value")).toDouble() == 3,
              "element values in order");
        check(expr.contains(QStringLiteral("loc")), "ArrayLiteral carries a loc (position of '[')");
    }
    {
        bool threw = false;
        try {
            parseSrc(QStringLiteral("search synth\nlet x = [1, 2\nsolid(0.1,0.2,0.3).write(o0)\nrender(o0)\n"));
        } catch (const nm::DslSyntaxError&) {
            threw = true;
        }
        check(threw, "unterminated array literal (missing ']') throws");
    }
    {
        const QJsonObject prog = parseSrc(QStringLiteral("search synth\nlet x = []\nsolid(0.1,0.2,0.3).write(o0)\nrender(o0)\n"));
        const QJsonObject expr = prog.value(QStringLiteral("vars")).toArray().at(0).toObject().value(QStringLiteral("expr")).toObject();
        check(expr.value(QStringLiteral("elements")).toArray().isEmpty(), "empty array literal []");
    }

    // ==================================================================
    // numeric constant folding (parse-time, double)
    // ==================================================================
    {
        const QJsonObject prog = parseSrc(
            QStringLiteral("search synth\nlet a = -5 + 3\nlet b = -(2+3)*4\nlet c = 0.1 + 0.2\nlet d = ---5\nsolid(a,b,c).write(o0)\nrender(o0)\n"));
        const QJsonArray vars = prog.value(QStringLiteral("vars")).toArray();
        check(numEq(vars.at(0).toObject().value(QStringLiteral("expr")).toObject().value(QStringLiteral("value")).toDouble(), -2.0),
              "-5 + 3 == -2 (unary binds tighter than the following additive op)");
        check(numEq(vars.at(1).toObject().value(QStringLiteral("expr")).toObject().value(QStringLiteral("value")).toDouble(), -20.0),
              "-(2+3)*4 == -20");
        const double c = vars.at(2).toObject().value(QStringLiteral("expr")).toObject().value(QStringLiteral("value")).toDouble();
        check(c == 0.30000000000000004, "0.1 + 0.2 keeps the exact IEEE double artifact 0.30000000000000004 (double end-to-end)");
        check(numEq(vars.at(3).toObject().value(QStringLiteral("expr")).toObject().value(QStringLiteral("value")).toDouble(), -5.0),
              "---5 == -5 (odd number of unary minuses)");
    }
    {
        const QJsonObject prog = parseSrc(QStringLiteral("search synth\nlet x = Math.PI\nsolid(0.1,0.2,0.3).write(o0)\nrender(o0)\n"));
        const double pi = prog.value(QStringLiteral("vars")).toArray().at(0).toObject().value(QStringLiteral("expr")).toObject().value(QStringLiteral("value")).toDouble();
        check(pi == 3.141592653589793, "Math.PI folds to the exact reference double literal");
    }
    {
        // HEX -> Color: int(pair,16)/255 in double; 3-digit duplication;
        // alpha defaults to 1.0; 8-digit form carries alpha.
        const QJsonObject prog = parseSrc(QStringLiteral("search synth\nlet a = #fff\nlet b = #8040c0\nlet c = #8040c080\nsolid(0.1,0.2,0.3).write(o0)\nrender(o0)\n"));
        const QJsonArray vars = prog.value(QStringLiteral("vars")).toArray();
        const QJsonObject colA = vars.at(0).toObject().value(QStringLiteral("expr")).toObject();
        check(colA.value(QStringLiteral("type")).toString() == nm::NodeKind::Color, "#fff -> Color");
        const QJsonArray a = colA.value(QStringLiteral("value")).toArray();
        check(numEq(a.at(0).toDouble(), 1.0) && numEq(a.at(3).toDouble(), 1.0), "#fff -> [1,1,1,1] (3-digit duplication, alpha default 1)");
        const QJsonArray b = vars.at(1).toObject().value(QStringLiteral("expr")).toObject().value(QStringLiteral("value")).toArray();
        check(numEq(b.at(0).toDouble(), 0x80 / 255.0) && numEq(b.at(1).toDouble(), 0x40 / 255.0) && numEq(b.at(2).toDouble(), 0xc0 / 255.0)
                  && numEq(b.at(3).toDouble(), 1.0),
              "#8040c0 -> rgb/255, alpha defaults to 1");
        const QJsonArray c = vars.at(2).toObject().value(QStringLiteral("expr")).toObject().value(QStringLiteral("value")).toArray();
        check(numEq(c.at(3).toDouble(), 0x80 / 255.0), "#8040c080 -> alpha channel is the 4th hex pair / 255");
    }

    // ==================================================================
    // osc() 4-way disambiguation heuristic
    // ==================================================================
    {
        // isBareOsc: osc() with zero args/kwargs -> Oscillator with ALL defaults.
        const QJsonObject prog = parseSrc(QStringLiteral("search synth\nsolid(osc(), 0.2, 0.3).write(o0)\nrender(o0)\n"));
        const QJsonObject arg0 = prog.value(QStringLiteral("plans")).toArray().at(0).toObject().value(QStringLiteral("chain")).toArray().at(0).toObject().value(QStringLiteral("args")).toArray().at(0).toObject();
        check(arg0.value(QStringLiteral("type")).toString() == nm::NodeKind::Oscillator, "bare osc() -> Oscillator");
        check(arg0.value(QStringLiteral("oscType")).toObject().value(QStringLiteral("path")).toArray().at(1).toString() == QStringLiteral("sine"),
              "default oscType is oscKind.sine");
        check(numEq(arg0.value(QStringLiteral("max")).toObject().value(QStringLiteral("value")).toDouble(), 1.0), "default max is 1");
    }
    {
        // A single positional NON-oscKind-Member arg with no kwargs matches
        // none of the 4 transform conditions -> falls through as a plain
        // Call named "osc" (the synth.osc generator effect, not the value
        // oscillator). Verified against the oracle.
        const QJsonObject prog = parseSrc(QStringLiteral("search synth\nlet x = osc(0.2)\nsolid(0.1,0.2,0.3).write(o0)\nrender(o0)\n"));
        const QJsonObject expr = prog.value(QStringLiteral("vars")).toArray().at(0).toObject().value(QStringLiteral("expr")).toObject();
        check(expr.value(QStringLiteral("type")).toString() == nm::NodeKind::Call, "osc(0.2) (bare positional number, no kwargs) stays a plain Call");
        check(expr.value(QStringLiteral("name")).toString() == QStringLiteral("osc"), "Call name is osc");
    }
    {
        const QJsonObject prog = parseSrc(QStringLiteral("search synth\nlet x = osc(type: oscKind.noise, min: 0.09, max: 0.46, speed: 3, seed: 5701)\nsolid(0.1,0.2,0.3).write(o0)\nrender(o0)\n"));
        const QJsonObject expr = prog.value(QStringLiteral("vars")).toArray().at(0).toObject().value(QStringLiteral("expr")).toObject();
        check(expr.value(QStringLiteral("type")).toString() == nm::NodeKind::Oscillator, "osc(type: ..., ...) (all-osc-kwargs) -> Oscillator");
        check(numEq(expr.value(QStringLiteral("seed")).toObject().value(QStringLiteral("value")).toDouble(), 5701.0), "osc kwarg seed resolved");
    }
    {
        bool threw = false;
        try {
            parseSrc(QStringLiteral("search synth\nlet x = osc(type: oscKind.noise, bogus: 1)\nsolid(0.1,0.2,0.3).write(o0)\nrender(o0)\n"));
        } catch (const nm::DslSyntaxError&) {
            threw = true;
        }
        check(threw, "osc() with an unknown kwarg name throws");
    }

    // ==================================================================
    // midi() / audio() / from() / read() / read3d()
    // ==================================================================
    {
        // NOTE: channel/mode/sensitivity must be ALL-keyword here -- a
        // positional channel followed by keyword mode/sensitivity would hit
        // the same "cannot mix positional and keyword" rule tested above
        // (verified against the oracle: midi(2, mode: ..., sensitivity: ...)
        // throws at parseCall(), before the midi-specific transform ever runs).
        const QJsonObject prog = parseSrc(
            QStringLiteral("search synth\nlet x = midi(channel: 2, mode: midiMode.trigger, sensitivity: 0.5)\nsolid(0.1,0.2,0.3).write(o0)\nrender(o0)\n"));
        const QJsonObject midi = prog.value(QStringLiteral("vars")).toArray().at(0).toObject().value(QStringLiteral("expr")).toObject();
        check(midi.value(QStringLiteral("type")).toString() == nm::NodeKind::Midi, "midi() -> Midi node");
        check(numEq(midi.value(QStringLiteral("channel")).toObject().value(QStringLiteral("value")).toDouble(), 2.0), "midi kwarg channel resolved");
        check(midi.value(QStringLiteral("mode")).toObject().value(QStringLiteral("path")).toArray().at(1).toString() == QStringLiteral("trigger"),
              "midi kwarg mode resolved");
    }
    {
        bool threw = false;
        try {
            parseSrc(QStringLiteral("search synth\nlet x = midi()\nsolid(0.1,0.2,0.3).write(o0)\nrender(o0)\n"));
        } catch (const nm::DslSyntaxError&) {
            threw = true;
        }
        check(threw, "midi() with no channel throws (channel is required)");
    }
    {
        const QJsonObject prog = parseSrc(QStringLiteral("search synth\nlet x = audio(audioBand.low)\nsolid(0.1,0.2,0.3).write(o0)\nrender(o0)\n"));
        const QJsonObject audio = prog.value(QStringLiteral("vars")).toArray().at(0).toObject().value(QStringLiteral("expr")).toObject();
        check(audio.value(QStringLiteral("type")).toString() == nm::NodeKind::Audio, "audio() -> Audio node");
        check(audio.value(QStringLiteral("band")).toObject().value(QStringLiteral("path")).toArray().at(1).toString() == QStringLiteral("low"),
              "audio positional band resolved");
    }
    {
        const QJsonObject prog = parseSrc(QStringLiteral(
            "search synth\nlet x = midi(channel: 2, midiMode.trigger, 0.25, 0.75, 0.5, name: \"Controller\", id: \"port-a\")\n"
            "solid(0.1,0.2,0.3).write(o0)\nrender(o0)\n"));
        const QJsonObject midi = prog.value(QStringLiteral("vars")).toArray().at(0).toObject().value(QStringLiteral("expr")).toObject();
        check(midi.value(QStringLiteral("mode")).toObject().value(QStringLiteral("path")).toArray().at(1).toString()
                  == QStringLiteral("trigger"),
              "midi dense positionals fill fields skipped by kwargs");
        check(numEq(midi.value(QStringLiteral("min")).toObject().value(QStringLiteral("value")).toDouble(), 0.25)
                  && numEq(midi.value(QStringLiteral("max")).toObject().value(QStringLiteral("value")).toDouble(), 0.75),
              "midi mixed positional min/max resolved");
        check(midi.value(QStringLiteral("name")).toObject().value(QStringLiteral("value")).toString()
                  == QStringLiteral("Controller")
                  && midi.value(QStringLiteral("id")).toObject().value(QStringLiteral("value")).toString()
                         == QStringLiteral("port-a"),
              "midi device identity retained");
    }
    {
        const QJsonObject prog = parseSrc(QStringLiteral(
            "search synth\nlet x = audio(band: audioBand.raw, 0.25, 0.75, channel: 2, name: \"Interface\", id: \"device-b\")\n"
            "solid(0.1,0.2,0.3).write(o0)\nrender(o0)\n"));
        const QJsonObject audio = prog.value(QStringLiteral("vars")).toArray().at(0).toObject().value(QStringLiteral("expr")).toObject();
        check(audio.value(QStringLiteral("band")).toObject().value(QStringLiteral("path")).toArray().at(1).toString()
                  == QStringLiteral("raw"),
              "audio raw band retained");
        check(numEq(audio.value(QStringLiteral("min")).toObject().value(QStringLiteral("value")).toDouble(), 0.25)
                  && numEq(audio.value(QStringLiteral("max")).toObject().value(QStringLiteral("value")).toDouble(), 0.75),
              "audio dense positionals fill fields skipped by kwargs");
        check(audio.value(QStringLiteral("channel")).toObject().value(QStringLiteral("value")).toDouble() == 2.0
                  && audio.value(QStringLiteral("name")).toObject().value(QStringLiteral("value")).toString()
                         == QStringLiteral("Interface")
                  && audio.value(QStringLiteral("id")).toObject().value(QStringLiteral("value")).toString()
                         == QStringLiteral("device-b"),
              "audio device identity and channel retained");
    }
    for (const QString& source : {
             QStringLiteral("search synth\nlet x = midi(1, id: \"port-a\")\n"),
             QStringLiteral("search synth\nlet x = midi(1, midiMode.velocity, 0, 1, 1, \"Controller\")\n"),
             QStringLiteral("search synth\nlet x = midi(1, bogus: 1)\n"),
             QStringLiteral("search synth\nlet x = midi(1, name: Controller)\n"),
             QStringLiteral("search synth\nlet x = midi(1, name: \"\")\n"),
             QStringLiteral("search synth\nlet x = audio(audioBand.low, name: \"Interface\")\n"),
             QStringLiteral("search synth\nlet x = audio(audioBand.low, id: \"device-b\")\n"),
             QStringLiteral("search synth\nlet x = audio(audioBand.low, 0, 1, 2)\n"),
             QStringLiteral("search synth\nlet x = audio(audioBand.low, bogus: 1)\n"),
             QStringLiteral("search synth\nlet x = audio(audioBand.low, channel: 1, name: Interface)\n"),
             QStringLiteral("search synth\nlet x = audio(audioBand.low, channel: 1, name: \"\")\n"),
         }) {
        bool threw = false;
        try {
            parseSrc(source);
        } catch (const nm::DslSyntaxError&) {
            threw = true;
        }
        check(threw, "midi/audio selector invariants reject incomplete identity");
    }
    for (const auto& testCase : {
             std::pair{
                 QStringLiteral("search synth\nlet x = midi(1, zzz: 1, aaa: 2)\n"),
                 QStringLiteral("midi() unknown parameter 'zzz' at line 2 col 9. Valid: channel, mode, min, max, "
                                "sensitivity, name, id, cc, nrpn, zone, members")},
             std::pair{
                 QStringLiteral("search synth\nlet x = audio(audioBand.low, zzz: 1, aaa: 2)\n"),
                 QStringLiteral("audio() unknown parameter 'zzz' at line 2 col 9. Valid: band, min, max, channel, "
                                "name, id")},
         }) {
        QString message;
        try {
            parseSrc(testCase.first);
        } catch (const nm::DslSyntaxError& error) {
            message = error.message();
        }
        check(message == testCase.second, "midi/audio reports the first unknown keyword in source order");
    }
    {
        const QJsonObject prog = parseSrc(QStringLiteral("search synth\nfrom(filter, blur(amount: 5)).write(o0)\nrender(o0)\n"));
        const QJsonObject call = prog.value(QStringLiteral("plans")).toArray().at(0).toObject().value(QStringLiteral("chain")).toArray().at(0).toObject();
        check(call.value(QStringLiteral("name")).toString() == QStringLiteral("blur"), "from(filter, blur(...)) replaces the call with the target");
        const QJsonObject ns = call.value(QStringLiteral("namespace")).toObject();
        check(ns.value(QStringLiteral("name")).toString() == QStringLiteral("filter") && ns.value(QStringLiteral("fromOverride")).toBool(),
              "from() injects a namespace override with fromOverride:true");
    }
    {
        bool threw = false;
        try {
            parseSrc(QStringLiteral("search synth\nfoo.bar.baz()\nrender(o0)\n"));
        } catch (const nm::DslSyntaxError&) {
            threw = true;
        }
        check(threw, "a 2+-segment dotted call (foo.bar.baz()) is a syntax error, not a namespaced call (verified against oracle: 'Expect (' )");
    }
    {
        bool threw = false;
        try {
            parseSrc(QStringLiteral("search synth\nnd.noise().write(o0)\nrender(o0)\n"));
        } catch (const nm::DslSyntaxError&) {
            threw = true;
        }
        check(threw, "inline single-segment namespace call (nd.noise()) is explicitly forbidden");
    }
    {
        const QJsonObject prog = parseSrc(QStringLiteral("search synth3d\nlet a = read3d(vol0)\nread3d(vol0, geo0).write3d(vol1, geo1)\nrender(o0)\n"));
        const QJsonObject single = prog.value(QStringLiteral("vars")).toArray().at(0).toObject().value(QStringLiteral("expr")).toObject();
        check(single.value(QStringLiteral("type")).toString() == nm::NodeKind::Read3D, "read3d(vol0) -> Read3D");
        check(single.value(QStringLiteral("geo")).isNull(), "single-arg read3d() leaves geo == null");
        const QJsonObject stmt = prog.value(QStringLiteral("plans")).toArray().at(0).toObject();
        check(stmt.value(QStringLiteral("write3d")).toObject().value(QStringLiteral("tex3d")).toObject().value(QStringLiteral("name")).toString()
                  == QStringLiteral("vol1"),
              "write3d() sets the top-level write3d shortcut {tex3d,geo} (no type key)");
    }

    // ==================================================================
    // search directive requirements
    // ==================================================================
    {
        bool threw = false;
        try {
            parseSrc(QStringLiteral("solid(0.1,0.2,0.3).write(o0)\nrender(o0)\n"));
        } catch (const nm::DslSyntaxError& e) {
            threw = true;
            check(e.line() == -1, "missing-search error has no location (matches reference: no 'at line' suffix)");
        }
        check(threw, "missing search directive throws");
    }
    {
        bool threw = false;
        try {
            parseSrc(QStringLiteral("search bogus\nsolid(0.1,0.2,0.3).write(o0)\nrender(o0)\n"));
        } catch (const nm::DslSyntaxError&) {
            threw = true;
        }
        check(threw, "invalid namespace name throws");
    }
    {
        bool threw = false;
        try {
            parseSrc(QStringLiteral("search synth\nsearch filter\nsolid(0.1,0.2,0.3).write(o0)\nrender(o0)\n"));
        } catch (const nm::DslSyntaxError&) {
            threw = true;
        }
        check(threw, "a second search directive throws");
    }
    {
        bool threw = false;
        try {
            parseSrc(QStringLiteral("search synth\nsolid(0.1,0.2,0.3).write(o0)\nsearch filter\nrender(o0)\n"));
        } catch (const nm::DslSyntaxError&) {
            threw = true;
        }
        check(threw, "a search directive after other statements throws");
    }

    // ==================================================================
    // write() surface variants
    // ==================================================================
    {
        const QJsonObject prog = parseSrc(QStringLiteral(
            "search synth\nnoise().write(xyz0)\nnoise().write(vel1)\nnoise().write(rgba2)\nnoise().write(mesh3)\nnoise().write(none)\nrender(o0)\n"));
        const QJsonArray plans = prog.value(QStringLiteral("plans")).toArray();
        check(plans.size() == 5, "5 chain statements");
        const struct { const char* type; const char* name; } expected[5] = {
            {"XyzRef", "xyz0"}, {"VelRef", "vel1"}, {"RgbaRef", "rgba2"}, {"MeshRef", "mesh3"}, {"OutputRef", "none"},
        };
        bool allMatch = true;
        for (int i = 0; i < 5; ++i) {
            const QJsonObject w = plans.at(i).toObject().value(QStringLiteral("write")).toObject();
            allMatch = allMatch && w.value(QStringLiteral("type")).toString() == QString::fromUtf8(expected[i].type)
                       && w.value(QStringLiteral("name")).toString() == QString::fromUtf8(expected[i].name);
        }
        check(allMatch, "write() accepts xyz/vel/rgba/mesh refs and the literal 'none' ident");
    }

    if (g_failures == 0) {
        std::printf("ALL PASS (test_parser)\n");
        return 0;
    }
    std::printf("%d FAILURE(S) (test_parser)\n", g_failures);
    return 1;
}
