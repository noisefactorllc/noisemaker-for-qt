// Parity-gate helper (NOT a ctest case): candidate side of
// parity/check_audio_state.mjs. Replays AudioState operations and prints one
// dumpState() per "dump" event, as a JSON array per scenario.
//
// Usage: audio_state_dump <scenarios.json>
// Numbers that JSON cannot carry arrive as the strings "NaN", "Infinity"
// and "-Infinity"; absent optional values are NaN.

#include "../noisemaker/runtime/audio_state.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

double number(const QJsonValue& value) {
    if (value.isDouble()) return value.toDouble();
    if (value.isString()) {
        const QString text = value.toString();
        if (text == QStringLiteral("Infinity")) return std::numeric_limits<double>::infinity();
        if (text == QStringLiteral("-Infinity")) return -std::numeric_limits<double>::infinity();
    }
    return qQNaN();
}

std::vector<quint8> bytes(const QJsonValue& value) {
    std::vector<quint8> out;
    for (const QJsonValue& b : value.toArray()) out.push_back(static_cast<quint8>(b.toInt()));
    return out;
}

nm::AudioDevice device(const QJsonObject& object) {
    nm::AudioDevice d;
    d.id = object.value(QStringLiteral("id")).toString();
    d.name = object.value(QStringLiteral("name")).toString();
    d.channelCount = object.value(QStringLiteral("channelCount")).toInt();
    d.connected = object.value(QStringLiteral("connected")).toBool(true);
    return d;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: audio_state_dump <scenarios.json>\n");
        return 2;
    }
    QFile file(QString::fromLocal8Bit(argv[1]));
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "ERROR: cannot open '%s'\n", argv[1]);
        return 1;
    }
    QJsonArray results;
    for (const QJsonValue& scenarioValue : QJsonDocument::fromJson(file.readAll()).array()) {
        const QJsonObject scenario = scenarioValue.toObject();
        nm::AudioState state;
        QJsonArray dumps;
        for (const QJsonValue& eventValue : scenario.value(QStringLiteral("events")).toArray()) {
            const QJsonObject e = eventValue.toObject();
            const QString op = e.value(QStringLiteral("op")).toString();
            if (op == QStringLiteral("update") || op == QStringLiteral("channelUpdate")) {
                nm::AudioState* target = &state;
                if (op == QStringLiteral("channelUpdate")) {
                    const int channel = e.value(QStringLiteral("channel")).toInt();
                    target = e.contains(QStringLiteral("id"))
                        ? state.deviceChannelState(e.value(QStringLiteral("id")).toString(), channel)
                        : state.defaultChannelState(channel);
                }
                const std::vector<quint8> data = bytes(e.value(QStringLiteral("bytes")));
                if (target) target->updateFromFrequencyData(data.data(), static_cast<int>(data.size()),
                                                            number(e.value(QStringLiteral("smoothing"))));
            } else if (op == QStringLiteral("bands")) {
                state.setBands(number(e.value(QStringLiteral("low"))), number(e.value(QStringLiteral("mid"))),
                               number(e.value(QStringLiteral("high"))));
            } else if (op == QStringLiteral("raw")) {
                state.setRaw(number(e.value(QStringLiteral("value"))));
            } else if (op == QStringLiteral("rawUnavailable")) {
                state.setRawUnavailable();
            } else if (op == QStringLiteral("registerDevice")) {
                state.registerDevice(device(e.value(QStringLiteral("device")).toObject()));
            } else if (op == QStringLiteral("setChannelValues")) {
                const QJsonObject v = e.value(QStringLiteral("values")).toObject();
                state.setChannelValues(e.value(QStringLiteral("id")).toString(), e.value(QStringLiteral("channel")).toInt(),
                                       number(v.value(QStringLiteral("low"))), number(v.value(QStringLiteral("mid"))),
                                       number(v.value(QStringLiteral("high"))), number(v.value(QStringLiteral("vol"))),
                                       number(v.value(QStringLiteral("raw"))));
            } else if (op == QStringLiteral("deviceRawUnavailable")) {
                state.setDeviceRawUnavailable(e.value(QStringLiteral("id")).toString());
            } else if (op == QStringLiteral("disconnectDevice")) {
                state.disconnectDevice(e.value(QStringLiteral("id")).toString());
            } else if (op == QStringLiteral("inventory")) {
                QVector<nm::AudioDevice> devices;
                for (const QJsonValue& d : e.value(QStringLiteral("devices")).toArray()) devices.append(device(d.toObject()));
                state.setDeviceInventory(devices);
            } else if (op == QStringLiteral("registerDefault")) {
                state.registerDefaultChannels(e.value(QStringLiteral("count")).toInt());
            } else if (op == QStringLiteral("disconnectDefault")) {
                state.disconnectDefaultInput();
            } else if (op == QStringLiteral("spectrum")) {
                const std::vector<quint8> data = bytes(e.value(QStringLiteral("bytes")));
                state.setSpectrum(data.data(), static_cast<int>(data.size()));
            } else if (op == QStringLiteral("waveform")) {
                const std::vector<quint8> data = bytes(e.value(QStringLiteral("bytes")));
                state.setWaveform(data.data(), static_cast<int>(data.size()));
            } else if (op == QStringLiteral("resetAggregate")) {
                state.resetAggregate();
            } else if (op == QStringLiteral("reset")) {
                state.reset();
            } else if (op == QStringLiteral("dump")) {
                dumps.append(state.dumpState());
            }
        }
        results.append(QJsonObject{{QStringLiteral("name"), scenario.value(QStringLiteral("name"))},
                                   {QStringLiteral("dumps"), dumps}});
    }
    const QByteArray json = QJsonDocument(results).toJson(QJsonDocument::Compact);
    std::fwrite(json.constData(), 1, static_cast<size_t>(json.size()), stdout);
    return 0;
}
