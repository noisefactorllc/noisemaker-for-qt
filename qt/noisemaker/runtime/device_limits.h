#pragma once

#include <QHash>
#include <QString>

#include "graph.h"

namespace nm::detail {

// Mutates a render-graph copy so every numeric volumeSize-family uniform
// produces a slice atlas that fits maxTextureSize.
bool clampGraphVolumeSizes(Graph& graph, int maxTextureSize);

// Fits one MRT pass to maxColorBytesPerSample. outputLocations maps graph
// output names to their linked GLSL fragment locations, preserving shader
// attachment priority even though QJsonObject iteration is key-sorted.
bool applyMrtFormatBudget(
    Graph& graph,
    const Pass& pass,
    const QHash<QString, int>& outputLocations,
    int maxColorBytesPerSample);

} // namespace nm::detail
