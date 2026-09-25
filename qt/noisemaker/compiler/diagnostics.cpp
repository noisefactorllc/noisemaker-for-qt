#include "diagnostics.h"

#include <QMap>

namespace nm {

namespace {

struct DiagnosticInfo {
    const char* defaultMessage;
    const char* severity;
    const char* stage;
};

const QMap<QString, DiagnosticInfo>& diagTable() {
    static const QMap<QString, DiagnosticInfo> table = {
        {QStringLiteral("L001"), {"Unexpected character", "error", "lexer"}},
        {QStringLiteral("L002"), {"Unterminated string literal", "error", "lexer"}},
        {QStringLiteral("L003"), {"Unterminated comment", "error", "lexer"}},
        {QStringLiteral("L004"), {"Output surface reference out of range", "error", "lexer"}},
        {QStringLiteral("P001"), {"Unexpected token", "error", "parser"}},
        {QStringLiteral("P002"), {"Expected closing parenthesis", "error", "parser"}},
        {QStringLiteral("P003"), {"Invalid automation arguments", "error", "parser"}},
        {QStringLiteral("P004"), {"Invalid search directive", "error", "parser"}},
        {QStringLiteral("P005"), {"Invalid output operation", "error", "parser"}},
        {QStringLiteral("P006"), {"Invalid subchain", "error", "parser"}},
        {QStringLiteral("P007"), {"Invalid call expression", "error", "parser"}},
        {QStringLiteral("S001"), {"Unknown identifier", "error", "semantic"}},
        {QStringLiteral("S002"), {"Argument out of range", "warning", "semantic"}},
        {QStringLiteral("S003"), {"Variable used before assignment", "error", "semantic"}},
        {QStringLiteral("S004"), {"Cannot assign null or undefined", "error", "semantic"}},
        {QStringLiteral("S005"), {"Illegal chain structure", "error", "semantic"}},
        {QStringLiteral("S006"), {"Starter chain missing write() call", "error", "semantic"}},
        {QStringLiteral("S007"), {"Deprecated parameter alias", "warning", "semantic"}},
        {QStringLiteral("S008"), {"Deprecated effect", "warning", "semantic"}},
        {QStringLiteral("R001"), {"Runtime error", "error", "runtime"}},
    };
    return table;
}

} // namespace

DslSyntaxError DslSyntaxError::at(const QString& core, int line, int col) {
    return DslSyntaxError(
        QStringLiteral("%1 at line %2 col %3").arg(core).arg(line).arg(col), line, col);
}

QString diagStage(const QString& code) {
    const auto it = diagTable().constFind(code);
    if (it != diagTable().constEnd()) {
        return QString::fromUtf8(it->stage);
    }
    return QStringLiteral("unknown");
}

QString diagSeverity(const QString& code) {
    const auto it = diagTable().constFind(code);
    if (it != diagTable().constEnd()) {
        return QString::fromUtf8(it->severity);
    }
    return QStringLiteral("error");
}

QString diagDefaultMessage(const QString& code) {
    const auto it = diagTable().constFind(code);
    if (it != diagTable().constEnd()) {
        return QString::fromUtf8(it->defaultMessage);
    }
    return QString();
}

} // namespace nm
