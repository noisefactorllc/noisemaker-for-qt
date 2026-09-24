// nm-render's command line: --help lists every mode and flag and exits 0;
// an unknown option, no arguments, or no mode print the usage and exit 2.
//
//   test_nm_render_cli <path to nm-render>

#include <QCoreApplication>
#include <QProcess>
#include <QStringList>

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

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        std::fprintf(stderr, "usage: test_nm_render_cli <path to nm-render>\n");
        return 2;
    }
    const QString nmRender = QString::fromLocal8Bit(argv[1]);
    const QStringList flags = {
        QStringLiteral("--dsl"),         QStringLiteral("--graph"),        QStringLiteral("--samples"),
        QStringLiteral("--batch-manifest"), QStringLiteral("--size"),      QStringLiteral("--out"),
        QStringLiteral("--time"),        QStringLiteral("--frames"),       QStringLiteral("--dump-tokens"),
        QStringLiteral("--dump-ast"),    QStringLiteral("--dump-validated"), QStringLiteral("--dump-graph"),
        QStringLiteral("--mesh"),        QStringLiteral("--help"),
    };

    const Result help = run(nmRender, {QStringLiteral("--help")});
    bool listsAll = help.out.startsWith(QStringLiteral("usage: nm-render"));
    for (const QString& flag : flags) {
        if (!help.out.contains(flag)) {
            std::printf("  --help does not mention %s\n", qPrintable(flag));
            listsAll = false;
        }
    }
    check(help.exitCode == 0 && listsAll && help.err.isEmpty(),
          "--help prints every mode and flag on standard output and exits 0");

    const Result unknown = run(nmRender, {QStringLiteral("--dsl"), QStringLiteral("x.dsl"), QStringLiteral("--bogus")});
    check(unknown.exitCode == 2 && unknown.err.startsWith(QStringLiteral("ERROR: unknown option --bogus"))
              && unknown.err.contains(QStringLiteral("usage: nm-render")) && unknown.out.isEmpty(),
          "an unknown option is named, the usage follows, and the exit status is 2");

    const Result none = run(nmRender, {});
    check(none.exitCode == 2 && none.err.startsWith(QStringLiteral("usage: nm-render")),
          "no arguments print the usage and exit 2");

    const Result noMode = run(nmRender, {QStringLiteral("--size"), QStringLiteral("8x8")});
    check(noMode.exitCode == 2 && noMode.err.contains(QStringLiteral("no mode"))
              && noMode.err.contains(QStringLiteral("usage: nm-render")),
          "options without a mode name the problem and exit 2");

    const Result missing = run(nmRender, {QStringLiteral("--dsl"), QStringLiteral("x.dsl")});
    check(missing.exitCode == 2 && missing.err.contains(QStringLiteral("--dsl, --size, and --out are required")),
          "a mode without its required options exits 2");

    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
