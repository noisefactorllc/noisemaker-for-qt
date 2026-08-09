// dump_parse.cpp — implements nm-render's pre-declared `--dump-ast <file>`
// flag (see flag_hooks.h). Registers a handler at static-init time so
// main.cpp never needs another edit (T3 hook contract).
//
// Runs nm::parse(nm::lex(src)) for the given DSL file and prints the
// result as canonical JSON to stdout: {"ok":true,"ast":<Program>} on
// success, or {"ok":false,"error":<message>} if lexing/parsing throws --
// mirrors tools/dump-ast.mjs's own try/catch wrapping of parse(lex(src))
// exactly, so nm::parse() itself stays a pure mirror of the reference
// parse() (which also just throws, no wrapper). Candidate side of
// parity/check_parse.mjs, which spawns this once per corpus file.

#include "flag_hooks.h"

#include "../../noisemaker/compiler/diagnostics.h"
#include "../../noisemaker/compiler/lexer.h"
#include "../../noisemaker/compiler/parser.h"

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

int handleDumpAst(const QStringList& args) {
    const QString path = findValue(args, QStringLiteral("--dump-ast"));
    if (path.isEmpty()) {
        std::fprintf(stderr, "ERROR: --dump-ast requires a file path\n");
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
        const QJsonArray tokens = nm::lex(src);
        const QJsonObject ast = nm::parse(tokens);
        out.insert(QStringLiteral("ok"), true);
        out.insert(QStringLiteral("ast"), ast);
    } catch (const nm::DslSyntaxError& e) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"), e.message());
    } catch (const std::exception& e) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"), QString::fromUtf8(e.what()));
    }

    const QByteArray json = QJsonDocument(out).toJson(QJsonDocument::Compact);
    std::fwrite(json.constData(), 1, static_cast<size_t>(json.size()), stdout);
    std::fputc('\n', stdout);
    return 0;
}

nm::FlagHandlerRegistrar registrar(QStringLiteral("--dump-ast"), &handleDumpAst);

} // namespace
