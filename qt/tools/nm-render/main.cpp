// nm-render CLI. Shared file (see qt/CMakeLists.txt header comment and
// flag_hooks.h) — created ONCE here in T3 with every later flag
// pre-declared; implementing one of those flags is purely additive (a new
// translation unit in this directory that registers a handler) and must
// never require editing this file again.

#include "flag_hooks.h"

#include "../../noisemaker/runtime/backend.h"
#include "../../noisemaker/runtime/graph.h"
#include "../../noisemaker/runtime/png_io.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
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

QString findValue(const QStringList& args, const QString& flag) {
    const int idx = args.indexOf(flag);
    if (idx < 0 || idx + 1 >= args.size()) {
        return QString();
    }
    return args.at(idx + 1);
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

// dataRoot = "qt/noisemaker" (T3 brief). Derived from the nm-render
// executable's own location assuming the documented build layout
// (`qt/build` is a sibling of `qt/noisemaker`; see CLAUDE.md-equivalent
// "Build dir: qt/build"), which works regardless of the caller's current
// working directory. Falls back to the CWD-relative literal "qt/noisemaker"
// (the form the T3 smoke test's own invocation convention — run from the
// repo root — resolves correctly) if that layout isn't found, so nm-render
// still works when invoked exactly as documented even from a relocated or
// custom build directory.
QString resolveDataRoot() {
    QDir fromExe(QCoreApplication::applicationDirPath());
    if (fromExe.cdUp() && fromExe.cd(QStringLiteral("noisemaker"))
        && fromExe.exists(QStringLiteral("shaders"))) {
        return fromExe.absolutePath();
    }

    QDir fromCwd(QStringLiteral("qt/noisemaker"));
    if (fromCwd.exists(QStringLiteral("shaders"))) {
        return fromCwd.absolutePath();
    }

    return QStringLiteral("qt/noisemaker");
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
// literally) before a single readSurface(). Throws on any failure.
QImage renderGraph(const nm::Graph& graph, QSize size, double time, int frames) {
    nm::Backend backend;
    backend.setup(nullptr, resolveDataRoot(), size);
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

    if (graphPath.isEmpty() || sizeText.isEmpty() || outPath.isEmpty()) {
        std::fprintf(stderr, "ERROR: --graph, --size, and --out are required\n");
        return 2;
    }

    QSize size;
    if (!parseSize(sizeText, &size)) {
        std::fprintf(stderr, "ERROR: --size must look like WxH (got '%s')\n", sizeText.toUtf8().constData());
        return 2;
    }

    const double time = timeText.isEmpty() ? 0.0 : timeText.toDouble();
    const int frames = framesText.isEmpty() ? 1 : std::max(1, framesText.toInt());

    try {
        const nm::Graph graph = loadGraphFile(graphPath);
        const QImage image = renderGraph(graph, size, time, frames);
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
        const QString outLabel = outPath.isEmpty() ? QStringLiteral("<unknown>") : outPath;

        try {
            if (outPath.isEmpty()) {
                throw std::runtime_error("manifest item missing 'out'");
            }
            QSize size;
            if (!parseSize(sizeText, &size)) {
                throw std::runtime_error("manifest item has invalid or missing 'size' (expected WxH)");
            }

            const nm::Graph graph = loadGraphFile(graphPath);
            const QImage image = renderGraph(graph, size, time, frames);
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

    if (args.isEmpty()) {
        std::fprintf(stderr,
                      "usage: nm-render --graph <g.json> --size WxH --time T --frames N --out <c.png>\n"
                      "       nm-render --batch-manifest <m.json>\n");
        return 2;
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

    std::fprintf(stderr, "unimplemented: no recognized render mode in arguments\n");
    return 2;
}
