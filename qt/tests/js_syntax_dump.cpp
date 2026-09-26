// js_syntax_dump.cpp -- parity-gate helper (NOT a ctest case -- like
// registry_dump.cpp, this is CLI plumbing consumed by
// parity/check_func_bodies_differential.mjs, not a pass/fail assertion
// binary). It batch-decides Func bodies with nm::js::checkFunctionBody so
// the differential corpus runner can compare the checker's verdicts against
// `new Function('state', ...)` under any node major.
//
// Protocol (stdin -> stdout, one line per input, order preserved):
//   input line: the full body (`with(state){ return ...; }`), percent-encoded
//     UTF-8. Every byte outside [!-~] plus '%' itself is sent as %XX; bytes
//     below 0x21 and 0x7f likewise, so newlines cannot break the line framing.
//   output line: one character, V (Verdict::Valid), I (Verdict::Invalid)
//     or U (Verdict::Unsupported).
#include <QString>
#include <cstdio>
#include <iostream>
#include <string>

#include "noisemaker/compiler/js_syntax.h"

namespace {

QString decode(const QString& line) {
    // The wire encoding is per-UTF-16-code-unit %XX, so re-decode into code
    // units and let QString::fromUtf16 recombine surrogate pairs (UTF-8
    // decoding would corrupt astral identifiers).
    std::u16string units;
    units.reserve(static_cast<std::size_t>(line.size()));
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (c == u'%' && i + 4 < line.size()) {
            const QString hex = line.mid(i + 1, 4);
            bool ok = false;
            const ushort value = hex.toUShort(&ok, 16);
            if (!ok) {
                std::fputs("js_syntax_dump: malformed %XXXX escape\n", stderr);
                std::exit(2);
            }
            units.push_back(static_cast<char16_t>(value));
            i += 4;
        } else {
            units.push_back(static_cast<char16_t>(c.unicode()));
        }
    }
    return QString::fromUtf16(units.data(), static_cast<qsizetype>(units.size()));
}

} // namespace

int main() {
    std::string raw;
    while (std::getline(std::cin, raw)) {
        if (raw.empty()) continue;
        const QString body = decode(QString::fromStdString(raw));
        const nm::js::CheckResult result =
            nm::js::checkFunctionBody(QStringLiteral("state"), body);
        std::putchar(result.verdict == nm::js::Verdict::Valid    ? 'V'
                     : result.verdict == nm::js::Verdict::Invalid ? 'I'
                                                                  : 'U');
        std::putchar('\n');
    }
    return 0;
}
