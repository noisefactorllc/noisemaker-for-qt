// dump_lex.cpp — implements nm-render's pre-declared `--dump-tokens <file>`
// flag (see flag_hooks.h). Registers a handler at static-init time so
// main.cpp never needs another edit (T3 hook contract).
//
// Prints nm::lex()'s token stream for the given DSL file as canonical
// JSON (a bare array, matching the shape the reference oracle
// tools/dump-tokens.mjs emits per file) to stdout. Candidate side of
// parity/check_lex.mjs, which spawns this once per corpus file.

#include "flag_hooks.h"

#include "../../noisemaker/compiler/diagnostics.h"
#include "../../noisemaker/compiler/lexer.h"

#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
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

int handleDumpTokens(const QStringList& args) {
    const QString path = findValue(args, QStringLiteral("--dump-tokens"));
    if (path.isEmpty()) {
        std::fprintf(stderr, "ERROR: --dump-tokens requires a file path\n");
        return 2;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "ERROR: cannot open '%s'\n", path.toUtf8().constData());
        return 1;
    }
    const QString src = QString::fromUtf8(file.readAll());

    try {
        const QJsonArray tokens = nm::lex(src);
        const QByteArray json = QJsonDocument(tokens).toJson(QJsonDocument::Compact);
        std::fwrite(json.constData(), 1, static_cast<size_t>(json.size()), stdout);
        std::fputc('\n', stdout);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ERROR: %s\n", e.what());
        return 1;
    }
    return 0;
}

nm::FlagHandlerRegistrar registrar(QStringLiteral("--dump-tokens"), &handleDumpTokens);

} // namespace
