#pragma once

// diagnostics.h — the lexer/parser error type. The reference doesn't define
// a custom error class: shaders/src/lang/{lexer,parser}.js just `throw new
// SyntaxError(...)` with message text of the form "<core> at line L col C"
// (a handful of throw sites omit the location suffix — ported verbatim,
// see lexer.cpp / parser.cpp call sites). td's dsl_syntax_error.py and
// godot's diagnostics.gd _fail()/push_error() are the cross-checked prior
// ports of this same text-format contract; DslSyntaxError::at() mirrors
// DslSyntaxError.At in td/noisemaker-for-touchdesigner and TD's
// dsl_syntax_error.py 1:1.
//
// PORTING-GUIDE.md "Compiler porting rules": diagnostics text is
// observable in dumps — dump_parse.cpp's {ok:false, error} entry surfaces
// this message verbatim, so the text is parity-relevant even though
// parity/check_parse.mjs itself only diffs the ok:true/false verdict, not
// the string (reference/oracle agreement on error TEXT isn't machine-
// gated here — see task report).

#include <QString>

#include <stdexcept>

namespace nm {

// Thrown by nm::lex / nm::parse on malformed DSL input. `message()` is the
// full display text (already carrying "at line L col C" where the
// reference attaches one); `line()`/`col()` are -1 when the reference
// throw site had no location.
class DslSyntaxError : public std::runtime_error {
public:
    explicit DslSyntaxError(const QString& message, int line = -1, int col = -1)
        : std::runtime_error(message.toStdString()), message_(message), line_(line), col_(col) {}

    // Builds "<core> at line L col C" (mirrors the reference's
    // `` `${msg} at line ${line} col ${col}` `` template literal, used by
    // the large majority of throw sites).
    static DslSyntaxError at(const QString& core, int line, int col);

    const QString& message() const { return message_; }
    int line() const { return line_; }
    int col() const { return col_; }

private:
    QString message_;
    int line_;
    int col_;
};

} // namespace nm
