// nm-render CLI. Shared file (see qt/CMakeLists.txt header comment and
// flag_hooks.h): flags other than --graph and --batch-manifest are
// implemented in their own translation units, which register a handler
// for a name pre-declared below. A new flag also needs a line in kUsage
// and kKnownFlags, which --help and the unknown-option check read
// (qt/tests/test_nm_render_cli.cpp checks both).

#include "data_root.h"
#include "flag_hooks.h"
#include "host_meshes.h"

#include "../../noisemaker/runtime/backend.h"
#include "../../noisemaker/runtime/graph.h"
#include "../../noisemaker/runtime/png_io.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSize>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

// Flags implemented by later tasks in their own translation units (see
// flag_hooks.h): --samples (renderer track, T5, in runtime/-backed files);
// --dsl/--dump-tokens/--dump-ast/--dump-validated/--dump-graph (compiler
// track, T8/T9/T10, in dump_*.cpp). Pre-declared here so this file is never
// edited again — an unimplemented one prints "unimplemented: <flag>" and
// exits 2.
const QStringList kPreDeclaredFlags = {
    QStringLiteral("--samples"),
    QStringLiteral("--dsl"),
    QStringLiteral("--dump-tokens"),
    QStringLiteral("--dump-ast"),
    QStringLiteral("--dump-validated"),
    QStringLiteral("--dump-graph"),
};

// Every mode and flag nm-render and its flag handlers (flag_hooks.h)
// accept. Printed by --help, and with any usage error.
const char* const kUsage =
    "usage: nm-render MODE [OPTIONS]\n"
    "\n"
    "Render modes (write PNG files):\n"
    "  --dsl FILE --size WxH --out PNG [--time T] [--frames N] [--mesh OBJ]\n"
    "      Compile a DSL program and render it N times at normalized loop time T,\n"
    "      0 <= T < 1 (defaults: T 0, N 1). Feedback effects need N > 1.\n"
    "  --graph FILE --size WxH --out PNG [--time T] [--frames N] [--mesh OBJ]\n"
    "        [--external-texture ID=PNG]...\n"
    "      Render an exported graph JSON the same way.\n"
    "  --graph FILE --size WxH --samples TOTAL:EVERY --out PNG [--mesh OBJ]\n"
    "      Step TOTAL frames 1/60 s apart. Every EVERY frames, write PNG with .tS\n"
    "      before its extension, S being the elapsed whole seconds (out.t5.png).\n"
    "  --batch-manifest FILE\n"
    "      Render each {graph, size, out, time, frames, mesh, externalTextures}\n"
    "      object of a JSON array (externalTextures maps ID to PNG).\n"
    "\n"
    "  --mesh OBJ  Load an OBJ file as mesh0 for meshLoader(), replacing the\n"
    "              default built-in mesh.\n"
    "  --external-texture ID=PNG  Upload PNG as the host texture ID that the graph\n"
    "              samples (for example textTex_step_1), top row first. Repeatable.\n"
    "\n"
    "Compiler dumps (JSON on standard output, for the parity gates):\n"
    "  --dump-tokens FILE  --dump-ast FILE  --dump-validated FILE  --dump-graph FILE\n"
    "\n"
    "  --help  Print this text.\n"
    "\n"
    "Render modes read shaders/, effects/ and share/ from qt/noisemaker beside the build\n"
    "directory, else ./qt/noisemaker. Exit status: 0 on success, 1 when compiling or\n"
    "rendering fails, 2 for a usage error.\n";

const QStringList kKnownFlags = {
    QStringLiteral("--dsl"),          QStringLiteral("--graph"),      QStringLiteral("--samples"),
    QStringLiteral("--batch-manifest"), QStringLiteral("--size"),     QStringLiteral("--out"),
    QStringLiteral("--time"),         QStringLiteral("--frames"),     QStringLiteral("--dump-tokens"),
    QStringLiteral("--dump-ast"),     QStringLiteral("--dump-validated"), QStringLiteral("--dump-graph"),
    QStringLiteral("--mesh"),         QStringLiteral("--help"),       QStringLiteral("--external-texture"),
};

int usageError(const QString& message) {
    if (!message.isEmpty()) std::fprintf(stderr, "ERROR: %s\n\n", message.toUtf8().constData());
    std::fprintf(stderr, "%s", kUsage);
    return 2;
}

QString findValue(const QStringList& args, const QString& flag) {
    const int idx = args.indexOf(flag);
    if (idx < 0 || idx + 1 >= args.size()) {
        return QString();
    }
    return args.at(idx + 1);
}

// Host pixels for an external texture id (for example textTex_step_1): a
// PNG whose top row is the texture's highest v, as parity/export-and-render
// saves what the reference sampled. Uploaded with flipY = true, which
// restores the reference's texel rows exactly.
using ExternalTextures = std::vector<std::pair<QString, QString>>;

// Every `--external-texture <texId>=<png>` argument, in order.
ExternalTextures findExternalTextures(const QStringList& args) {
    ExternalTextures textures;
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

bool parseSize(const QString& text, QSize* out) {
    static const QRegularExpression re(QStringLiteral("^(\\d+)x(\\d+)$"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) {
        return false;
    }
    *out = QSize(m.captured(1).toInt(), m.captured(2).toInt());
    return out->width() > 0 && out->height() > 0;
}

nm::Graph loadGraphFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(("cannot open graph file '" + path + "'").toStdString());
    }
    return nm::Graph::fromJson(file.readAll());
}

// Renders `graph` at `size`, calling render(time) `frames` times (settle
// iterations — see ARCHITECTURE.md "Runtime model" Time row; T3 does not
// detect feedback graphs itself, so the caller's --frames value is honored
// literally) before a single readSurface(). `meshPath` (may be empty) is
// the host mesh for mesh0 (host_meshes.h); `externalTextures` are the host
// pixels for external texture ids. Throws on any failure.
QImage renderGraph(const nm::Graph& graph, QSize size, double time, int frames, const QString& meshPath,
                   const ExternalTextures& externalTextures) {
    nm::Backend backend;
    backend.setup(nullptr, nm::resolveDataRoot(), size);
    nm::loadHostMeshes(backend, graph, meshPath);
    const QStringList graphExternalIds = nm::Backend::externalTextureIds(graph);
    for (const auto& [texId, path] : externalTextures) {
        if (!graphExternalIds.contains(texId)) {
            throw std::runtime_error(
                ("external texture '" + texId + "' is not sampled by this graph").toStdString());
        }
        const QImage image(path);
        if (image.isNull()) {
            throw std::runtime_error(("cannot read external texture '" + path + "'").toStdString());
        }
        backend.updateTextureFromSource(texId, image, nm::ExternalTextureOptions{true});
    }
    for (int i = 0; i < frames; ++i) {
        backend.render(graph, time);
    }
    return backend.readSurface();
}

int runSingleGraph(const QStringList& args) {
    const QString graphPath = findValue(args, QStringLiteral("--graph"));
    const QString sizeText = findValue(args, QStringLiteral("--size"));
    const QString timeText = findValue(args, QStringLiteral("--time"));
    const QString framesText = findValue(args, QStringLiteral("--frames"));
    const QString outPath = findValue(args, QStringLiteral("--out"));
    const QString meshPath = findValue(args, QStringLiteral("--mesh"));

    if (graphPath.isEmpty() || sizeText.isEmpty() || outPath.isEmpty()) {
        std::fprintf(stderr, "ERROR: --graph, --size, and --out are required\n");
        return 2;
    }

    QSize size;
    if (!parseSize(sizeText, &size)) {
        std::fprintf(stderr, "ERROR: --size must look like WxH (got '%s')\n", sizeText.toUtf8().constData());
        return 2;
    }

    ExternalTextures externalTextures;
    try {
        externalTextures = findExternalTextures(args);
    } catch (const std::invalid_argument& e) {
        std::fprintf(stderr, "ERROR: %s\n", e.what());
        return 2;
    }

    const double time = timeText.isEmpty() ? 0.0 : timeText.toDouble();
    const int frames = framesText.isEmpty() ? 1 : std::max(1, framesText.toInt());

    try {
        const nm::Graph graph = loadGraphFile(graphPath);
        const QImage image = renderGraph(graph, size, time, frames, meshPath, externalTextures);
        if (!nm::savePng(image, outPath)) {
            throw std::runtime_error("failed to write PNG");
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ERROR %s: %s\n", outPath.toUtf8().constData(), e.what());
        return 1;
    }

    std::printf("RENDERED %s\n", outPath.toUtf8().constData());
    return 0;
}

int runBatchManifest(const QString& manifestPath) {
    if (manifestPath.isEmpty()) {
        std::fprintf(stderr, "ERROR: --batch-manifest requires a path\n");
        return 2;
    }

    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "ERROR: cannot open batch manifest '%s'\n", manifestPath.toUtf8().constData());
        return 2;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        std::fprintf(stderr, "ERROR: batch manifest '%s' is not a JSON array (%s)\n",
                      manifestPath.toUtf8().constData(), parseError.errorString().toUtf8().constData());
        return 2;
    }

    bool anyFailed = false;
    for (const QJsonValue& itemValue : doc.array()) {
        const QJsonObject item = itemValue.toObject();
        const QString graphPath = item.value(QStringLiteral("graph")).toString();
        const QString outPath = item.value(QStringLiteral("out")).toString();
        const QString sizeText = item.value(QStringLiteral("size")).toString();
        const double time = item.value(QStringLiteral("time")).toDouble(0.0);
        const int frames = std::max(1, item.value(QStringLiteral("frames")).toInt(1));
        const QString meshPath = item.value(QStringLiteral("mesh")).toString();
        const QString outLabel = outPath.isEmpty() ? QStringLiteral("<unknown>") : outPath;

        try {
            if (outPath.isEmpty()) {
                throw std::runtime_error("manifest item missing 'out'");
            }
            QSize size;
            if (!parseSize(sizeText, &size)) {
                throw std::runtime_error("manifest item has invalid or missing 'size' (expected WxH)");
            }

            ExternalTextures externalTextures;
            const QJsonObject externals = item.value(QStringLiteral("externalTextures")).toObject();
            for (auto it = externals.begin(); it != externals.end(); ++it) {
                externalTextures.emplace_back(it.key(), it.value().toString());
            }

            const nm::Graph graph = loadGraphFile(graphPath);
            const QImage image = renderGraph(graph, size, time, frames, meshPath, externalTextures);
            if (!nm::savePng(image, outPath)) {
                throw std::runtime_error("failed to write PNG");
            }

            std::printf("RENDERED %s\n", outPath.toUtf8().constData());
        } catch (const std::exception& e) {
            std::fprintf(stderr, "ERROR %s: %s\n", outLabel.toUtf8().constData(), e.what());
            anyFailed = true;
        }
    }

    return anyFailed ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    // QGuiApplication (not QCoreApplication): QOffscreenSurface needs a
    // windowing-capable QPA platform plugin loaded, even though nm-render
    // never shows a window. The default "cocoa" plugin on macOS supports
    // true offscreen GL surfaces natively.
    QGuiApplication app(argc, argv);

    QStringList args = app.arguments();
    if (!args.isEmpty()) {
        args.removeFirst(); // program name
    }

    if (args.contains(QStringLiteral("--help"))) {
        std::printf("%s", kUsage);
        return 0;
    }
    if (args.isEmpty()) return usageError(QString());
    for (const QString& arg : args) {
        if (arg.startsWith(QStringLiteral("--")) && !kKnownFlags.contains(arg)) {
            return usageError(QStringLiteral("unknown option ") + arg);
        }
    }

    // Pre-declared flags dispatch through the registration hook so
    // implementing them never touches this file again (see flag_hooks.h).
    for (const QString& flag : kPreDeclaredFlags) {
        if (args.contains(flag)) {
            auto& registry = nm::flagHandlerRegistry();
            const auto it = registry.find(flag);
            if (it == registry.end()) {
                std::fprintf(stderr, "unimplemented: %s\n", flag.toUtf8().constData());
                return 2;
            }
            return it->second(args);
        }
    }

    try {
        if (args.contains(QStringLiteral("--batch-manifest"))) {
            return runBatchManifest(findValue(args, QStringLiteral("--batch-manifest")));
        }
        if (args.contains(QStringLiteral("--graph"))) {
            return runSingleGraph(args);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ERROR: %s\n", e.what());
        return 1;
    }

    return usageError(QStringLiteral("no mode: pass --dsl, --graph, --batch-manifest or a --dump-* flag"));
}
