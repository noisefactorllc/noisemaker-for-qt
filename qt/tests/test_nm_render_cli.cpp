// nm-render's command line: --help lists every mode and flag and exits 0;
// an unknown option, no arguments or no mode print the usage and exit 2; a
// malformed --external-texture value exits 2, and an asyncInit overlay id
// accepts a host PNG. The data root is found at
// NOISEMAKER_QT_DATA_ROOT, then in an install layout, then in the source
// tree (data_root.h); a wrong one is named.
// Qt may add its own diagnostics to standard error (for example
// "XDG_RUNTIME_DIR not set" on Linux), so stderr is searched, not matched
// from its start.
//
//   test_nm_render_cli <path to nm-render> <source data root (qt/noisemaker)>

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTemporaryDir>

#include <cstdio>

namespace {

int g_failures = 0;

void check(bool condition, const char* description) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) ++g_failures;
}

struct Result {
    int exitCode = -1;
    QString out;
    QString err;
};

Result run(const QString& program, const QStringList& args) {
    QProcess process;
    process.start(program, args);
    Result result;
    if (!process.waitForFinished(60000) || process.exitStatus() != QProcess::NormalExit) return result;
    result.exitCode = process.exitCode();
    result.out = QString::fromUtf8(process.readAllStandardOutput());
    result.err = QString::fromUtf8(process.readAllStandardError());
    return result;
}

// Runs `program` in `workingDir` with NOISEMAKER_QT_DATA_ROOT set to
// `dataRootEnv`, or unset when that is empty.
Result runIn(const QString& program, const QStringList& args, const QString& workingDir, const QString& dataRootEnv) {
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("NOISEMAKER_QT_DATA_ROOT"));
    if (!dataRootEnv.isEmpty()) env.insert(QStringLiteral("NOISEMAKER_QT_DATA_ROOT"), dataRootEnv);
    process.setProcessEnvironment(env);
    process.setWorkingDirectory(workingDir);
    process.start(program, args);
    Result result;
    if (!process.waitForFinished(60000) || process.exitStatus() != QProcess::NormalExit) return result;
    result.exitCode = process.exitCode();
    result.out = QString::fromUtf8(process.readAllStandardOutput());
    result.err = QString::fromUtf8(process.readAllStandardError());
    return result;
}

bool copyTree(const QString& from, const QString& to) {
    QDirIterator it(from, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString source = it.next();
        const QString target = to + QLatin1Char('/') + QDir(from).relativeFilePath(source);
        if (!QDir().mkpath(QFileInfo(target).path()) || !QFile::copy(source, target)) return false;
    }
    return true;
}

// Stages <prefix>/bin/nm-render (with the DLLs beside it, for Windows) and,
// when `sourceDataRoot` is given, <prefix>/share/noisemaker-qt/noisemaker.
// Returns the staged executable, or an empty string on failure.
QString stageInstall(const QString& nmRender, const QString& prefix, const QString& sourceDataRoot) {
    const QFileInfo exe(nmRender);
    const QString bin = prefix + QStringLiteral("/bin");
    if (!QDir().mkpath(bin)) return QString();
    const QString staged = bin + QLatin1Char('/') + exe.fileName();
    if (!QFile::copy(nmRender, staged)) return QString();
    QFile::setPermissions(staged, QFile(nmRender).permissions());
    for (const QString& dll : QDir(exe.path()).entryList({QStringLiteral("*.dll")}, QDir::Files)) {
        if (!QFile::copy(exe.path() + QLatin1Char('/') + dll, bin + QLatin1Char('/') + dll)) return QString();
    }
    if (!sourceDataRoot.isEmpty()) {
        const QString data = prefix + QStringLiteral("/share/noisemaker-qt/noisemaker");
        for (const QString& dir : {QStringLiteral("effects"), QStringLiteral("shaders"), QStringLiteral("fonts"), QStringLiteral("share")}) {
            if (QDir(sourceDataRoot).exists(dir) && !copyTree(sourceDataRoot + QLatin1Char('/') + dir, data + QLatin1Char('/') + dir)) {
                return QString();
            }
        }
    }
    return staged;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 3) {
        std::fprintf(stderr, "usage: test_nm_render_cli <path to nm-render> <source data root>\n");
        return 2;
    }
    const QString nmRender = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    const QString sourceDataRoot = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
    const QStringList flags = {
        QStringLiteral("--dsl"),         QStringLiteral("--graph"),        QStringLiteral("--samples"),
        QStringLiteral("--batch-manifest"), QStringLiteral("--size"),      QStringLiteral("--out"),
        QStringLiteral("--time"),        QStringLiteral("--frames"),       QStringLiteral("--dump-tokens"),
        QStringLiteral("--dump-ast"),    QStringLiteral("--dump-validated"), QStringLiteral("--dump-graph"),
        QStringLiteral("--mesh"),        QStringLiteral("--help"),         QStringLiteral("--external-texture"),
    };

    const Result help = run(nmRender, {QStringLiteral("--help")});
    bool listsAll = help.out.startsWith(QStringLiteral("usage: nm-render"));
    for (const QString& flag : flags) {
        if (!help.out.contains(flag)) {
            std::printf("  --help does not mention %s\n", qPrintable(flag));
            listsAll = false;
        }
    }
    check(help.exitCode == 0 && listsAll && !help.err.contains(QStringLiteral("usage: nm-render")),
          "--help prints every mode and flag on standard output and exits 0");

    const Result unknown = run(nmRender, {QStringLiteral("--dsl"), QStringLiteral("x.dsl"), QStringLiteral("--bogus")});
    check(unknown.exitCode == 2 && unknown.err.contains(QStringLiteral("ERROR: unknown option --bogus"))
              && unknown.err.contains(QStringLiteral("usage: nm-render")) && unknown.out.isEmpty(),
          "an unknown option is named, the usage follows, and the exit status is 2");

    const Result none = run(nmRender, {});
    check(none.exitCode == 2 && none.err.contains(QStringLiteral("usage: nm-render")) && none.out.isEmpty(),
          "no arguments print the usage and exit 2");

    const Result noMode = run(nmRender, {QStringLiteral("--size"), QStringLiteral("8x8")});
    check(noMode.exitCode == 2 && noMode.err.contains(QStringLiteral("no mode"))
              && noMode.err.contains(QStringLiteral("usage: nm-render")),
          "options without a mode name the problem and exit 2");

    const Result missing = run(nmRender, {QStringLiteral("--dsl"), QStringLiteral("x.dsl")});
    check(missing.exitCode == 2 && missing.err.contains(QStringLiteral("--dsl, --size, and --out are required")),
          "a mode without its required options exits 2");

    const Result badTexture = run(nmRender, {QStringLiteral("--graph"), QStringLiteral("x.json"), QStringLiteral("--size"),
                                             QStringLiteral("8x8"), QStringLiteral("--out"), QStringLiteral("x.png"),
                                             QStringLiteral("--external-texture"), QStringLiteral("textTex_step_1")});
    check(badTexture.exitCode == 2
              && badTexture.err.contains(QStringLiteral("--external-texture expects <texId>=<png>")),
          "an --external-texture value without ID=PNG exits 2");

    const Result badDslTexture = run(nmRender, {QStringLiteral("--dsl"), QStringLiteral("x.dsl"), QStringLiteral("--size"),
                                                QStringLiteral("8x8"), QStringLiteral("--out"), QStringLiteral("x.png"),
                                                QStringLiteral("--external-texture"), QStringLiteral("textTex_step_1")});
    check(badDslTexture.exitCode == 2
              && badDslTexture.err.contains(QStringLiteral("--external-texture expects <texId>=<png>")),
          "--dsl also rejects an --external-texture value without ID=PNG with exit 2");

    // Data root lookup (data_root.h). Each case runs from an unrelated
    // working directory unless it says otherwise.
    {
        QTemporaryDir scratch;
        const QString program = scratch.path() + QStringLiteral("/p.dsl");
        QFile file(program);
        const bool written = file.open(QIODevice::WriteOnly)
                             && file.write("search synth\nnoise().write(o0)\nrender(o0)\n") > 0;
        file.close();
        const QString cwd = scratch.path() + QStringLiteral("/cwd");
        const bool haveCwd = QDir().mkpath(cwd);
        const QString png = scratch.path() + QStringLiteral("/out.png");
        const QStringList renderArgs = {QStringLiteral("--dsl"), program, QStringLiteral("--size"), QStringLiteral("16x16"),
                                        QStringLiteral("--out"), png};

        // NOISEMAKER_QT_DATA_ROOT overrides every other location, and a wrong
        // one names its effects directory instead of reporting unknown effects.
        const QString badRoot = scratch.path() + QStringLiteral("/no-data-root");
        const Result wrongDump = runIn(nmRender, {QStringLiteral("--dump-graph"), program}, cwd, badRoot);
        check(written && haveCwd && wrongDump.out.contains(QStringLiteral("\"ok\":false"))
                  && wrongDump.out.contains(QDir::cleanPath(badRoot + QStringLiteral("/effects"))),
              "a wrong NOISEMAKER_QT_DATA_ROOT: --dump-graph names <root>/effects");
        const Result wrongRender = runIn(nmRender, renderArgs, cwd, badRoot);
        check(wrongRender.exitCode == 1 && wrongRender.err.contains(QDir::cleanPath(badRoot + QStringLiteral("/effects"))),
              "a wrong NOISEMAKER_QT_DATA_ROOT: --dsl exits 1 naming <root>/effects");
        const Result envDump = runIn(nmRender, {QStringLiteral("--dump-graph"), program}, cwd, sourceDataRoot);
        check(envDump.out.contains(QStringLiteral("\"ok\":true")), "NOISEMAKER_QT_DATA_ROOT set to the data root: --dump-graph compiles");

        // An install layout: <prefix>/bin/nm-render finds
        // <prefix>/share/noisemaker-qt/noisemaker from any working directory.
        const QString installed = stageInstall(nmRender, scratch.path() + QStringLiteral("/prefix"), sourceDataRoot);
        const Result installedRender = runIn(installed, renderArgs, cwd, QString());
        check(!installed.isEmpty() && installedRender.exitCode == 0 && QFileInfo::exists(png),
              "an installed nm-render renders from an unrelated working directory");
        const QString bare = stageInstall(nmRender, scratch.path() + QStringLiteral("/bare"), QString());
        const Result bareRender = runIn(bare, renderArgs, cwd, QString());
        check(!bare.isEmpty() && bareRender.exitCode == 1 && bareRender.err.contains(QStringLiteral("no effect definitions")),
              "the same executable without the share/ data finds none (the install path was the one used)");

        // The source tree, run from the repository root.
        const QString repoRoot = QDir::cleanPath(sourceDataRoot + QStringLiteral("/../.."));
        const Result sourceDump = runIn(nmRender, {QStringLiteral("--dump-graph"), program}, repoRoot, QString());
        check(sourceDump.out.contains(QStringLiteral("\"ok\":true")), "the source tree's qt/noisemaker is found from the repository root");
    }

    // A host texture under an asyncInit overlay id replaces the traced
    // overlay (the parity-llvmpipe job passes the reference's own overlay).
    // With fibers alpha 1 over black the frame is the overlay itself, so the
    // output PNG equals the host PNG, rendered first by nm-render.
    {
        QTemporaryDir scratch;
        const auto writeDsl = [&](const QString& name, const QByteArray& source) {
            QFile file(scratch.path() + QLatin1Char('/') + name);
            return file.open(QIODevice::WriteOnly) && file.write(source) == source.size() ? file.fileName() : QString();
        };
        const QString solid = writeDsl(QStringLiteral("solid.dsl"), "search synth\nsolid(color: #c86432).write(o0)\nrender(o0)\n");
        const QString fibers = writeDsl(QStringLiteral("fibers.dsl"),
                                        "search filter, synth\nsolid(color: #000000).fibers(density: 1, alpha: 1).write(o0)\nrender(o0)\n");
        const QString hostPng = scratch.path() + QStringLiteral("/host.png");
        const QString outPng = scratch.path() + QStringLiteral("/out.png");
        const QStringList size = {QStringLiteral("--size"), QStringLiteral("32x32")};
        const Result host = runIn(nmRender, QStringList{QStringLiteral("--dsl"), solid, QStringLiteral("--out"), hostPng} + size,
                                  scratch.path(), sourceDataRoot);
        const Result replaced = runIn(nmRender,
                                      QStringList{QStringLiteral("--dsl"), fibers, QStringLiteral("--out"), outPng,
                                                  QStringLiteral("--external-texture"), QStringLiteral("node_1_overlayTex=") + hostPng}
                                          + size,
                                      scratch.path(), sourceDataRoot);
        QFile hostFile(hostPng);
        QFile outFile(outPng);
        const bool same = hostFile.open(QIODevice::ReadOnly) && outFile.open(QIODevice::ReadOnly)
                          && hostFile.readAll() == outFile.readAll();
        check(host.exitCode == 0 && replaced.exitCode == 0 && same,
              "--external-texture node_1_overlayTex replaces the fibers overlay with the host PNG");
        const Result unknown = runIn(nmRender,
                                     QStringList{QStringLiteral("--dsl"), fibers, QStringLiteral("--out"), outPng,
                                                 QStringLiteral("--external-texture"), QStringLiteral("node_9_overlayTex=") + hostPng}
                                         + size,
                                     scratch.path(), sourceDataRoot);
        check(unknown.exitCode == 1 && unknown.err.contains(QStringLiteral("not sampled by this graph")),
              "an overlay id of a node the graph does not have exits 1");
    }

    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
