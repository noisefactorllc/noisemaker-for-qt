#pragma once

#include <QList>
#include <QString>
#include <QSet>

namespace nm {

// A single structured runtime diagnostic that is recorded rather than
// thrown — the Qt-native counterpart of the reference's GAP-007 records
// (shaders/src/runtime/backends/diagnostics.js DiagnosticCollector +
// webgl2.js/pipeline.js record sites). Record shape and codes mirror the
// reference: ERR_DIMENSION_FALLBACK (pipeline.diagnostics, stage
// "dimension") and ERR_UNKNOWN_FORMAT_FALLBACK (backend.diagnostics,
// stage "texture-create"). `backend` carries this port's own native
// Desktop-GL backend id ("qt-gl"), the counterpart of the reference's
// "webgl2"/"webgpu".
struct SurfaceDiagnostic {
    QString code;
    QString backend;
    QString stage;
    QString spec;     // ERR_DIMENSION_FALLBACK: the unknown dimension key
    QString format;   // ERR_UNKNOWN_FORMAT_FALLBACK: the unknown format string
    QString fallback; // "screen" or "rgba8"
};

// A capped, queryable collector for structured diagnostics that are
// recorded rather than thrown (GAP-007): the historically-silent format
// and dimension fallbacks keep their behavior (no new rejection of
// previously accepted input) but surface structured records instead of
// pure silence. add() shifts records out beyond the cap (default 64),
// exactly like the reference DiagnosticCollector.
class DiagnosticCollector {
public:
    explicit DiagnosticCollector(int capacity = 64) : cap(capacity) {}

    const SurfaceDiagnostic& add(const SurfaceDiagnostic& record) {
        records.append(record);
        while (records.size() > cap) {
            records.removeFirst();
        }
        return records.last();
    }

    void clear() { records.clear(); }

    int cap = 64;
    QList<SurfaceDiagnostic> records;
};

// One call-site bundle handing the collector plus its dedup state into
// the pure resolver functions (resolveDimension / resolveGlFormat). The
// dedup set is owned by the caller (SurfaceCache keeps one per category),
// matching the reference's _warnedDimensionFallbacks /
// _warnedFormatFallbacks sets: each distinct key warns and records only
// once per cache lifetime. With a null collector the sink is inert, so
// library call sites without a diagnostic context behave exactly as
// before.
struct DiagnosticSink {
    DiagnosticCollector* collector = nullptr;
    QSet<QString>* seen = nullptr;
    QString backend = QStringLiteral("qt-gl");

    // Records ERR_DIMENSION_FALLBACK once per `key`; the warning text
    // mirrors the reference pipeline.js message with this port's own
    // prefix.
    void recordDimensionFallback(const QString& key, int screenSize);

    // Records ERR_UNKNOWN_FORMAT_FALLBACK once per `format`; mirrors the
    // reference webgl2.js message.
    void recordFormatFallback(const QString& format);
};

} // namespace nm
