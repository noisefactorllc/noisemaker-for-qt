// dump_validate.cpp — implements nm-render's pre-declared `--dump-validated
// <file>` flag (see flag_hooks.h). Registers a handler at static-init time
// so main.cpp never needs another edit (T3 hook contract).
//
// Loads the EffectRegistry from EffectRegistry::defaultDataRoot() (CWD-
// relative "qt/noisemaker" -- nm-render is always invoked from the repo
// root by the driving parity/*.mjs scripts, matching every other dump_*
// tool's convention of adding no new required flags beyond the file path),
// then runs nm::validate(nm::parse(nm::lex(src)), registry) for the given
// DSL file and prints the result as canonical JSON to stdout:
// {"ok":true,"out":<ValidatedProgram>} on success, or
// {"ok":false,"error":<message>} if lexing/parsing/validating throws --
// mirrors tools/dump-validate.mjs's own try/catch wrapping exactly (it
// wraps `validate(parse(lex(...)))` the SAME way for every file), so
// nm::validate() itself stays a pure mirror of the reference validate()
// (which only ever throws for the parser-guaranteed-unreachable missing-
// search case) PLUS the family's UnsupportedDsl fail-loud points (which
// the reference does NOT throw for -- see validator.h). Candidate side of
// parity/check_validate.mjs, which spawns this once per corpus file
// (matching check_lex.mjs/check_parse.mjs's own per-file convention, since
// nm-render's native startup is fast enough that batching isn't needed).
//
// Registry loading happens ONCE per process (once per file, since this is
// a fresh nm-render invocation each time) -- 210 small JSON files parse in
// well under the time a single DSL validate takes, so no caching is
// needed across the 339-file gate run.

#include "flag_hooks.h"

#include "../../noisemaker/compiler/diagnostics.h"
#include "../../noisemaker/compiler/effect_registry.h"
#include "../../noisemaker/compiler/lexer.h"
#include "../../noisemaker/compiler/parser.h"
#include "../../noisemaker/compiler/validator.h"

#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

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

int handleDumpValidated(const QStringList& args) {
    const QString path = findValue(args, QStringLiteral("--dump-validated"));
    if (path.isEmpty()) {
        std::fprintf(stderr, "ERROR: --dump-validated requires a file path\n");
        return 2;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "ERROR: cannot open '%s'\n", path.toUtf8().constData());
        return 1;
    }
    const QString src = QString::fromUtf8(file.readAll());

    QJsonObject out;
    try {
        nm::EffectRegistry registry;
        registry.loadAll(nm::EffectRegistry::defaultDataRoot());
        const QJsonArray tokens = nm::lex(src);
        const QJsonObject ast = nm::parse(tokens);
        const QJsonObject validated = nm::validate(ast, registry);
        out.insert(QStringLiteral("ok"), true);
        out.insert(QStringLiteral("out"), validated);
    } catch (const nm::DslSyntaxError& e) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"), e.message());
    } catch (const std::exception& e) {
        // Covers nm::UnsupportedDsl too (derives from std::runtime_error) --
        // the family's fail-loud points are reported the same way any other
        // validate()-time failure would be; check_validate.mjs only diffs
        // ok:true/false plus (on ok:true) the full `out` structure, never
        // the error TEXT (matching check_lex.mjs/check_parse.mjs's own
        // ok-only-on-failure comparison contract).
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"), QString::fromUtf8(e.what()));
    }

    const QByteArray json = QJsonDocument(out).toJson(QJsonDocument::Compact);
    std::fwrite(json.constData(), 1, static_cast<size_t>(json.size()), stdout);
    std::fputc('\n', stdout);
    return 0;
}

nm::FlagHandlerRegistrar registrar(QStringLiteral("--dump-validated"), &handleDumpValidated);

} // namespace
