#include "diagnostics.h"

namespace nm {

DslSyntaxError DslSyntaxError::at(const QString& core, int line, int col) {
    return DslSyntaxError(
        QStringLiteral("%1 at line %2 col %3").arg(core).arg(line).arg(col), line, col);
}

} // namespace nm
