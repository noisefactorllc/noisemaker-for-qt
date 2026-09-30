#include "diagnostics.h"

#include <QtGlobal>

namespace nm {

void DiagnosticSink::recordDimensionFallback(const QString& key, int screenSize) {
    if (collector == nullptr || seen == nullptr) {
        return;
    }
    if (seen->contains(key)) {
        return;
    }
    seen->insert(key);
    qWarning("[nm] Unknown dimension spec %s; falling back to screen size (%d)",
             key.toUtf8().constData(), screenSize);
    SurfaceDiagnostic record;
    record.code = QStringLiteral("ERR_DIMENSION_FALLBACK");
    record.backend = backend;
    record.stage = QStringLiteral("dimension");
    record.spec = key;
    record.fallback = QStringLiteral("screen");
    collector->add(record);
}

void DiagnosticSink::recordFormatFallback(const QString& format) {
    if (collector == nullptr || seen == nullptr) {
        return;
    }
    if (seen->contains(format)) {
        return;
    }
    seen->insert(format);
    qWarning("[nm] Unknown texture format '%s'; falling back to rgba8",
             format.toUtf8().constData());
    SurfaceDiagnostic record;
    record.code = QStringLiteral("ERR_UNKNOWN_FORMAT_FALLBACK");
    record.backend = backend;
    record.stage = QStringLiteral("texture-create");
    record.format = format;
    record.fallback = QStringLiteral("rgba8");
    collector->add(record);
}

} // namespace nm
