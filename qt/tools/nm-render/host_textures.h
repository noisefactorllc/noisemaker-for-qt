#pragma once

// Host pixels for external texture ids (for example textTex_step_1), shared
// by the nm-render render paths that take them (--graph, --batch-manifest,
// --dsl). `--external-texture <texId>=<png>` names a PNG whose top row is the
// texture's highest v, as parity/export-and-render.mjs saves what the
// reference sampled. It is uploaded with flipY = true, which restores the
// reference's texel rows exactly. An asyncInit overlay id (node_1_overlayTex)
// is accepted too: the host texture replaces the overlay the Backend would
// generate (nm::Backend::setExternalTexture).

#include "../../noisemaker/runtime/async_overlay.h"
#include "../../noisemaker/runtime/backend.h"
#include "../../noisemaker/runtime/graph.h"

#include <QImage>
#include <QString>
#include <QStringList>

#include <stdexcept>
#include <utility>
#include <vector>

namespace nm {

using HostTextures = std::vector<std::pair<QString, QString>>;

// Every `--external-texture <texId>=<png>` argument, in order. Throws
// std::invalid_argument for a value without both parts (a usage error).
inline HostTextures findHostTextures(const QStringList& args) {
    HostTextures textures;
    for (int i = 0; i + 1 < args.size(); ++i) {
        if (args.at(i) != QStringLiteral("--external-texture")) {
            continue;
        }
        const QString value = args.at(i + 1);
        const qsizetype split = value.indexOf(QLatin1Char('='));
        if (split <= 0 || split == value.size() - 1) {
            throw std::invalid_argument(
                ("--external-texture expects <texId>=<png> (got '" + value + "')").toStdString());
        }
        textures.emplace_back(value.left(split), value.mid(split + 1));
    }
    return textures;
}

// Uploads each texture. An id that is neither an external texture nor an
// asyncInit overlay of the graph, or an unreadable file, is an error.
inline void loadHostTextures(Backend& backend, const Graph& graph, const HostTextures& textures) {
    const QStringList graphExternalIds = Backend::externalTextureIds(graph) + asyncOverlayTextureIds(graph);
    for (const auto& [texId, path] : textures) {
        if (!graphExternalIds.contains(texId)) {
            throw std::runtime_error(
                ("external texture '" + texId + "' is not sampled by this graph").toStdString());
        }
        const QImage image(path);
        if (image.isNull()) {
            throw std::runtime_error(("cannot read external texture '" + path + "'").toStdString());
        }
        backend.updateTextureFromSource(texId, image, ExternalTextureOptions{true});
    }
}

} // namespace nm
