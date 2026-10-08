// Unit tests for the runtime diagnostic port: the
// DiagnosticCollector / DiagnosticSink pair and resolveDimension's
// unknown-form fallback recording. Mirrors the reference's
// shaders/tests/test_backend_diagnostics.js format/dimension fallback
// cases (upstream dd4606ea + a0e9bbff) at the Qt-native surface
// (qt/noisemaker/runtime/diagnostics.{h,cpp}, surface.cpp). These are
// pure-CPU paths and need no GL context. Plain check() harness like the
// other Core-only tests (no test-framework dependency).

#include "../noisemaker/runtime/diagnostics.h"
#include "../noisemaker/runtime/surface.h"

#include <QJsonObject>
#include <QJsonValue>

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const std::string& description) {
    if (condition) {
        std::printf("PASS: %s\n", description.c_str());
    } else {
        std::printf("FAIL: %s\n", description.c_str());
        ++g_failures;
    }
}

// Collects qWarning messages so the tests can assert the
// once-per-unknown-key warning contract alongside the records.
struct WarningCapture {
    QStringList messages;
    QtMessageHandler previous = nullptr;

    void install() { previous = qInstallMessageHandler(messageHandler); }
    void uninstall() { qInstallMessageHandler(previous); }

    static void messageHandler(QtMsgType, const QMessageLogContext&, const QString& message) {
        instance()->messages.append(message);
    }
    static WarningCapture* instance() {
        static WarningCapture capture;
        return &capture;
    }
};

} // namespace

int main() {
    WarningCapture* capture = WarningCapture::instance();
    capture->install();

    // -- unknown string records ERR_DIMENSION_FALLBACK, behavior unchanged
    {
        nm::DiagnosticCollector collector;
        QSet<QString> seen;
        nm::DiagnosticSink sink;
        sink.collector = &collector;
        sink.seen = &seen;
        capture->messages.clear();

        check(nm::resolveDimension(QJsonValue(QStringLiteral("zoom")), 1000, QJsonObject(), &sink) == 1000,
              "behavior unchanged: unknown dimension forms resolve to screen size");
        check(collector.records.size() == 1, "one dimension fallback record");
        if (collector.records.size() == 1) {
            const nm::SurfaceDiagnostic& record = collector.records.first();
            check(record.code == QStringLiteral("ERR_DIMENSION_FALLBACK"),
                  "the dimension record carries the diagnostic code");
            check(record.backend == QStringLiteral("qt-gl"),
                  "the dimension record names the native backend");
            check(record.stage == QStringLiteral("dimension"),
                  "the dimension record uses the reference stage");
            check(record.spec == QStringLiteral("zoom"),
                  "the dimension record carries the unknown spec");
            check(record.fallback == QStringLiteral("screen"),
                  "the dimension record carries the fallback");
        }
        check(capture->messages.size() == 1, "the warning is emitted once per unknown spec");
        check(capture->messages.first().contains(QStringLiteral("Unknown dimension spec zoom"))
                  && capture->messages.first().contains(QStringLiteral("falling back to screen size (1000)")),
              "the warning text mirrors the reference message");

        // Deduplicated per key; each distinct unknown form records once.
        (void)nm::resolveDimension(QJsonValue(QStringLiteral("zoom")), 1000, QJsonObject(), &sink);
        check(collector.records.size() == 1, "the same fallback is deduplicated");
        check(capture->messages.size() == 1, "the warning fires once per unknown spec");
        QJsonObject bogus;
        bogus.insert(QStringLiteral("bogus"), true);
        (void)nm::resolveDimension(QJsonValue(bogus), 1000, QJsonObject(), &sink);
        check(collector.records.size() == 2, "each distinct unknown form is recorded once");

        // The object form's record key is its JSON form; a bool form uses
        // its stringified value.
        check(collector.records.last().spec == QStringLiteral("{\"bogus\":true}"),
              "the object form records the JSON key");
        (void)nm::resolveDimension(QJsonValue(true), 1000, QJsonObject(), &sink);
        check(collector.records.size() == 3 && collector.records.last().spec == QStringLiteral("true"),
              "the bool form records its stringified key");
    }

    // -- recognized forms and absent specs record nothing
    {
        nm::DiagnosticCollector collector;
        QSet<QString> seen;
        nm::DiagnosticSink sink;
        sink.collector = &collector;
        sink.seen = &seen;
        capture->messages.clear();

        // A malformed percent string keeps the screen-size fallback and is
        // surfaced like any other unknown form.
        check(nm::resolveDimension(QJsonValue(QStringLiteral("bogus%")), 1000, QJsonObject(), &sink) == 1000,
              "behavior unchanged: a malformed percent form falls back to screen size");
        check(collector.records.size() == 1
                  && collector.records.first().code == QStringLiteral("ERR_DIMENSION_FALLBACK")
                  && collector.records.first().spec == QStringLiteral("bogus%"),
              "a malformed percent form records a deduplicated ERR_DIMENSION_FALLBACK");
        (void)nm::resolveDimension(QJsonValue(QStringLiteral("bogus%")), 1000, QJsonObject(), &sink);
        check(collector.records.size() == 1, "the malformed percent fallback is deduplicated");

        QJsonObject paramSpec;
        paramSpec.insert(QStringLiteral("param"), QStringLiteral("x"));
        QJsonObject divideSpec;
        divideSpec.insert(QStringLiteral("screenDivide"), QStringLiteral("z"));
        QJsonObject scaleSpec;
        scaleSpec.insert(QStringLiteral("scale"), 0.5);
        const std::vector<QJsonValue> recognized = {
            QJsonValue(QStringLiteral("screen")),
            QJsonValue(QStringLiteral("auto")),
            QJsonValue(QStringLiteral("input")),
            QJsonValue(QStringLiteral("resolution")),
            QJsonValue(64.0),
            QJsonValue(QStringLiteral("50%")),
            QJsonValue(paramSpec),
            QJsonValue(divideSpec),
            QJsonValue(scaleSpec),
            QJsonValue(),
            QJsonValue(QJsonValue::Null),
        };
        bool allResolved = true;
        for (const QJsonValue& spec : recognized) {
            int expected = 1000;
            if (spec.isDouble()
                || (spec.isObject() && spec.toObject().contains(QStringLiteral("param")))) {
                expected = 64;
            } else if (spec.toString() == QStringLiteral("50%")
                       || (spec.isObject() && spec.toObject().contains(QStringLiteral("scale")))) {
                expected = 500;
            }
            if (nm::resolveDimension(spec, 1000, QJsonObject(), &sink) != expected) {
                allResolved = false;
            }
        }
        check(allResolved, "recognized forms resolve as before");
        check(collector.records.size() == 1,
              "recognized forms and absent specs add no diagnostic beyond the malformed-percent record");
        check(capture->messages.size() == 1, "recognized forms emit no warning");

        // 'input'/'resolution' resolve exactly like screen/auto (a0e9bbff).
        check(nm::resolveDimension(QJsonValue(QStringLiteral("input")), 1000) == 1000
                  && nm::resolveDimension(QJsonValue(QStringLiteral("resolution")), 1000) == 1000,
              "the validator-accepted input/resolution keywords resolve to the screen dimension");
    }

    // -- the collector caps at 64 and can be cleared; the sink is inert
    //    without a collector
    {
        nm::DiagnosticCollector collector;
        QSet<QString> seen;
        nm::DiagnosticSink sink;
        sink.collector = &collector;
        sink.seen = &seen;
        capture->messages.clear();
        for (int i = 0; i < 70; ++i) {
            (void)nm::resolveDimension(QJsonValue(QStringLiteral("zoom%1").arg(i)), 1000, QJsonObject(), &sink);
        }
        check(collector.records.size() == 64, "the collector caps at 64 records");
        check(collector.records.first().spec == QStringLiteral("zoom6")
                  && collector.records.last().spec == QStringLiteral("zoom69"),
              "records beyond the cap shift out oldest-first");
        collector.clear();
        check(collector.records.isEmpty(), "clear() empties the collector");

        nm::DiagnosticSink inert; // null collector and dedup set: no-op
        check(nm::resolveDimension(QJsonValue(QStringLiteral("zoom")), 1000, QJsonObject(), &inert) == 1000,
              "a sink without a collector leaves the resolution unchanged");
    }

    capture->uninstall();

    if (g_failures == 0) {
        std::printf("ALL PASS\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
