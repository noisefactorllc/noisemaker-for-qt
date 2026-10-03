// Unit tests for pass-level uniform aliases (reference noisemaker bd773801,
// shaders/src/runtime/uniform-aliases.js): a pass definition that feeds a
// shader uniform from a differently named global (`uniforms: { layoutMode:
// "layout" }`) records `{ shaderUniform: globalName }` on the expanded pass,
// and Backend::applyStepParameterValues writes a changed parameter to the
// aliased shader uniforms too — the alias write happens both when the pass
// carries the param under its own name and when it only carries the aliased
// name (reference canvas.js applyStepParameterValues + 71d805eb/109c00ac).
//
// Non-GL by design: Backend::applyStepParameterValues only mutates
// graph.passes, so the alias-write contract is exercised without a GL
// context (the GL-context-dependent render path stays covered by
// test_host_inputs). Plain assert-style checks, no test framework
// dependency (matches test_graph_load.cpp's convention).

#include "../noisemaker/compiler/dsl_compiler.h"
#include "../noisemaker/compiler/effect_registry.h"
#include "../noisemaker/runtime/backend.h"
#include "../noisemaker/runtime/graph.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <cstdio>

namespace {

int g_failures = 0;

void check(bool condition, const char* description) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) ++g_failures;
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

const char* const kAliasedGraph = R"JSON(
{
  "id": "alias1",
  "source": "search render\npointsEmit().write(o0)\nrender(o0)\n",
  "renderSurface": "o0",
  "passes": [
    {
      "id": "node_0_pass_0",
      "passType": "effect",
      "namespace": "render",
      "func": "pointsEmit",
      "progName": "pointsEmit",
      "program": "node_0_pointsEmit",
      "effectKey": "render.pointsEmit",
      "nodeId": "node_0",
      "stepIndex": 0,
      "inputs": {},
      "outputs": {},
      "uniforms": {
        "shaderX": 7,
        "layoutMode": 0
      },
      "uniformAliases": {
        "shaderX": "layout",
        "layoutMode": "layout"
      }
    },
    {
      "id": "node_0_write_blit",
      "passType": "blit",
      "namespace": null,
      "func": "blit",
      "progName": "blit",
      "program": "blit",
      "defines": {},
      "inputs": {"src": "node_0_out"},
      "outputs": {"color": "global_o0"},
      "uniforms": {},
      "uniformAliases": null
    }
  ],
  "allocations": {},
  "textures": {}
}
)JSON";

} // namespace

int main() {
    // --- compiled graph: the expander records the renamed mapping ---
    {
        nm::Graph graph = nm::compileGraph(
            QStringLiteral("search synth, render, points\nperlin().pointsEmit(stateSize: x256).pointsRender().write(o0)\nrender(o0)\n"),
            registry());
        const nm::Pass* aliasPass = nullptr;
        for (const nm::Pass& p : graph.passes) {
            if (p.uniforms.contains(QStringLiteral("layoutMode"))) aliasPass = &p;
        }
        check(aliasPass != nullptr, "compiled pointsEmit graph: the init pass carrying layoutMode exists");
        if (aliasPass) {
            check(aliasPass->uniformAliases.value(QStringLiteral("layoutMode")).toString() == QStringLiteral("layout"),
                  "the compiled pass carries uniformAliases { layoutMode: layout }");
        }
    }

    // --- Graph::fromJson parses uniformAliases (and null) ---
    {
        const nm::Graph graph = nm::Graph::fromJson(QByteArray(kAliasedGraph));
        check(graph.passes.size() == 2, "fixture graph parses with 2 passes");
        const nm::Pass& effectPass = graph.passes.first();
        check(effectPass.uniformAliases.size() == 2
                  && effectPass.uniformAliases.value(QStringLiteral("shaderX")).toString() == QStringLiteral("layout")
                  && effectPass.uniformAliases.value(QStringLiteral("layoutMode")).toString() == QStringLiteral("layout"),
              "Graph::fromJson parses the pass uniformAliases mapping");
        check(graph.passes.last().uniformAliases.isEmpty(),
              "Graph::fromJson parses a null uniformAliases as absent");
    }

    // --- alias writes on the parameter paths ---
    {
        nm::Graph graph = nm::Graph::fromJson(QByteArray(kAliasedGraph));
        nm::Backend backend;
        QJsonObject stepParams;
        stepParams.insert(QStringLiteral("layout"), QJsonValue(static_cast<double>(1)));
        QJsonObject steps;
        steps.insert(QStringLiteral("step_0"), stepParams);
        const int writes = backend.applyStepParameterValues(graph, registry(), steps);
        const nm::Pass& effectPass = graph.passes.first();
        check(effectPass.uniforms.value(QStringLiteral("shaderX")).toDouble() == 1.0,
              "the aliased shader uniform (shaderX) receives the changed parameter value");
        check(effectPass.uniforms.value(QStringLiteral("layoutMode")).toDouble() == 1.0,
              "the renamed init-pass uniform (layoutMode) receives the changed parameter value");
        check(writes == 1, "the alias write counts as one pass uniform write (reference writeUniformAliases returns a boolean)");
    }

    // --- a parameter without aliases writes nothing extra ---
    {
        nm::Graph graph = nm::Graph::fromJson(QByteArray(kAliasedGraph));
        nm::Backend backend;
        QJsonObject stepParams;
        stepParams.insert(QStringLiteral("attrition"), QJsonValue(static_cast<double>(0.5)));
        QJsonObject steps;
        steps.insert(QStringLiteral("step_0"), stepParams);
        const int writes = backend.applyStepParameterValues(graph, registry(), steps);
        const nm::Pass& effectPass = graph.passes.first();
        check(effectPass.uniforms.value(QStringLiteral("shaderX")).toDouble() == 7.0
                  && effectPass.uniforms.value(QStringLiteral("layoutMode")).toDouble() == 0.0,
              "a param the pass does not carry and no alias binds leaves the aliased uniforms untouched");
        check(writes == 0, "no writes counted when nothing matched");
    }

    if (g_failures == 0) {
        std::printf("ALL PASS (test_uniform_aliases)\n");
        return 0;
    }
    std::printf("%d FAILURE(S) (test_uniform_aliases)\n", g_failures);
    return 1;
}