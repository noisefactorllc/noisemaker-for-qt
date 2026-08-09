// expand_dump.cpp -- parity-gate helper (candidate side of
// parity/check_expand.mjs). Loads the C++ EffectRegistry, runs
// nm::expand(nm::validate(nm::parse(nm::lex(src)))) for ONE DSL file, and
// prints the RAW (pre-normalization) expand() result as JSON:
// {"ok":true,"out":{passes,errors,programs,textureSpecs,renderSurface}} on
// success, {"ok":false,"error":<message>} if lexing/parsing/validating/
// expanding throws -- the exact envelope dump_validate.cpp already
// established (T9) and tools/dump-expand.mjs's own try/catch shape.
//
// NOT an nm-render flag: flag_hooks.h's kPreDeclaredFlags table (frozen
// since T3) has no `--dump-expand` slot, and main.cpp may never be edited
// to add one. Mirrors T9's registry_dump.cpp precedent exactly: a
// standalone executable registered in qt/tests/CMakeLists.txt
// (append-only shared territory for both tracks), NOT nm-render CLI
// plumbing. Per-file invocation (one process per file) matches
// dump_validate.cpp's own convention -- nm-render-adjacent binaries in
// this repo start fast enough that whole-corpus batching (Godot's own
// check_expand.mjs candidate convention, engine-startup-bound) isn't
// needed here.
//
// Usage: expand_dump <file.dsl> [dataRoot]   (dataRoot default:
// EffectRegistry::defaultDataRoot())

#include "../noisemaker/compiler/diagnostics.h"
#include "../noisemaker/compiler/effect_registry.h"
#include "../noisemaker/compiler/expander.h"
#include "../noisemaker/compiler/lexer.h"
#include "../noisemaker/compiler/parser.h"
#include "../noisemaker/compiler/validator.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: expand_dump <file.dsl> [dataRoot]\n");
        return 2;
    }
    const QString path = QString::fromLocal8Bit(argv[1]);
    const QString dataRoot = (argc > 2) ? QString::fromLocal8Bit(argv[2]) : nm::EffectRegistry::defaultDataRoot();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "ERROR: cannot open '%s'\n", path.toUtf8().constData());
        return 1;
    }
    const QString src = QString::fromUtf8(file.readAll());

    QJsonObject out;
    try {
        nm::EffectRegistry registry;
        registry.loadAll(dataRoot);
        const QJsonArray tokens = nm::lex(src);
        const QJsonObject ast = nm::parse(tokens);
        const QJsonObject validated = nm::validate(ast, registry);
        const nm::ExpandResult expanded = nm::expand(validated, registry);

        QJsonArray passesJson;
        for (const nm::ExpandedPass& p : expanded.passes) passesJson.append(nm::toRawPassJson(p));

        QJsonObject expandOut;
        expandOut.insert(QStringLiteral("passes"), passesJson);
        expandOut.insert(QStringLiteral("errors"), expanded.errors);
        expandOut.insert(QStringLiteral("programs"), expanded.programs);
        expandOut.insert(QStringLiteral("textureSpecs"), expanded.textureSpecs);
        expandOut.insert(QStringLiteral("renderSurface"),
                          expanded.renderSurface.isUndefined() ? QJsonValue(QJsonValue::Null) : expanded.renderSurface);

        out.insert(QStringLiteral("ok"), true);
        out.insert(QStringLiteral("out"), expandOut);
    } catch (const nm::DslSyntaxError& e) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"), e.message());
    } catch (const std::exception& e) {
        // Covers nm::UnsupportedDsl too (derives from std::runtime_error) --
        // check_expand.mjs only diffs ok:true/false plus (on ok:true) the
        // full `out` structure, never the error TEXT (matching
        // check_validate.mjs's own contract).
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"), QString::fromUtf8(e.what()));
    }

    const QByteArray json = QJsonDocument(out).toJson(QJsonDocument::Compact);
    std::fwrite(json.constData(), 1, static_cast<size_t>(json.size()), stdout);
    std::fputc('\n', stdout);
    return 0;
}
