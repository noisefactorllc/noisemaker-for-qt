// Unit tests for nm::Graph::fromJson (qt/noisemaker/runtime/graph.{h,cpp}).
// Plain assert-style checks, no test framework dependency (T3 brief Step 2).
// The fixture below is shaped exactly like `tools/export-graph.mjs`'s
// output for parity/programs/solid.dsl (docs/GRAPH-JSON-SCHEMA.md).
//
// RED (before graph.cpp has a real definition): this binary fails to LINK
// ("undefined symbols: nm::Graph::fromJson(...)"). GREEN: all checks below
// print PASS and the process exits 0.

#include "../noisemaker/runtime/graph.h"

#include <QByteArray>
#include <QJsonArray>

#include <cstdio>
#include <stdexcept>

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

const char* const kMiniGraph = R"JSON(
{
  "id": "test123",
  "source": "search synth\nsolid(0.2, 0.6, 0.9).write(o0)\nrender(o0)\n",
  "renderSurface": "o0",
  "passes": [
    {
      "id": "node_0_pass_0",
      "passType": "effect",
      "namespace": "synth",
      "func": "solid",
      "progName": "solid",
      "program": "node_0_solid",
      "defines": {},
      "inputs": {},
      "outputs": { "color": "node_0_out" },
      "uniforms": { "color": [0.5, 0.5, 0.5], "alpha": 0.6 },
      "uniformSpecs": { "alpha": { "min": 0, "max": 1 } },
      "effectKey": "synth.solid",
      "nodeId": "node_0",
      "stepIndex": 0,
      "scopedParams": null
    },
    {
      "id": "node_1_write_blit",
      "passType": "blit",
      "namespace": null,
      "func": "blit",
      "progName": "blit",
      "program": "blit",
      "defines": {},
      "inputs": { "src": "node_0_out" },
      "outputs": { "color": "global_o0" },
      "uniforms": {},
      "uniformSpecs": {},
      "effectKey": null,
      "nodeId": "node_1",
      "stepIndex": 1,
      "scopedParams": null
    }
  ],
  "allocations": { "node_0_out": "phys_0" },
  "textures": {
    "node_0_out": {
      "width": "screen",
      "height": "screen",
      "format": "rgba16f",
      "usage": ["render", "sample", "copySrc", "copyDst"]
    }
  },
  "programs": { "blit": { "uniformLayout": {}, "defines": {} } }
}
)JSON";

} // namespace

int main() {
    // Happy path: passes / textures / renderSurface all parsed.
    {
        const nm::Graph graph = nm::Graph::fromJson(QByteArray(kMiniGraph));

        check(graph.id == QStringLiteral("test123"), "graph.id parsed");
        check(graph.renderSurface == QStringLiteral("o0"), "graph.renderSurface parsed");
        check(graph.passes.size() == 2, "graph.passes has 2 entries, in order");

        const nm::Pass& effectPass = graph.passes.at(0);
        check(effectPass.id == QStringLiteral("node_0_pass_0"), "pass[0].id parsed");
        check(effectPass.passType == QStringLiteral("effect"), "pass[0].passType == effect");
        check(effectPass.effectNamespace == QStringLiteral("synth"), "pass[0].namespace parsed");
        check(effectPass.func == QStringLiteral("solid"), "pass[0].func parsed");
        check(effectPass.progName == QStringLiteral("solid"), "pass[0].progName parsed");
        check(effectPass.program == QStringLiteral("node_0_solid"), "pass[0].program parsed");
        check(effectPass.effectKey == QStringLiteral("synth.solid"), "pass[0].effectKey parsed");
        check(effectPass.nodeId == QStringLiteral("node_0"), "pass[0].nodeId parsed");
        check(effectPass.inputs.isEmpty(), "pass[0].inputs is empty");
        check(effectPass.outputs.value(QStringLiteral("color")).toString() == QStringLiteral("node_0_out"),
              "pass[0].outputs parsed");
        check(effectPass.uniforms.value(QStringLiteral("alpha")).toDouble() == 0.6,
              "pass[0].uniforms parsed (scalar)");
        check(effectPass.uniforms.value(QStringLiteral("color")).toArray().size() == 3,
              "pass[0].uniforms parsed (array)");
        check(effectPass.defines.isEmpty(), "pass[0].defines is empty");

        const nm::Pass& blitPass = graph.passes.at(1);
        check(blitPass.passType == QStringLiteral("blit"), "pass[1].passType == blit");
        check(blitPass.effectNamespace.isEmpty(), "pass[1].namespace is empty (JSON null) for blit");
        check(blitPass.effectKey.isEmpty(), "pass[1].effectKey is empty (JSON null) for blit");
        check(blitPass.func == QStringLiteral("blit"), "pass[1].func == blit");
        check(blitPass.progName == QStringLiteral("blit"), "pass[1].progName == blit");
        check(blitPass.inputs.value(QStringLiteral("src")).toString() == QStringLiteral("node_0_out"),
              "pass[1].inputs.src parsed");
        check(blitPass.outputs.value(QStringLiteral("color")).toString() == QStringLiteral("global_o0"),
              "pass[1].outputs.color parsed");

        check(graph.allocations.value(QStringLiteral("node_0_out")) == QStringLiteral("phys_0"),
              "graph.allocations parsed");

        check(graph.textures.contains(QStringLiteral("node_0_out")), "graph.textures has node_0_out");
        const nm::TextureSpec& spec = graph.textures.value(QStringLiteral("node_0_out"));
        check(spec.width.toString() == QStringLiteral("screen"), "textures['node_0_out'].width == 'screen'");
        check(spec.height.toString() == QStringLiteral("screen"), "textures['node_0_out'].height == 'screen'");
        check(spec.format == QStringLiteral("rgba16f"), "textures['node_0_out'].format parsed");
        check(spec.usage.size() == 4, "textures['node_0_out'].usage parsed");
        check(!spec.is3D, "textures['node_0_out'].is3D defaults false");

        // renderSurface's own texId never appears as a key in `textures{}`
        // -- it's a global, double-buffered-in-the-full-system surface the
        // backend creates on demand (docs/GRAPH-JSON-SCHEMA.md texId
        // conventions).
        check(!graph.textures.contains(QStringLiteral("global_o0")),
              "global_o0 is correctly absent from graph.textures (created on demand)");
    }

    // Malformed JSON must throw std::runtime_error, not crash or return a
    // half-built Graph.
    {
        bool threw = false;
        try {
            nm::Graph::fromJson(QByteArrayLiteral("{ not valid json"));
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "malformed JSON throws std::runtime_error");
    }

    // Valid JSON, wrong top-level shape (not an object).
    {
        bool threw = false;
        try {
            nm::Graph::fromJson(QByteArrayLiteral("[1, 2, 3]"));
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "JSON array (non-object) top level throws std::runtime_error");
    }

    // Valid JSON object, missing required 'passes' array.
    {
        bool threw = false;
        try {
            nm::Graph::fromJson(QByteArrayLiteral(R"({"id":"x"})"));
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "JSON missing required 'passes' array throws std::runtime_error");
    }

    // A pass missing a required field (progName) must also throw.
    {
        bool threw = false;
        try {
            nm::Graph::fromJson(QByteArrayLiteral(
                R"({"passes":[{"id":"p0","passType":"effect","func":"solid"}]})"));
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "a pass missing a required field throws std::runtime_error");
    }

    if (g_failures == 0) {
        std::printf("ALL PASS (test_graph_load)\n");
        return 0;
    }
    std::printf("%d FAILURE(S) (test_graph_load)\n", g_failures);
    return 1;
}
