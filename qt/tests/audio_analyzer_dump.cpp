// Parity-gate helper (NOT a ctest case): candidate side of
// parity/check_audio_analyzer.mjs. Feeds a signal through nm::AudioAnalyzer
// one render quantum at a time and records the four AnalyserNode getters
// at the requested quantum counts.
//
// Usage: audio_analyzer_dump <cases.json>
// cases.json: [{"name", "options": {fftSize, smoothingTimeConstant,
// minDecibels, maxDecibels}, "channels": n, "samples": [interleaved floats],
// "reads": [quantum counts, ascending]}, ...]
// Output: [{"name", "reads": [{"quanta", "byteFrequency", "floatFrequency"
// (null for -Infinity), "byteTimeDomain", "floatTimeDomain"}]}]

#include "../noisemaker/runtime/audio_analyzer.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <cstdio>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: audio_analyzer_dump <cases.json>\n");
        return 2;
    }
    QFile file(QString::fromLocal8Bit(argv[1]));
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "ERROR: cannot open '%s'\n", argv[1]);
        return 1;
    }
    const QJsonArray cases = QJsonDocument::fromJson(file.readAll()).array();
    QJsonArray results;
    for (const QJsonValue& caseValue : cases) {
        const QJsonObject c = caseValue.toObject();
        const QJsonObject o = c.value(QStringLiteral("options")).toObject();
        nm::AudioAnalyzer::Options options;
        options.fftSize = o.value(QStringLiteral("fftSize")).toInt();
        options.smoothingTimeConstant = o.value(QStringLiteral("smoothingTimeConstant")).toDouble();
        options.minDecibels = o.value(QStringLiteral("minDecibels")).toDouble();
        options.maxDecibels = o.value(QStringLiteral("maxDecibels")).toDouble();
        nm::AudioAnalyzer analyzer(options);
        const int channels = c.value(QStringLiteral("channels")).toInt();
        std::vector<float> samples;
        for (const QJsonValue& s : c.value(QStringLiteral("samples")).toArray()) samples.push_back(static_cast<float>(s.toDouble()));
        const qsizetype quantumValues = static_cast<qsizetype>(nm::AudioAnalyzer::kRenderQuantumFrames) * channels;
        qsizetype written = 0;
        QJsonArray reads;
        for (const QJsonValue& readValue : c.value(QStringLiteral("reads")).toArray()) {
            const qint64 target = readValue.toInteger();
            while (analyzer.renderQuanta() < target && written + quantumValues <= static_cast<qsizetype>(samples.size())) {
                analyzer.write(samples.data() + written, nm::AudioAnalyzer::kRenderQuantumFrames, channels);
                written += quantumValues;
            }
            std::vector<quint8> byteFrequency(static_cast<size_t>(analyzer.frequencyBinCount()));
            std::vector<float> floatFrequency(static_cast<size_t>(analyzer.frequencyBinCount()));
            std::vector<quint8> byteTime(static_cast<size_t>(analyzer.fftSize()));
            std::vector<float> floatTime(static_cast<size_t>(analyzer.fftSize()));
            // Same order as the oracle page: byte frequency first, so the
            // float read reuses this quantum's analysis.
            analyzer.getByteFrequencyData(byteFrequency.data(), analyzer.frequencyBinCount());
            analyzer.getFloatFrequencyData(floatFrequency.data(), analyzer.frequencyBinCount());
            analyzer.getByteTimeDomainData(byteTime.data(), analyzer.fftSize());
            analyzer.getFloatTimeDomainData(floatTime.data(), analyzer.fftSize());
            QJsonArray bf, ff, bt, ft;
            for (quint8 v : byteFrequency) bf.append(v);
            for (float v : floatFrequency) ff.append(std::isfinite(v) ? QJsonValue(static_cast<double>(v)) : QJsonValue(QJsonValue::Null));
            for (quint8 v : byteTime) bt.append(v);
            for (float v : floatTime) ft.append(static_cast<double>(v));
            reads.append(QJsonObject{{QStringLiteral("quanta"), static_cast<double>(analyzer.renderQuanta())},
                                     {QStringLiteral("byteFrequency"), bf},
                                     {QStringLiteral("floatFrequency"), ff},
                                     {QStringLiteral("byteTimeDomain"), bt},
                                     {QStringLiteral("floatTimeDomain"), ft}});
        }
        results.append(QJsonObject{{QStringLiteral("name"), c.value(QStringLiteral("name"))},
                                   {QStringLiteral("reads"), reads}});
    }
    const QByteArray json = QJsonDocument(results).toJson(QJsonDocument::Compact);
    std::fwrite(json.constData(), 1, static_cast<size_t>(json.size()), stdout);
    return 0;
}
