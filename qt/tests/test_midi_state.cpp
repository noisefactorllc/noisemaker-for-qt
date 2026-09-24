// nm::MidiState end to end: raw messages -> snapshot() ->
// Backend::setMidiState() -> midi() automation values. Field-level parity
// with the reference MidiState is gated separately by
// parity/check_midi_state.mjs; this test checks the host contract.

#include "../noisemaker/runtime/backend.h"
#include "../noisemaker/runtime/midi_state.h"

#include <QElapsedTimer>
#include <QJsonArray>

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const char* description) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) ++g_failures;
}

void send(nm::MidiState& state, std::initializer_list<int> bytes, const nm::MidiPort* port = nullptr, double time = 1000.0) {
    std::vector<quint8> data;
    for (int b : bytes) data.push_back(static_cast<quint8>(b));
    state.handleMessage(data.data(), static_cast<qsizetype>(data.size()), port, time);
}

QJsonObject midi(std::initializer_list<std::pair<const char*, QJsonValue>> fields) {
    QJsonObject object{{QStringLiteral("type"), QStringLiteral("Midi")},
                       {QStringLiteral("min"), 0.0},
                       {QStringLiteral("max"), 1.0},
                       {QStringLiteral("sensitivity"), 0.0}};
    for (const auto& [key, value] : fields) object.insert(QString::fromLatin1(key), value);
    return object;
}

double resolve(nm::Backend& backend, const nm::MidiState& state, const QJsonObject& descriptor) {
    backend.setMidiState(state.snapshot());
    return backend.resolveUniformValue(descriptor, 0.5).toDouble();
}

} // namespace

int main() {
    nm::Backend backend; // resolveUniformValue needs no GL context
    const nm::MidiPort keys{QStringLiteral("port-a"), QStringLiteral("Keys"), true};
    const nm::MidiPort pads{QStringLiteral("port-b"), QStringLiteral("Pads"), true};
    const nm::MidiPort keys2{QStringLiteral("port-c"), QStringLiteral("Keys"), true};

    nm::MidiState state;
    send(state, {0xb0, 1, 64}, &keys);
    send(state, {0xb0, 33, 32}, &keys);
    check(std::fabs(resolve(backend, state, midi({{"channel", 1}, {"mode", 6}, {"cc", 1}})) - ((64 << 7) | 32) / 16383.0) < 1e-12,
          "a 14-bit CC pair from one port drives mode 6");
    send(state, {0xb0, 33, 99}, &pads);
    check(std::fabs(resolve(backend, state, midi({{"channel", 1}, {"mode", 6}, {"cc", 1}})) - ((0 << 7) | 99) / 16383.0) < 1e-12,
          "the aggregate CC14 takes the complete pair of the last sending port");

    send(state, {0xe0, 0x7f, 0x7f}, &keys);
    check(resolve(backend, state, midi({{"channel", 1}, {"mode", 8}})) == 1.0, "pitch bend 16383 maps to max");

    send(state, {0x90, 60, 127}, &keys);
    check(resolve(backend, state, midi({{"channel", 1}, {"mode", 2}})) == 1.0, "a held note's velocity drives mode 2");
    send(state, {0x80, 60, 0}, &keys);
    check(resolve(backend, state, midi({{"channel", 1}, {"mode", 2}})) == 0.0, "note off closes the gate");

    // Port selection by name: unique, then ambiguous, then via inventory.
    send(state, {0xb0, 7, 127}, &keys);
    check(resolve(backend, state, midi({{"channel", 1}, {"mode", 5}, {"cc", 7}, {"name", QStringLiteral("Keys")}})) == 1.0,
          "a unique port name selects that port's state");
    send(state, {0xb0, 7, 64}, &keys2);
    check(resolve(backend, state, midi({{"channel", 1}, {"mode", 5}, {"cc", 7}, {"name", QStringLiteral("Keys")}})) == 0.0,
          "an ambiguous port name selects nothing (minimum)");
    check(resolve(backend, state, midi({{"channel", 1}, {"mode", 5}, {"cc", 7}, {"id", QStringLiteral("port-a")},
                                        {"name", QStringLiteral("Keys")}})) == 1.0,
          "a port id is authoritative");
    state.setPortInventory({keys, pads, nm::MidiPort{QStringLiteral("port-c"), QStringLiteral("Keys"), false}});
    check(resolve(backend, state, midi({{"channel", 1}, {"mode", 5}, {"cc", 7}, {"name", QStringLiteral("Keys")}})) == 1.0,
          "with an inventory, a name resolves through the physical port list");

    // MPE: RPN 6 on channel 1 configures a 7-member lower zone.
    send(state, {0xb0, 101, 0}, &pads);
    send(state, {0xb0, 100, 6}, &pads);
    send(state, {0xb0, 6, 7}, &pads);
    send(state, {0x93, 64, 100}, &pads, 2000.0);
    send(state, {0x95, 67, 90}, &pads, 3000.0);
    check(std::fabs(resolve(backend, state, midi({{"zone", 0}, {"mode", 0}})) - 67 / 127.0) < 1e-12,
          "the newest note in the configured lower zone drives zone mode 0");

    state.disconnectPort(QStringLiteral("port-b"));
    check(resolve(backend, state, midi({{"zone", 0}, {"mode", 0}})) == 0.0, "disconnecting a port drops its zone notes");
    check(resolve(backend, state, midi({{"channel", 1}, {"mode", 6}, {"cc", 1}})) == 0.0,
          "disconnecting a port clears the aggregate values it supplied");

    send(state, {0xf8});
    send(state, {0xf8}, &keys);
    check(state.snapshot().value(QStringLiteral("clockCount")).toDouble() == 2.0, "clock pulses count on the aggregate");

    const QJsonObject snap = state.snapshot();
    check(snap.value(QStringLiteral("ports")).toObject().contains(QStringLiteral("port-a"))
              && snap.value(QStringLiteral("unscopedState")).isObject()
              && !snap.value(QStringLiteral("channels")).toObject().value(QStringLiteral("1")).toObject().contains(QStringLiteral("ccPorts")),
          "snapshot carries ports and unscopedState but no origin bookkeeping");

    QElapsedTimer timer;
    timer.start();
    const int iterations = 200;
    for (int i = 0; i < iterations; ++i) backend.setMidiState(state.snapshot());
    std::printf("  snapshot+setMidiState: %.3f ms each (3 ports + unscoped)\n", double(timer.nsecsElapsed()) / 1e6 / iterations);

    state.reset();
    check(resolve(backend, state, midi({{"channel", 1}, {"mode", 8}})) == 8192 / 16383.0, "reset restores the pitch-bend center");

    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
