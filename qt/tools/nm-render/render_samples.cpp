// render_samples.cpp — implements nm-render's pre-declared `--samples
// <totalFrames>:<sampleEvery>` flag (see flag_hooks.h). Registers a handler
// at static-init time so main.cpp never needs another edit (T3 hook
// contract).
//
// Timed-sampling candidate side of parity/run_samples.sh, for stateful sims
// (navierStokes and similar) where a single pinned-time settle render never
// lets the simulation evolve. Mirrors the reference's own timed-drive
// semantics (ARCHITECTURE.md "Time" row / reference pipeline.js render()'s
// deltaTime handling) and godot's render_samples(): step a REAL per-frame
// deltaTime of 1/600 (one 60fps frame normalized into the reference's 10s
// animation loop -- see pipeline.js's own fallback comment "Approximate one
// frame at 60fps normalized to 10s loop" = 1/60/10 = 1/600) so `frame`
// frames of stepping cover `frame/60` seconds of sim time, snapshotting
// every `sampleEvery` frames to `<out-with-.tN-inserted>.png` where N is
// that elapsed second count -- matching export-and-render.mjs's own
// `--run-seconds`/`--sample-every` (seconds-based) golden naming
// (`<name>.golden.t<sec>.png`) exactly, so parity/run_samples.sh can diff
// sample-for-sample.
//
// `nm::Backend`'s public API is unchanged (T5 brief "Interfaces"): this
// file is pure CLI-side looping over the existing render(t)/readSurface().

#include "flag_hooks.h"

#include "../../noisemaker/runtime/backend.h"
#include "../../noisemaker/runtime/graph.h"
#include "../../noisemaker/runtime/png_io.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSize>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>

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

// Same resolution strategy as main.cpp's own (private, not shared --
// flag-handler translation units are self-contained by the T3 hook
// contract; see flag_hooks.h).
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

nm::Graph loadGraphFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(("cannot open graph file '" + path + "'").toStdString());
    }
    return nm::Graph::fromJson(file.readAll());
}

// "<dir>/name.candidate.png" -> "<dir>/name.candidate.t<sec>.png" (insert
// before the final ".png", matching export-and-render.mjs's
// `<programName>.golden.t<sec>.png` sibling convention exactly so
// parity/run_samples.sh's golden/candidate pairing by `t<sec>` needs no
// per-tool special-casing).
QString sampleOutPath(const QString& outBase, int seconds) {
    const QString suffix = QStringLiteral(".png");
    if (outBase.endsWith(suffix)) {
        return outBase.left(outBase.size() - suffix.size()) + QStringLiteral(".t%1.png").arg(seconds);
    }
    return outBase + QStringLiteral(".t%1.png").arg(seconds);
}

// `--samples <totalFrames>:<sampleEvery>`, both in FRAMES (not seconds --
// parity/run_samples.sh is the seconds<->frames adapter, matching the
// task brief's exact CLI contract). dt = 1/600 per frame (see file header);
// frame is 1-indexed (matching the reference's own `(startFrame+i+1)/600`
// timed-drive stepping in export-and-render.mjs), snapshot whenever
// `frame % sampleEvery == 0`.
int handleSamples(const QStringList& args) {
    const QString graphPath = findValue(args, QStringLiteral("--graph"));
    const QString sizeText = findValue(args, QStringLiteral("--size"));
    const QString samplesText = findValue(args, QStringLiteral("--samples"));
    const QString outPath = findValue(args, QStringLiteral("--out"));

    if (graphPath.isEmpty() || sizeText.isEmpty() || outPath.isEmpty()) {
        std::fprintf(stderr, "ERROR: --graph, --size, --samples, and --out are required\n");
        return 2;
    }

    QSize size;
    if (!parseSize(sizeText, &size)) {
        std::fprintf(stderr, "ERROR: --size must look like WxH (got '%s')\n", sizeText.toUtf8().constData());
        return 2;
    }

    static const QRegularExpression samplesRe(QStringLiteral("^(\\d+):(\\d+)$"));
    const QRegularExpressionMatch samplesMatch = samplesRe.match(samplesText);
    if (!samplesMatch.hasMatch()) {
        std::fprintf(stderr, "ERROR: --samples must look like <totalFrames>:<sampleEvery> (got '%s')\n",
                      samplesText.toUtf8().constData());
        return 2;
    }
    const int totalFrames = samplesMatch.captured(1).toInt();
    const int sampleEvery = std::max(1, samplesMatch.captured(2).toInt());
    if (totalFrames <= 0) {
        std::fprintf(stderr, "ERROR: --samples totalFrames must be positive (got '%s')\n",
                      samplesText.toUtf8().constData());
        return 2;
    }

    try {
        const nm::Graph graph = loadGraphFile(graphPath);
        nm::Backend backend;
        backend.setup(nullptr, resolveDataRoot(), size);

        static const double kDeltaTime = 1.0 / 600.0;
        for (int frame = 1; frame <= totalFrames; ++frame) {
            const double rawTime = static_cast<double>(frame) * kDeltaTime;
            const double t = rawTime - std::floor(rawTime); // normalized wrap into [0,1), matching `% 1.0`
            backend.render(graph, t);

            if (frame % sampleEvery == 0) {
                const int seconds = frame / 60;
                const QString samplePath = sampleOutPath(outPath, seconds);
                const QImage image = backend.readSurface();
                if (!nm::savePng(image, samplePath)) {
                    throw std::runtime_error(("failed to write PNG '" + samplePath + "'").toStdString());
                }
                std::printf("RENDERED %s\n", samplePath.toUtf8().constData());
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ERROR %s: %s\n", outPath.toUtf8().constData(), e.what());
        return 1;
    }

    return 0;
}

nm::FlagHandlerRegistrar registrar(QStringLiteral("--samples"), &handleSamples);

} // namespace
