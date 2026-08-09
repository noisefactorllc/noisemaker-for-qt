// registry_dump.cpp -- parity-gate helper (candidate side of
// parity/check_registry.mjs). Loads the C++ EffectRegistry exactly as any
// real consumer would and prints its dumpSummary() ({ops, enums,
// paramAliases, effectAliases, effectKeys}) as compact JSON to stdout.
//
// NOT part of nm-render: main.cpp's flag-hook table (qt/tools/nm-render/
// flag_hooks.h) has no pre-declared slot for a registry-summary mode (only
// --samples/--dsl/--dump-tokens/--dump-ast/--dump-validated/--dump-graph
// were pre-declared in T3), and check_registry.mjs must go green in Step 1
// -- before the validator or its dump_validate.cpp candidate binary even
// exist (Step 2/3). A standalone executable registered in this file (T8's
// own ruling: qt/tests/CMakeLists.txt is append-only shared territory for
// both tracks) sidesteps both constraints without ever touching main.cpp.
// Plain main(), no QCoreApplication instance -- pure QJson work needs none
// (matches test_lexer.cpp / test_parser.cpp's convention).
//
// Usage: registry_dump [dataRoot]   (default: EffectRegistry::defaultDataRoot())

#include "../noisemaker/compiler/effect_registry.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cstdio>

int main(int argc, char** argv) {
    const QString dataRoot =
        (argc > 1) ? QString::fromLocal8Bit(argv[1]) : nm::EffectRegistry::defaultDataRoot();

    nm::EffectRegistry registry;
    registry.loadAll(dataRoot);

    const QJsonObject dump = registry.dumpSummary();
    const QByteArray json = QJsonDocument(dump).toJson(QJsonDocument::Compact);
    std::fwrite(json.constData(), 1, static_cast<size_t>(json.size()), stdout);
    std::fputc('\n', stdout);
    return 0;
}
