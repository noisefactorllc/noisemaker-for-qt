// Mesh host API and triangle draw path on nm::Backend: loadOBJFromString /
// loadOBJFromFile / uploadMeshData / externalMeshes, and render/meshRender's
// drawMode "triangles" (depth test, back-face culling, zero mesh default,
// re-upload after releaseGl()). Plain executable, run from the repo root
// (WORKING_DIRECTORY in CMakeLists.txt). Rendered parity against reference
// goldens is parity/run.sh on the mesh* fixtures.

#include "../noisemaker/compiler/dsl_compiler.h"
#include "../noisemaker/compiler/effect_registry.h"
#include "../noisemaker/runtime/backend.h"

#include <QFile>
#include <QGuiApplication>
#include <QImage>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const char* description) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) ++g_failures;
}

const QString kDataRoot = QStringLiteral("qt/noisemaker");

// meshRender defaults: bgColor (0.1, 0.1, 0.15), bgAlpha 1, stored in an
// rgba16f surface -> (25, 25, 38) after the 8-bit readback.
bool isBackground(QRgb c) {
    return std::abs(qRed(c) - 25) <= 1 && std::abs(qGreen(c) - 25) <= 1 && std::abs(qBlue(c) - 38) <= 1;
}

QRgb renderCenter(nm::Backend& backend, const nm::Graph& graph, QRgb* corner = nullptr) {
    backend.render(graph, 0.25);
    const QImage image = backend.readSurface();
    if (corner) *corner = image.pixel(1, 1);
    return image.pixel(image.width() / 2, image.height() / 2);
}

// A triangle around the centre at depth z. OBJ order (a, b, c) is stored
// as (a, c, b); with a = (-0.5,-0.5), b = (0,0.5), c = (0.5,-0.5) the
// stored order is counter-clockwise on screen (front facing).
QString triangle(double z, int firstVertex, const char* normal, bool reversed) {
    const QString base = QString::number(firstVertex);
    const QString mid = QString::number(firstVertex + 1);
    const QString last = QString::number(firstVertex + 2);
    return QStringLiteral("v -0.5 -0.5 %1\nv 0 0.5 %1\nv 0.5 -0.5 %1\nvn %2\n").arg(z).arg(QLatin1String(normal))
        + (reversed ? QStringLiteral("f %1//%4 %3//%4 %2//%4\n") : QStringLiteral("f %1//%4 %2//%4 %3//%4\n"))
              .arg(base, mid, last, QString::number((firstVertex + 2) / 3));
}

// Normals toward and away from the default light (0.5, 0.7, 0.5):
// lit ~ 211, unlit (ambient + rim) ~ 131 in the 8-bit red channel.
const char* const kLit = "0.5 0.7 0.5";
const char* const kUnlit = "-0.5 -0.7 -0.5";

void testBeforeSetup() {
    nm::Backend backend;
    const nm::MeshLoadResult fromString = backend.loadOBJFromString(QStringLiteral("v 0 0 0\n"));
    check(!fromString.success && fromString.vertexCount == 0 && fromString.error == QStringLiteral("Pipeline not ready"),
          "loadOBJFromString before setup: {false, 0, \"Pipeline not ready\"}");
    const nm::MeshLoadResult fromFile = backend.loadOBJFromFile(QStringLiteral("qt/noisemaker/share/meshes/cube.obj"));
    check(!fromFile.success && fromFile.error == QStringLiteral("Pipeline not ready"),
          "loadOBJFromFile before setup: \"Pipeline not ready\"");
}

void testExternalMeshes(nm::EffectRegistry& registry) {
    nm::Backend backend;
    backend.setup(nullptr, kDataRoot, QSize(16, 16));
    const nm::Graph loader = nm::compileGraph(QStringLiteral("search render\nmeshLoader().meshRender().write(o0)\nrender(o0)"),
                                              registry);
    const QVector<nm::ExternalMeshInput> inputs = backend.externalMeshes(loader);
    check(inputs.size() == 1 && inputs.first().meshId == QStringLiteral("mesh0") && inputs.first().stepIndex == 0
              && inputs.first().effectKey == QStringLiteral("render.meshLoader"),
          "externalMeshes: meshLoader step 0 reads mesh0");
    check(inputs.size() == 1 && inputs.first().builtinMeshes.size() == 7
              && inputs.first().builtinMeshes.first().name == QStringLiteral("sphere")
              && QFile::exists(inputs.first().builtinMeshes.first().path),
          "externalMeshes: seven built-in meshes, sphere first, path exists");
    const nm::Graph renderOnly = nm::compileGraph(
        QStringLiteral("search synth, render\nnoise(seed: 1).meshRender().write(o0)\nrender(o0)"), registry);
    check(backend.externalMeshes(renderOnly).isEmpty(), "externalMeshes: meshRender alone declares no mesh input");
}

void testTriangles(nm::EffectRegistry& registry) {
    nm::Backend backend;
    backend.setup(nullptr, kDataRoot, QSize(64, 64));
    const nm::Graph graph = nm::compileGraph(
        QStringLiteral("search synth, render\nnoise(seed: 1).meshRender().write(o0)\nrender(o0)"), registry);

    QRgb corner = 0;
    check(isBackground(renderCenter(backend, graph)), "no mesh loaded: zero mesh textures draw nothing");

    nm::MeshLoadResult result = backend.loadOBJFromString(triangle(0.0, 1, kLit, false));
    check(result.success && result.vertexCount == 3 && result.error.isEmpty(), "loadOBJFromString: {true, 3}");
    QRgb center = renderCenter(backend, graph, &corner);
    check(!isBackground(center) && qRed(center) > 170, "front-facing triangle is drawn and lit");
    check(isBackground(corner), "pixels outside the triangle keep the background");

    backend.loadOBJFromString(triangle(0.0, 1, kLit, true));
    check(isBackground(renderCenter(backend, graph)), "clockwise (back-facing) triangle is culled");

    // Depth: the nearer triangle (smaller z) wins whichever is drawn last.
    backend.loadOBJFromString(triangle(-1.0, 1, kUnlit, false) + triangle(1.0, 4, kLit, false));
    center = renderCenter(backend, graph);
    check(!isBackground(center) && qRed(center) < 170, "depth test: near unlit triangle hides a later far lit one");
    backend.loadOBJFromString(triangle(-1.0, 1, kLit, false) + triangle(1.0, 4, kUnlit, false));
    center = renderCenter(backend, graph);
    check(qRed(center) > 170, "depth test: near lit triangle hides a later far unlit one");

    // Frames keep the same result (depth buffer cleared per pass).
    center = renderCenter(backend, graph);
    check(qRed(center) > 170, "second frame: same result");

    result = backend.loadOBJFromString(QString());
    check(result.success && result.vertexCount == 0, "empty OBJ: {true, 0}");
    check(isBackground(renderCenter(backend, graph)), "empty OBJ clears the mesh");

    result = backend.loadOBJFromFile(QStringLiteral("qt/noisemaker/share/meshes/missing.obj"));
    check(!result.success && result.vertexCount == 0 && result.error.startsWith(QStringLiteral("Failed to load OBJ: ")),
          "loadOBJFromFile: unreadable file -> {false, 0, \"Failed to load OBJ: ...\"}");

    QString many = QStringLiteral("v 0 0 0\nv 1 0 0\nv 0 1 0\n");
    for (int i = 0; i < 21846; ++i) many += QStringLiteral("f 1 2 3\n");
    result = backend.loadOBJFromString(many);
    check(result.success && result.vertexCount == 65536, "65538 vertices truncate to 65536");

    const std::vector<float> texels(256 * 256 * 4, 0.0f);
    const std::vector<float> shortArray(16, 0.0f);
    result = backend.uploadMeshData(QStringLiteral("mesh0"), texels, texels, shortArray, 256, 256, 0);
    check(!result.success && !result.error.isEmpty(), "uploadMeshData: short array is rejected");
    result = backend.uploadMeshData(QStringLiteral("mesh0"), texels, texels, texels, 0, 256, 0);
    check(!result.success, "uploadMeshData: zero width is rejected");
    result = backend.uploadMeshData(QStringLiteral("mesh0"), texels, texels, texels, 256, 256, 0);
    check(result.success && result.vertexCount == 0, "uploadMeshData: zero texels upload");
    check(isBackground(renderCenter(backend, graph)), "uploadMeshData zeros: nothing drawn");

    // A loaded OBJ is kept and uploaded again after the GL objects go away.
    backend.loadOBJFromString(triangle(0.0, 1, kLit, false));
    backend.releaseGl();
    backend.setup(nullptr, kDataRoot, QSize(64, 64));
    center = renderCenter(backend, graph);
    check(!isBackground(center) && qRed(center) > 170, "releaseGl() + setup(): the loaded OBJ is uploaded again");
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    nm::EffectRegistry registry;
    registry.loadAll(kDataRoot);

    testBeforeSetup();
    testExternalMeshes(registry);
    testTriangles(registry);

    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
