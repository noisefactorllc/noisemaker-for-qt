#include "../noisemaker/runtime/backend.h"
#include "../noisemaker/runtime/graph.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <cstdio>

namespace nm {

struct BackendTestAccess {
    static int repeatCount(Backend& backend, const Pass& pass, double normalizedTime) {
        backend.m_time = normalizedTime;
        return backend.resolveRepeatCount(pass);
    }
};

} // namespace nm

namespace {

int g_failures = 0;

void check(bool condition, const char* description) {
    if (condition) {
        std::printf("PASS: %s\n", description);
    } else {
        std::printf("FAIL: %s\n", description);
        ++g_failures;
    }
}

bool approx(double actual, double expected, double tolerance = 1e-9) {
    return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
}

QJsonObject oscillator(int type, QJsonValue speed = 1.0) {
    return {
        {QStringLiteral("type"), QStringLiteral("Oscillator")},
        {QStringLiteral("oscType"), type},
        {QStringLiteral("min"), 0.0},
        {QStringLiteral("max"), 1.0},
        {QStringLiteral("speed"), speed},
        {QStringLiteral("offset"), 0.0},
        {QStringLiteral("seed"), 1.0},
    };
}

} // namespace

int main() {
    nm::Backend backend;

    {
        const QJsonObject saw = oscillator(2);
        check(approx(backend.resolveUniformValue(saw, 0.25).toDouble(), 0.25),
              "literal-rate saw oscillator resolves at normalized time");

        const QJsonObject carrier = oscillator(2, oscillator(0));
        const double expected = std::fmod(-20.0 / (std::acos(-1.0) * 2.0), 1.0) + 1.0;
        const double first = backend.resolveUniformValue(carrier, 0.25).toDouble();
        const double repeated = backend.resolveUniformValue(carrier, 0.25).toDouble();
        check(approx(first, expected), "nested sine rate integrates across normalized time");
        check(first == repeated, "nested oscillator evaluation is seekable and deterministic");

        QJsonObject noisyRate = oscillator(5);
        noisyRate.insert(QStringLiteral("seed"), 37.0);
        const QJsonObject noisyCarrier = oscillator(0, noisyRate);
        const double later = backend.resolveUniformValue(noisyCarrier, 0.83).toDouble();
        backend.resolveUniformValue(noisyCarrier, 0.12);
        check(std::isfinite(later)
                  && later == backend.resolveUniformValue(noisyCarrier, 0.83).toDouble(),
              "noise-modulated rate remains finite and frame-order independent");
    }

    {
        QJsonObject channel;
        channel.insert(QStringLiteral("key"), 64.0);
        channel.insert(QStringLiteral("velocity"), 127.0);
        channel.insert(QStringLiteral("gate"), 1.0);
        channel.insert(QStringLiteral("time"), 0.0);
        backend.setMidiState({
            {QStringLiteral("channels"), QJsonObject{{QStringLiteral("1"), channel}}},
        });

        QJsonObject midi{
            {QStringLiteral("type"), QStringLiteral("Midi")},
            {QStringLiteral("channel"), 1},
            {QStringLiteral("mode"), 2},
            {QStringLiteral("min"), 0.0},
            {QStringLiteral("max"), 1.0},
            {QStringLiteral("sensitivity"), 1.0},
        };
        const QJsonObject carrier = oscillator(2, midi);
        check(approx(backend.resolveUniformValue(carrier, 0.0125).toDouble(), 0.25),
              "current MIDI snapshot can drive oscillator rate");

        QJsonObject invalidChannelMidi = midi;
        invalidChannelMidi.insert(QStringLiteral("channel"), 0);
        const double channelZero = backend.resolveUniformValue(invalidChannelMidi, 0.5).toDouble();
        invalidChannelMidi.insert(QStringLiteral("channel"), 17);
        const double channelSeventeen = backend.resolveUniformValue(invalidChannelMidi, 0.5).toDouble();
        invalidChannelMidi.insert(QStringLiteral("channel"), 1.5);
        check(channelZero == 1.0 && channelSeventeen == 1.0
                  && backend.resolveUniformValue(invalidChannelMidi, 0.5).toDouble() == 1.0,
              "invalid MIDI channels fall back to channel 1");

        const QJsonObject shape = oscillator(0);
        midi.insert(QStringLiteral("min"), shape);
        midi.insert(QStringLiteral("max"), shape);
        const QJsonObject rangedCarrier = oscillator(2, midi);
        const double expected = std::fmod(-20.0 / (std::acos(-1.0) * 2.0), 1.0) + 1.0;
        check(approx(backend.resolveUniformValue(rangedCarrier, 0.25).toDouble(), expected, 1e-8),
              "automated external-input ranges integrate across normalized time");

        const QJsonObject selectedPort{
            {QStringLiteral("name"), QStringLiteral("Controller")},
            {QStringLiteral("connected"), true},
            {QStringLiteral("state"), QJsonObject{{QStringLiteral("channels"),
                                                   QJsonObject{{QStringLiteral("1"), channel}}}}},
        };
        backend.setMidiState({
            {QStringLiteral("ports"), QJsonObject{{QStringLiteral("present-id"), selectedPort}}},
        });
        QJsonObject selectedMidi = midi;
        selectedMidi.insert(QStringLiteral("min"), 0.0);
        selectedMidi.insert(QStringLiteral("max"), 1.0);
        selectedMidi.insert(QStringLiteral("name"), QStringLiteral("Controller"));
        selectedMidi.insert(QStringLiteral("id"), QStringLiteral("missing-id"));
        check(backend.resolveUniformValue(selectedMidi, 0.5).toDouble() == 0.0,
              "explicit MIDI id is authoritative and never falls back by name");
        selectedMidi.insert(QStringLiteral("id"), QStringLiteral("present-id"));
        check(backend.resolveUniformValue(selectedMidi, 0.5).toDouble() == 1.0,
              "selected MIDI resolves the exact port and channel snapshot");
    }

    {
        backend.setAudioState({
            {QStringLiteral("low"), 0.0},
            {QStringLiteral("mid"), 0.0},
            {QStringLiteral("high"), 0.0},
            {QStringLiteral("vol"), 0.0},
            {QStringLiteral("raw"), -1.0},
            {QStringLiteral("rawReady"), true},
        });
        QJsonObject audio{
            {QStringLiteral("type"), QStringLiteral("Audio")},
            {QStringLiteral("band"), 4},
            {QStringLiteral("min"), 0.0},
            {QStringLiteral("max"), 1.0},
        };
        const QJsonObject carrier = oscillator(2, audio);
        check(approx(backend.resolveUniformValue(carrier, 0.0125).toDouble(), 0.75),
              "bipolar raw audio snapshot can reverse oscillator rate");
    }

    {
        const QJsonObject inner{
            {QStringLiteral("type"), QStringLiteral("Audio")},
            {QStringLiteral("band"), 4},
            {QStringLiteral("min"), 0.0},
            {QStringLiteral("max"), 1.0},
            {QStringLiteral("channel"), 2},
            {QStringLiteral("name"), QStringLiteral("Inner Interface")},
            {QStringLiteral("id"), QStringLiteral("inner-id")},
        };
        const QJsonObject outer{
            {QStringLiteral("type"), QStringLiteral("Audio")},
            {QStringLiteral("band"), 0},
            {QStringLiteral("min"), inner},
            {QStringLiteral("max"), 1.0},
            {QStringLiteral("channel"), 1},
            {QStringLiteral("name"), QStringLiteral("Outer Interface")},
            {QStringLiteral("id"), QStringLiteral("outer-id")},
        };
        nm::Graph graph;
        nm::Pass pass;
        pass.uniforms.insert(QStringLiteral("amount"), outer);
        graph.passes.append(pass);

        const QJsonObject requirements = backend.getAudioInputRequirements(graph);
        const QJsonArray selected = requirements.value(QStringLiteral("selected")).toArray();
        check(selected.size() == 2, "nested selected audio descriptors both request capture");
        check(selected.at(0).toObject().value(QStringLiteral("id")).toString() == QStringLiteral("inner-id")
                  && selected.at(1).toObject().value(QStringLiteral("id")).toString() == QStringLiteral("outer-id"),
              "selected audio requirements preserve nested traversal order");

        QJsonObject invalidOuter = outer;
        invalidOuter.insert(QStringLiteral("_invalid"), true);
        pass.uniforms.insert(QStringLiteral("amount"), invalidOuter);
        graph.passes[0] = pass;
        const QJsonObject invalidRequirements = backend.getAudioInputRequirements(graph);
        check(invalidRequirements.value(QStringLiteral("selected")).toArray().isEmpty(),
              "invalid outer audio suppresses nested capture requirements");
        check(backend.resolveUniformValue(invalidOuter, 0.5).toDouble() == 0.0,
              "invalid outer audio with automated minimum fails closed at zero");

        nm::Graph taggedGraph;
        nm::Pass taggedPass;
        taggedPass.effectNamespace = QStringLiteral("synth");
        taggedPass.func = QStringLiteral("scope");
        taggedGraph.passes.append(taggedPass);
        check(backend.getAudioInputRequirements(taggedGraph)
                  .value(QStringLiteral("needsLegacy")).toBool(),
              "audio-tagged effects request legacy capture before backend setup");
    }

    {
        const QJsonObject selectedChannel{
            {QStringLiteral("low"), 0.8}, {QStringLiteral("mid"), 0.0},
            {QStringLiteral("high"), 0.0}, {QStringLiteral("vol"), 0.0},
        };
        const QJsonObject selectedDevice{
            {QStringLiteral("name"), QStringLiteral("Interface")},
            {QStringLiteral("connected"), true},
            {QStringLiteral("channels"), QJsonObject{{QStringLiteral("1"), selectedChannel}}},
        };
        backend.setAudioState({
            {QStringLiteral("devices"), QJsonObject{{QStringLiteral("present-id"), selectedDevice}}},
        });
        const QJsonObject explicitMissing{
            {QStringLiteral("type"), QStringLiteral("Audio")},
            {QStringLiteral("band"), 0}, {QStringLiteral("min"), 0.0},
            {QStringLiteral("max"), 1.0}, {QStringLiteral("channel"), 1},
            {QStringLiteral("name"), QStringLiteral("Interface")},
            {QStringLiteral("id"), QStringLiteral("missing-id")},
        };
        const QJsonObject exactId = [&] {
            QJsonObject value = explicitMissing;
            value.insert(QStringLiteral("id"), QStringLiteral("present-id"));
            return value;
        }();
        check(backend.resolveUniformValue(explicitMissing, 0.5).toDouble() == 0.0,
              "explicit audio id is authoritative and never falls back by name");
        check(approx(backend.resolveUniformValue(exactId, 0.5).toDouble(), 0.8),
              "selected audio resolves the exact device and channel snapshot");
    }

    {
        const QByteArray json = R"JSON({
            "passes": [{
                "id": "p0", "passType": "effect", "namespace": "synth",
                "func": "noise", "progName": "noise", "program": "p0_noise",
                "uniforms": {"scaleX": {"type":"Oscillator","oscType":2,"min":0,"max":1,"speed":1,"offset":0,"seed":1}},
                "uniformSpecs": {"scaleX": {"min":1,"max":100}},
                "inputs": {}, "outputs": {"out":"global_o0"}, "defines": {}
            }],
            "textures": {}, "allocations": {}, "renderSurface": "o0"
        })JSON";
        const nm::Graph graph = nm::Graph::fromJson(json);
        check(graph.passes.at(0).uniformSpecs.value(QStringLiteral("scaleX")).toObject()
                      .value(QStringLiteral("max")).toDouble() == 100.0,
              "graph loader preserves consumer uniform ranges for automation scaling");
        check(approx(backend.resolveUniformValue(
                         graph.passes.at(0).uniforms.value(QStringLiteral("scaleX")), 0.25,
                         graph.passes.at(0).uniformSpecs.value(QStringLiteral("scaleX")).toObject()).toDouble(),
                     25.75),
              "automation output scales through its consumer uniform range");
    }

    {
        nm::Pass pass;
        pass.repeat = QStringLiteral("iterations");
        pass.uniforms.insert(QStringLiteral("iterations"), oscillator(2));
        pass.uniformSpecs.insert(QStringLiteral("iterations"),
                                 QJsonObject{{QStringLiteral("min"), 1.0}, {QStringLiteral("max"), 4.0}});
        check(nm::BackendTestAccess::repeatCount(backend, pass, 0.25) == 1
                  && nm::BackendTestAccess::repeatCount(backend, pass, 0.75) == 3,
              "repeat counts consume the resolved and range-scaled automation uniform");
    }

    if (g_failures == 0) {
        std::printf("ALL PASS (test_automation)\n");
        return 0;
    }
    std::printf("%d FAILURE(S) (test_automation)\n", g_failures);
    return 1;
}
