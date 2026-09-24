// The export kit's file inputs (export-kit/kit/src/host_inputs.h): WAV and
// Standard MIDI File readers, and their replay into nm::AudioInput and
// nm::MidiState, through to the scope, spectrum and roll pixels they drive.
// Plain executable, run from the repo root (WORKING_DIRECTORY in
// CMakeLists.txt).

#include "host_inputs.h"

#include "../noisemaker/compiler/dsl_compiler.h"
#include "../noisemaker/compiler/effect_registry.h"
#include "../noisemaker/runtime/backend.h"

#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtEndian>

#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

int g_failures = 0;

void check(bool condition, const char* description) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) ++g_failures;
}

bool throwsWith(const std::function<void()>& call, const char* needle) {
    try {
        call();
    } catch (const std::runtime_error& error) {
        std::printf("  error: %s\n", error.what());
        return std::string(error.what()).find(needle) != std::string::npos;
    }
    return false;
}

void put16(QByteArray& out, quint16 v) { char b[2]; qToLittleEndian(v, b); out.append(b, 2); }
void put32(QByteArray& out, quint32 v) { char b[4]; qToLittleEndian(v, b); out.append(b, 4); }
void put16be(QByteArray& out, quint16 v) { char b[2]; qToBigEndian(v, b); out.append(b, 2); }
void put32be(QByteArray& out, quint32 v) { char b[4]; qToBigEndian(v, b); out.append(b, 4); }

// RIFF/WAVE with one fmt chunk (optionally WAVE_FORMAT_EXTENSIBLE), an odd
// sized chunk the reader must skip with its pad byte, and one data chunk.
QByteArray wav(quint16 format, int channels, int rate, int bits, const QByteArray& data, bool extensible = false) {
    QByteArray fmt;
    put16(fmt, extensible ? 0xFFFE : format);
    put16(fmt, quint16(channels));
    put32(fmt, quint32(rate));
    put32(fmt, quint32(rate * channels * bits / 8));
    put16(fmt, quint16(channels * bits / 8));
    put16(fmt, quint16(bits));
    if (extensible) {
        put16(fmt, 22);
        put16(fmt, quint16(bits));
        put32(fmt, 0);
        put16(fmt, format); // sub-format GUID, first two bytes
        fmt.append(QByteArray(14, '\0'));
    }
    QByteArray body("WAVE");
    body.append("fmt ");
    put32(body, quint32(fmt.size()));
    body.append(fmt);
    body.append("LIST");
    put32(body, 3);
    body.append("abc");
    body.append('\0');
    body.append("data");
    put32(body, quint32(data.size()));
    body.append(data);
    QByteArray file("RIFF");
    put32(file, quint32(body.size()));
    file.append(body);
    return file;
}

QString writeFile(const QTemporaryDir& dir, const char* name, const QByteArray& bytes) {
    const QString path = dir.filePath(QString::fromLatin1(name));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        throw std::runtime_error("cannot write test input " + path.toStdString());
    }
    return path;
}

void testWav(const QTemporaryDir& dir) {
    QByteArray pcm16;
    for (qint16 v : {qint16(0), qint16(16384), qint16(-32768), qint16(32767)}) put16(pcm16, quint16(v));
    const kit::WavAudio mono = kit::readWav(writeFile(dir, "pcm16.wav", wav(1, 1, 48000, 16, pcm16)));
    check(mono.sampleRate == 48000 && mono.channels == 1 && mono.frames() == 4 && mono.samples[0] == 0.0f
              && mono.samples[1] == 0.5f && mono.samples[2] == -1.0f && std::abs(mono.samples[3] - 32767 / 32768.0f) < 1e-7f,
          "16-bit PCM mono reads as v / 32768, skipping an odd-sized chunk");

    QByteArray pcm24;
    for (qint32 v : {0x400000, -0x800000, 0x7FFFFF, -1}) {
        pcm24.append(char(v & 0xFF));
        pcm24.append(char((v >> 8) & 0xFF));
        pcm24.append(char((v >> 16) & 0xFF));
    }
    const kit::WavAudio stereo = kit::readWav(writeFile(dir, "pcm24.wav", wav(1, 2, 44100, 24, pcm24)));
    check(stereo.channels == 2 && stereo.frames() == 2 && stereo.samples[0] == 0.5f && stereo.samples[1] == -1.0f
              && stereo.samples[3] < 0.0f && stereo.samples[3] > -1e-6f,
          "24-bit PCM stereo sign-extends and interleaves");

    QByteArray f32;
    for (float v : {0.25f, -0.75f}) {
        char b[4];
        qToLittleEndian(v, b);
        f32.append(b, 4);
    }
    const kit::WavAudio floats = kit::readWav(writeFile(dir, "float.wav", wav(3, 1, 22050, 32, f32, true)));
    check(floats.frames() == 2 && floats.samples[0] == 0.25f && floats.samples[1] == -0.75f,
          "WAVE_FORMAT_EXTENSIBLE with an IEEE float sub-format reads floats verbatim");

    const QByteArray u8("\x80\xff\x00", 3);
    const kit::WavAudio bytes = kit::readWav(writeFile(dir, "u8.wav", wav(1, 1, 8000, 8, u8)));
    check(bytes.frames() == 3 && bytes.samples[0] == 0.0f && bytes.samples[2] == -1.0f,
          "8-bit PCM is unsigned around 128");

    check(throwsWith([&] { kit::readWav(dir.filePath(QStringLiteral("missing.wav"))); }, "cannot open file"),
          "a missing WAV reports that it cannot be opened");
    check(throwsWith([&] { kit::readWav(writeFile(dir, "text.wav", QByteArray("not audio at all"))); },
                     "not a RIFF/WAVE file"),
          "a non-RIFF file is refused");
    check(throwsWith([&] { kit::readWav(writeFile(dir, "adpcm.wav", wav(2, 1, 8000, 4, QByteArray(8, '\0')))); },
                     "unsupported sample format"),
          "a compressed WAV is refused with the supported formats");
}

QByteArray vlq(quint32 value) {
    QByteArray out(1, char(value & 0x7F));
    while (value >>= 7) out.prepend(char((value & 0x7F) | 0x80));
    return out;
}

QByteArray chunk(const char* tag, const QByteArray& body) {
    QByteArray out(tag, 4);
    put32be(out, quint32(body.size()));
    return out + body;
}

QByteArray header(quint16 format, quint16 tracks, quint16 division) {
    QByteArray body;
    put16be(body, format);
    put16be(body, tracks);
    put16be(body, division);
    return chunk("MThd", body);
}

void testMidiFile(const QTemporaryDir& dir) {
    // Format 1: a tempo track (120 bpm, then 60 bpm from tick 960) and a
    // note track with running status, a SysEx event and a program change.
    QByteArray tempo;
    tempo += vlq(0) + QByteArray("\xFF\x51\x03\x07\xA1\x20", 6);
    tempo += vlq(960) + QByteArray("\xFF\x51\x03\x0F\x42\x40", 6);
    tempo += vlq(0) + QByteArray("\xFF\x2F\x00", 3);
    QByteArray notes;
    notes += vlq(0) + QByteArray("\x90\x3C\x64", 3);      // note on C4 at 0 s
    notes += vlq(480) + QByteArray("\x40\x50", 2);        // running status: note on E4 at 0.5 s
    notes += vlq(0) + QByteArray("\xF0\x03\x7E\x7F\xF7", 5);
    notes += vlq(480) + QByteArray("\xC1\x05", 2);        // program change at 1.0 s
    notes += vlq(480) + QByteArray("\x80\x3C\x00", 3);    // note off C4 at 2.0 s (60 bpm)
    notes += vlq(0) + QByteArray("\xFF\x2F\x00", 3);
    const QString path = writeFile(dir, "song.mid", header(1, 2, 480) + chunk("MTrk", tempo) + chunk("MTrk", notes));
    const std::vector<kit::MidiEvent> events = kit::readMidiFile(path);
    for (const kit::MidiEvent& e : events) std::printf("  %.4f s %s\n", e.seconds, e.bytes.toHex(' ').constData());
    check(events.size() == 4, "channel messages survive; meta and SysEx events are dropped");
    check(events.size() == 4 && events[0].seconds == 0.0 && events[1].seconds == 0.5 && events[2].seconds == 1.0
              && events[3].seconds == 2.0,
          "the tempo map converts ticks to seconds across a tempo change");
    check(events.size() == 4 && events[1].bytes == QByteArray("\x90\x40\x50", 3)
              && events[2].bytes == QByteArray("\xC1\x05", 2),
          "running status expands to full messages; one-byte messages keep one data byte");

    QByteArray smpte;
    smpte += vlq(0) + QByteArray("\x90\x3C\x64", 3);
    smpte += vlq(40) + QByteArray("\x80\x3C\x00", 3);
    const std::vector<kit::MidiEvent> timed =
        kit::readMidiFile(writeFile(dir, "smpte.mid", header(0, 1, quint16(0xE728)) + chunk("MTrk", smpte)));
    check(timed.size() == 2 && std::abs(timed[1].seconds - 0.04) < 1e-12,
          "an SMPTE division (25 fps x 40 ticks) times 40 ticks as one frame, 0.04 s");

    check(throwsWith([&] { kit::readMidiFile(writeFile(dir, "bad.mid", QByteArray("RIFF....WAVE"))); },
                     "not a Standard MIDI File"),
          "a non-MIDI file is refused");
    check(throwsWith([&] {
              kit::readMidiFile(writeFile(dir, "cut.mid", header(0, 1, 96) + chunk("MTrk", QByteArray("\x00\x90\x3C", 3))));
          }, "truncated channel message"),
          "a truncated track is refused");
}

void testAudioReplay() {
    kit::WavAudio sine;
    sine.sampleRate = 48000;
    sine.channels = 1;
    // Quiet enough (about -40 dBFS in the peak bin) that the analyser's
    // -30 dB ceiling does not clip neighbouring bins to the same byte.
    for (int i = 0; i < 48000; ++i) sine.samples.push_back(0.05f * float(std::sin(2 * kPi * 1500.0 * i / 48000.0)));

    nm::AudioInput before;
    kit::AudioReplay early(sine);
    early.advanceTo(-0.5, before);
    const QJsonArray silentWave = before.snapshot().value(QStringLiteral("waveform")).toArray();
    bool flat = silentWave.size() == 128;
    for (const QJsonValue& v : silentWave) flat = flat && std::abs(v.toDouble() - 128.0 / 255.0) < 1e-6;
    check(flat, "before the file starts the input is silent (waveform 128 / 255)");

    nm::AudioInput input;
    kit::AudioReplay replay(sine);
    replay.advanceTo(0.5, input);
    const QJsonObject snap = input.snapshot();
    const QJsonArray spectrum = snap.value(QStringLiteral("spectrum")).toArray();
    int peak = 0;
    for (int i = 1; i < spectrum.size(); ++i) {
        if (spectrum.at(i).toDouble() > spectrum.at(peak).toDouble()) peak = i;
    }
    std::printf("  spectrum peak bin %d (1500 Hz / (48000 / 256) = 8.0)\n", peak);
    check(peak == 8, "a 1500 Hz tone peaks in spectrum bin 8 of the 256-point Noisedeck analyser");

    replay.advanceTo(3.0, input);
    replay.advanceTo(3.0 + 1.0 / 60.0, input);
    const QJsonArray after = input.snapshot().value(QStringLiteral("waveform")).toArray();
    flat = true;
    for (const QJsonValue& v : after) flat = flat && std::abs(v.toDouble() - 128.0 / 255.0) < 1e-6;
    check(flat, "after the file ends the input is silent again");
}

void testMidiReplay() {
    kit::MidiReplay replay({{0.25, QByteArray("\x90\x3C\x64", 3)}, {1.0, QByteArray("\x80\x3C\x00", 3)}});
    nm::MidiState state;
    const auto key = [&] {
        return state.snapshot().value(QStringLiteral("channels")).toObject().value(QStringLiteral("1")).toObject()
            .value(QStringLiteral("keys")).toArray().at(60).toDouble();
    };
    replay.advanceTo(0.2, state);
    const double beforeNote = key();
    replay.advanceTo(0.25, state);
    const double held = key();
    replay.advanceTo(2.0, state);
    std::printf("  key 60: %.0f -> %.0f -> %.0f\n", beforeNote, held, key());
    check(beforeNote == 0 && held == 100 && key() == 0, "MIDI events apply at their times, inclusive");
}

// The engine side of the kit wiring: silence draws scope's line through the
// middle row, a held note lights roll's lane for channel 1.
void testRenderedInputs(nm::EffectRegistry& registry) {
    const QSize size(64, 64);
    nm::Backend backend;
    backend.setup(nullptr, QStringLiteral("qt/noisemaker"), size);
    const nm::Graph scope = nm::compileGraph(QStringLiteral("search synth\nscope().write(o0)\nrender(o0)"), registry);
    nm::AudioInput silent;
    silent.update();
    backend.setAudioState(silent.snapshot());
    backend.render(scope, 0.0);
    const QImage image = backend.readSurface();
    int brightestRow = -1, brightest = -1;
    for (int y = 0; y < size.height(); ++y) {
        const int g = qGreen(image.pixel(32, y));
        if (g > brightest) { brightest = g; brightestRow = y; }
    }
    std::printf("  silent scope line row %d (green %d)\n", brightestRow, brightest);
    check(brightest > 200 && std::abs(brightestRow - 31.5) <= 1.5, "silent audio draws scope's line through the middle");

    const nm::Graph roll = nm::compileGraph(QStringLiteral("search synth\nroll().write(o0)\nrender(o0)"), registry);
    nm::MidiState state;
    kit::MidiReplay replay({{0.0, QByteArray("\x90\x3C\x7F", 3)}});
    replay.advanceTo(0.0, state);
    backend.setMidiState(state.snapshot());
    for (int frame = 0; frame < 30; ++frame) backend.render(roll, frame / 600.0);
    const QImage lit = backend.readSurface();
    // Channel 1 is the bottom lane (rows 60..63 read top-down); key 60 sits at
    // (60 - 36) / 48 of the lane. Notes enter at the left edge.
    int litPixels = 0;
    for (int y = 60; y < 64; ++y) {
        for (int x = 0; x < 8; ++x) litPixels += qGreen(lit.pixel(x, y)) > 200 ? 1 : 0;
    }
    std::printf("  roll lit pixels in lane 1: %d\n", litPixels);
    check(litPixels > 0, "a held MIDI note lights roll's channel 1 lane at the left edge");
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    nm::EffectRegistry registry;
    registry.loadAll(QStringLiteral("qt/noisemaker"));

    testWav(dir);
    testMidiFile(dir);
    testAudioReplay();
    testMidiReplay();
    testRenderedInputs(registry);

    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
