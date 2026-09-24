#pragma once

// Host mesh setup shared by every nm-render render path (--graph,
// --batch-manifest, --dsl, --samples). nm-render acts as the reference demo
// host: for each step whose effect declares `externalMesh` it loads the
// effect's first built-in mesh (demo-ui.js _createMeshInputSection), then
// `--mesh <file.obj>` (or a batch item's "mesh") replaces mesh0, as a file
// chosen in the demo does. The engine itself loads no mesh.

#include "../../noisemaker/runtime/backend.h"
#include "../../noisemaker/runtime/graph.h"

#include <QString>

#include <stdexcept>

namespace nm {

inline void loadHostMeshes(Backend& backend, const Graph& graph, const QString& meshPath) {
    for (const ExternalMeshInput& input : backend.externalMeshes(graph)) {
        if (input.builtinMeshes.isEmpty()) continue;
        const BuiltinMesh& builtin = input.builtinMeshes.first();
        const MeshLoadResult result = backend.loadOBJFromFile(builtin.path, input.meshId);
        if (!result.success) {
            throw std::runtime_error(
                ("built-in mesh '" + builtin.name + "' for " + input.meshId + ": " + result.error).toStdString());
        }
    }
    if (!meshPath.isEmpty()) {
        const MeshLoadResult result = backend.loadOBJFromFile(meshPath, QStringLiteral("mesh0"));
        if (!result.success) {
            throw std::runtime_error(("--mesh '" + meshPath + "': " + result.error).toStdString());
        }
    }
}

} // namespace nm
