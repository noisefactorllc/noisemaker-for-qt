// Parity-gate helper (NOT a ctest case): candidate side of
// parity/check_midi_state.mjs. Replays a scenario file through nm::MidiState
// and prints one dumpState() per "dump" event, as a JSON array per scenario.
//
// Usage: midi_state_dump <scenarios.json>
// Scenario file: [{"name": str, "events": [event, ...]}, ...]. Events:
//   {"op":"message","data":[bytes],"port":{"id","name"}|null,"time":ms}
//   {"op":"disconnect","id":str}
//   {"op":"register","port":{"id","name"}}
//   {"op":"inventory","ports":[{"id","name","connected"}]}
//   {"op":"reset"}
//   {"op":"dump"}
// Every scenario runs in one process, in file order, like the oracle, so
// the process-wide note-order counter advances identically on both sides.

#include "../noisemaker/runtime/midi_state.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>
#include <vector>

namespace {

nm::MidiPort portFrom(const QJsonObject& object) {
    nm::MidiPort port;
    port.id = object.value(QStringLiteral("id")).toString();
    port.name = object.value(QStringLiteral("name")).toString();
    port.connected = object.value(QStringLiteral("connected")).toBool(true);
    return port;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: midi_state_dump <scenarios.json>\n");
        return 2;
    }
    QFile file(QString::fromLocal8Bit(argv[1]));
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "ERROR: cannot open '%s'\n", argv[1]);
        return 1;
    }
    const QJsonArray scenarios = QJsonDocument::fromJson(file.readAll()).array();
    QJsonArray results;
    for (const QJsonValue& scenarioValue : scenarios) {
        const QJsonObject scenario = scenarioValue.toObject();
        nm::MidiState state;
        QJsonArray dumps;
        for (const QJsonValue& eventValue : scenario.value(QStringLiteral("events")).toArray()) {
            const QJsonObject event = eventValue.toObject();
            const QString op = event.value(QStringLiteral("op")).toString();
            if (op == QStringLiteral("message")) {
                std::vector<quint8> bytes;
                for (const QJsonValue& b : event.value(QStringLiteral("data")).toArray()) {
                    bytes.push_back(static_cast<quint8>(b.toInt()));
                }
                const QJsonValue portValue = event.value(QStringLiteral("port"));
                const double time = event.value(QStringLiteral("time")).toDouble();
                if (portValue.isObject()) {
                    const nm::MidiPort port = portFrom(portValue.toObject());
                    state.handleMessage(bytes.data(), static_cast<qsizetype>(bytes.size()), &port, time);
                } else {
                    state.handleMessage(bytes.data(), static_cast<qsizetype>(bytes.size()), nullptr, time);
                }
            } else if (op == QStringLiteral("disconnect")) {
                state.disconnectPort(event.value(QStringLiteral("id")).toString());
            } else if (op == QStringLiteral("register")) {
                state.registerPort(portFrom(event.value(QStringLiteral("port")).toObject()));
            } else if (op == QStringLiteral("inventory")) {
                QVector<nm::MidiPort> ports;
                for (const QJsonValue& p : event.value(QStringLiteral("ports")).toArray()) ports.append(portFrom(p.toObject()));
                state.setPortInventory(ports);
            } else if (op == QStringLiteral("reset")) {
                state.reset();
            } else if (op == QStringLiteral("dump")) {
                dumps.append(state.dumpState());
            }
        }
        results.append(QJsonObject{{QStringLiteral("name"), scenario.value(QStringLiteral("name"))},
                                   {QStringLiteral("dumps"), dumps}});
    }
    std::fwrite(QJsonDocument(results).toJson(QJsonDocument::Compact).constData(), 1,
                static_cast<size_t>(QJsonDocument(results).toJson(QJsonDocument::Compact).size()), stdout);
    return 0;
}
