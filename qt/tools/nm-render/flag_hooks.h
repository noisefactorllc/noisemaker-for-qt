#pragma once

#include <QString>
#include <QStringList>

#include <functional>
#include <map>
#include <utility>

namespace nm {

// Exit code returned by a registered flag's handler becomes nm-render's
// process exit code. `args` is the full CLI argument list (program name
// already stripped).
using FlagHandler = std::function<int(const QStringList& args)>;

// Header-only registry (Meyer's singleton), so translation units compiled
// separately into the nm-render executable — dump_*.cpp (compiler track:
// T8/T9/T10), any render_*.cpp (renderer track: T5's --samples) — can
// register a handler for a pre-declared CLI flag via a static-init-time
// side effect, without main.cpp ever including or knowing about them.
//
// main.cpp pre-declares the flag *names* it does not itself implement
// (--samples, --dsl, --dump-tokens, --dump-ast, --dump-validated,
// --dump-graph); if none of those names has a registered handler when
// main.cpp sees it on the command line, it prints "unimplemented: <flag>"
// and exits 2. This means implementing one of those flags is purely an
// additive change — a new .cpp file that registers itself — and main.cpp
// (a shared file, owned by no single task after T3) never needs another
// edit.
inline std::map<QString, FlagHandler>& flagHandlerRegistry() {
    static std::map<QString, FlagHandler> registry;
    return registry;
}

inline void registerFlagHandler(const QString& flag, FlagHandler handler) {
    flagHandlerRegistry()[flag] = std::move(handler);
}

// Construct a file-scope instance of this to register a handler at
// static-initialization time, e.g. in dump_lex.cpp:
//
//   namespace {
//   int handleDumpTokens(const QStringList& args) { ... }
//   nm::FlagHandlerRegistrar registrar(QStringLiteral("--dump-tokens"), &handleDumpTokens);
//   }
struct FlagHandlerRegistrar {
    FlagHandlerRegistrar(const QString& flag, FlagHandler handler) {
        registerFlagHandler(flag, std::move(handler));
    }
};

} // namespace nm
