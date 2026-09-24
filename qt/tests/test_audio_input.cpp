// nm::AudioInput / nm::AudioState end to end: interleaved samples ->
// analysers -> update() -> snapshot() -> Backend::setAudioState() ->
// audio() automation values, plus the audioWaveform / audioSpectrum /
// midiNoteGrid engine inputs that synth/scope, synth/spectrum and synth/roll
// read. Field-level parity is gated separately by
// parity/check_audio_analyzer.mjs and parity/check_audio_state.mjs.
// Run from the repo root (WORKING_DIRECTORY in CMakeLists.txt).

#include "../noisemaker/compiler/dsl_compiler.h"
#include "../noisemaker/compiler/effect_registry.h"
#include "../noisemaker/runtime/audio_state.h"
#include "../noisemaker/runtime/backend.h"
#include "../noisemaker/runtime/midi_state.h"

#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>

#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const char* description) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) ++g_failures;
}

// frames x channels interleaved: channel c = amplitude[c] * sin(2 pi f t) + dc[c].
std::vector<float> tone(int frames, const std::vector<double>& amplitude, const std::vector<double>& dc,
                        double frequency = 1125.0, double rate = 48000.0) {
    const int channels = static_cast<int>(amplitude.size());
    std::vector<float> out(static_cast<size_t>(frames * channels));
    for (int f = 0; f < frames; ++f) {
        for (int c = 0; c < channels; ++c) {
            out[static_cast<size_t>(f * channels + c)] =
                static_cast<float>(amplitude[c] * std::sin(6.283185307179586 * frequency * f / rate) + dc[c]);
        }
    }
    return out;
}

QJsonObject audio(int band, std::initializer_list<std::pair<const char*, QJsonValue>> fields = {}) {
    QJsonObject object{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("band"), band},
                       {QStringLiteral("min"), 0.0}, {QStringLiteral("max"), 1.0}};
    for (const auto& [key, value] : fields) object.insert(QString::fromLatin1(key), value);
    return object;
}

double resolve(nm::Backend& backend, const nm::AudioInput& input, const QJsonObject& descriptor) {
    backend.setAudioState(input.snapshot());
    return backend.resolveUniformValue(descriptor, 0.5).toDouble();
}

bool imagesDiffer(const QImage& a, const QImage& b) {
    return a.size() != b.size() || std::memcmp(a.constBits(), b.constBits(), static_cast<size_t>(a.sizeInBytes())) != 0;
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    nm::Backend evaluator; // resolveUniformValue needs no GL context

    nm::AudioInput input;
    check(resolve(evaluator, input, audio(3)) == 0.0, "no input: volume 0");

    // Default input, stereo: loud left channel, quiet right with a DC offset.
    const std::vector<float> stereo = tone(128 * 20, {0.8, 0.05}, {0.0, 0.25});
    input.pushDefault(stereo.data(), 128 * 20, 2);
    input.update();
    const double vol = resolve(evaluator, input, audio(3));
    const double mid = resolve(evaluator, input, audio(1));
    std::printf("  aggregate vol=%.4f mid=%.4f\n", vol, mid);
    check(vol > 0.0 && mid > 0.0, "a 1125 Hz tone (3 periods per quantum) raises the aggregate volume and mid band");
    const double rawAggregate = resolve(evaluator, input, audio(4));
    check(std::fabs(rawAggregate - (0.125 + 1.0) * 0.5) < 0.01,
          "aggregate raw is the stereo-pair mean of the last quantum (DC 0.125 -> 0.5625)");
    check(std::fabs(resolve(evaluator, input, audio(4, {{"channel", 2}})) - (0.25 + 1.0) * 0.5) < 0.01,
          "default channel 2 raw carries its own DC offset");
    // The tone sits in bin 6 (mid band); vol samples bins 0, 8, 16, ... and
    // so mostly sees channel 2's DC offset, not the tone.
    const double mid1 = resolve(evaluator, input, audio(1, {{"channel", 1}}));
    const double mid2 = resolve(evaluator, input, audio(1, {{"channel", 2}}));
    std::printf("  channel mid: 1=%.4f 2=%.4f\n", mid1, mid2);
    check(mid1 > mid2, "per-channel analysers separate the loud and quiet channels");

    // A selected device resolved by name, then through an inventory.
    const nm::AudioDevice iface{QStringLiteral("iface-1"), QStringLiteral("Interface"), 4, true};
    const std::vector<float> quad = tone(128 * 20, {0.0, 0.0, 0.9, 0.0}, {0.0, 0.0, 0.0, -0.5});
    input.pushDevice(iface, quad.data(), 128 * 20);
    input.update();
    check(resolve(evaluator, input, audio(3, {{"channel", 3}, {"name", QStringLiteral("Interface")}})) > 0.0,
          "a device channel resolves by unique name");
    check(std::fabs(resolve(evaluator, input, audio(4, {{"channel", 4}, {"name", QStringLiteral("Interface")}})) - 0.25) < 0.01,
          "a device channel raw value carries its DC offset");
    input.state().setDeviceInventory({iface, nm::AudioDevice{QStringLiteral("iface-2"), QStringLiteral("Interface"), 2, true}});
    check(resolve(evaluator, input, audio(3, {{"channel", 3}, {"name", QStringLiteral("Interface")}})) == 0.0,
          "an ambiguous inventory name selects nothing");
    check(resolve(evaluator, input, audio(3, {{"channel", 3}, {"name", QStringLiteral("Interface")}, {"id", QStringLiteral("iface-1")}})) > 0.0,
          "a device id is authoritative");

    input.disconnectDevice(QStringLiteral("iface-1"));
    check(resolve(evaluator, input, audio(3, {{"channel", 3}, {"name", QStringLiteral("Interface")}, {"id", QStringLiteral("iface-1")}})) == 0.0,
          "a disconnected device resolves to the minimum");
    input.disconnectDefault();
    check(resolve(evaluator, input, audio(3)) == 0.0 && resolve(evaluator, input, audio(3, {{"channel", 1}})) == 0.0,
          "disconnecting the default input clears the aggregate and its channels");

    // Engine inputs: synth/scope reads audioWaveform, synth/roll midiNoteGrid.
    nm::EffectRegistry registry;
    registry.loadAll(QStringLiteral("qt/noisemaker"));
    nm::Backend backend;
    backend.setup(nullptr, QStringLiteral("qt/noisemaker"), QSize(64, 64));
    const nm::Graph scope = nm::compileGraph(QStringLiteral("search synth\nscope().write(o0)\nrender(o0)"), registry);
    nm::AudioInput scopeInput;
    backend.setAudioState(scopeInput.snapshot());
    backend.render(scope, 0.0);
    const QImage silent = backend.readSurface();
    const std::vector<float> loud = tone(128 * 4, {0.9}, {0.0}, 750.0);
    scopeInput.pushDefault(loud.data(), 128 * 4, 1);
    scopeInput.update();
    backend.setAudioState(scopeInput.snapshot());
    backend.render(scope, 0.0);
    check(imagesDiffer(silent, backend.readSurface()), "synth/scope draws the audioWaveform engine input");

    const nm::Graph roll = nm::compileGraph(QStringLiteral("search synth\nroll().write(o0)\nrender(o0)"), registry);
    nm::Backend rollBackend;
    rollBackend.setup(nullptr, QStringLiteral("qt/noisemaker"), QSize(64, 64));
    rollBackend.render(roll, 0.0);
    const QImage noNotes = rollBackend.readSurface();
    nm::Backend rollNotes;
    rollNotes.setup(nullptr, QStringLiteral("qt/noisemaker"), QSize(64, 64));
    nm::MidiState midi;
    for (int key = 40; key < 90; key += 3) {
        const quint8 on[3] = {0x90, static_cast<quint8>(key), 120};
        midi.handleMessage(on, 3, nullptr, 0.0);
    }
    rollNotes.setMidiState(midi.snapshot());
    rollNotes.render(roll, 0.0);
    check(imagesDiffer(noNotes, rollNotes.readSurface()), "synth/roll reads held notes through midiNoteGrid");

    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
