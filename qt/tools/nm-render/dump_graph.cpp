// dump_graph.cpp -- implements nm-render's two T10-owned pre-declared
// flags: `--dump-graph <file>` (GRAPH parity gate candidate) and
// `--dsl <file> --size WxH --time T --frames N --out <path>` (end-to-end
// live-DSL render). Registers both at static-init time so main.cpp never
// needs another edit (T3 hook contract, flag_hooks.h).
//
// `--dump-graph` prints {"ok":true,"out":<normalized graph JSON>} /
// {"ok":false,"error":...} -- the same envelope dump_validate.cpp (T9)
// established, consumed by this task's parity/check_graph.mjs.
//
// `--dsl` is built to be BYTE-IDENTICAL to `--graph <exported-from-the-
// same-source>`: nm::compileGraph() (dsl_compiler.h) produces the
// normalized GRAPH JSON (docs/GRAPH-JSON-SCHEMA.md -- the exact shape
// `--dump-graph` / tools/export-graph.mjs produce) and round-trips it
// through nm::Graph::fromJson(), the SAME parser `--graph` itself uses
// (qt/noisemaker/runtime/graph.cpp, read-only from here) -- so the two
// render paths can only diverge if this module's graph JSON differs from
// the oracle's, which parity/check_graph.mjs already gates end to end.
//
// resolveDataRoot()/parseSize()/findValue()/renderGraph() are duplicated
// from main.cpp on purpose: main.cpp's own copies are anonymous-
// namespace-local, and main.cpp is a frozen shared file after T3 (see
// PORTING-GUIDE.md file-ownership rules) -- dump_validate.cpp already
// established this per-file self-containment convention (it duplicates
// its own local findValue() rather than reaching into main.cpp).

#include "flag_hooks.h"

#include "../../noisemaker/compiler/dsl_compiler.h"
#include "../../noisemaker/compiler/effect_registry.h"
#include "../../noisemaker/runtime/backend.h"
#include "../../noisemaker/runtime/graph.h"
#include "../../noisemaker/runtime/png_io.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QImage>
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

namespace {

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

// Duplicated from main.cpp's resolveDataRoot() (see file header) --
// executable-relative first, CWD-relative "qt/noisemaker" fallback, so
// `--dsl` works "as documented" regardless of the caller's CWD, matching
// `--graph`'s own robustness (not just the narrower CWD-only resolution
// the parity-gate-only dump_*.cpp tools use, since those are always
// invoked from the repo root by their driving parity/*.mjs script).
QString resolveDataRoot() {
    QDir fromExe(QCoreApplication::applicationDirPath());
    if (fromExe.cdUp() && fromExe.cd(QStringLiteral("noisemaker")) && fromExe.exists(QStringLiteral("shaders"))) {
        return fromExe.absolutePath();
    }

    QDir fromCwd(QStringLiteral("qt/noisemaker"));
    if (fromCwd.exists(QStringLiteral("shaders"))) {
        return fromCwd.absolutePath();
    }

    return QStringLiteral("qt/noisemaker");
}

QString readTextFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error(("cannot open '" + path + "'").toStdString());
    }
    return QString::fromUtf8(file.readAll());
}

// Mirrors main.cpp's renderGraph() (same settle-frame contract: caller's
// --frames value is honored literally, no feedback-graph auto-detection).
QImage renderGraph(const nm::Graph& graph, const QString& dataRoot, QSize size, double time, int frames) {
    nm::Backend backend;
    backend.setup(nullptr, dataRoot, size);
    for (int i = 0; i < frames; ++i) {
        backend.render(graph, time);
    }
    return backend.readSurface();
}

int handleDumpGraph(const QStringList& args) {
    const QString path = findValue(args, QStringLiteral("--dump-graph"));
    if (path.isEmpty()) {
        std::fprintf(stderr, "ERROR: --dump-graph requires a file path\n");
        return 2;
    }

    QJsonObject out;
    try {
        const QString src = readTextFile(path);
        nm::EffectRegistry registry;
        // Parity-gate-only flag (candidate side of check_graph.mjs, always
        // invoked from the repo root) -- matches dump_validate.cpp's own
        // choice of the narrower CWD-relative resolution.
        registry.loadAll(nm::EffectRegistry::defaultDataRoot());
        const QJsonObject graphJson = nm::compileGraphJson(src, registry);
        out.insert(QStringLiteral("ok"), true);
        out.insert(QStringLiteral("out"), graphJson);
    } catch (const std::exception& e) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"), QString::fromUtf8(e.what()));
    }

    const QByteArray json = QJsonDocument(out).toJson(QJsonDocument::Compact);
    std::fwrite(json.constData(), 1, static_cast<size_t>(json.size()), stdout);
    std::fputc('\n', stdout);
    return 0;
}

int handleDsl(const QStringList& args) {
    const QString dslPath = findValue(args, QStringLiteral("--dsl"));
    const QString sizeText = findValue(args, QStringLiteral("--size"));
    const QString timeText = findValue(args, QStringLiteral("--time"));
    const QString framesText = findValue(args, QStringLiteral("--frames"));
    const QString outPath = findValue(args, QStringLiteral("--out"));

    if (dslPath.isEmpty() || sizeText.isEmpty() || outPath.isEmpty()) {
        std::fprintf(stderr, "ERROR: --dsl, --size, and --out are required\n");
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
        const QString src = readTextFile(dslPath);
        const QString dataRoot = resolveDataRoot();
        nm::EffectRegistry registry;
        registry.loadAll(dataRoot);
        const nm::Graph graph = nm::compileGraph(src, registry);
        const QImage image = renderGraph(graph, dataRoot, size, time, frames);
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

nm::FlagHandlerRegistrar registrarDumpGraph(QStringLiteral("--dump-graph"), &handleDumpGraph);
nm::FlagHandlerRegistrar registrarDsl(QStringLiteral("--dsl"), &handleDsl);

} // namespace
