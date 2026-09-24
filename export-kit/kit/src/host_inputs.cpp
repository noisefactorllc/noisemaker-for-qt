#include "host_inputs.h"

#include <QFile>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace kit {

namespace {

[[noreturn]] void fail(const QString& path, const char* problem) {
    throw std::runtime_error(("'" + path + "': ").toStdString() + problem);
}

QByteArray readAll(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) fail(path, "cannot open file");
    return file.readAll();
}

quint16 u16le(const char* p) { return qFromLittleEndian<quint16>(p); }
quint32 u32le(const char* p) { return qFromLittleEndian<quint32>(p); }
quint16 u16be(const char* p) { return qFromBigEndian<quint16>(p); }
quint32 u32be(const char* p) { return qFromBigEndian<quint32>(p); }

float pcmSample(const char* p, int bits) {
    switch (bits) {
    case 8:
        return (static_cast<unsigned char>(*p) - 128) / 128.0f;
    case 16:
        return qFromLittleEndian<qint16>(p) / 32768.0f;
    case 24: {
        const auto* b = reinterpret_cast<const unsigned char*>(p);
        qint32 v = qint32(b[0]) | (qint32(b[1]) << 8) | (qint32(b[2]) << 16);
        if (v & 0x800000) v -= 0x1000000;
        return static_cast<float>(v / 8388608.0);
    }
    default: // 32
        return static_cast<float>(qFromLittleEndian<qint32>(p) / 2147483648.0);
    }
}

} // namespace

WavAudio readWav(const QString& path) {
    const QByteArray data = readAll(path);
    const char* d = data.constData();
    if (data.size() < 12 || std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "WAVE", 4) != 0) {
        fail(path, "not a RIFF/WAVE file");
    }
    int format = 0, channels = 0, bits = 0;
    quint32 sampleRate = 0;
    const char* samples = nullptr;
    qsizetype sampleBytes = 0;
    for (qsizetype pos = 12; pos + 8 <= data.size();) {
        const quint32 size = u32le(d + pos + 4);
        const qsizetype body = pos + 8;
        const qsizetype available = std::min<qsizetype>(size, data.size() - body);
        if (std::memcmp(d + pos, "fmt ", 4) == 0) {
            if (available < 16) fail(path, "truncated fmt chunk");
            format = u16le(d + body);
            channels = u16le(d + body + 2);
            sampleRate = u32le(d + body + 4);
            bits = u16le(d + body + 14);
            if (format == 0xFFFE) { // WAVE_FORMAT_EXTENSIBLE: sub-format GUID starts with the format tag
                if (available < 26) fail(path, "truncated WAVE_FORMAT_EXTENSIBLE fmt chunk");
                format = u16le(d + body + 24);
            }
        } else if (std::memcmp(d + pos, "data", 4) == 0) {
            samples = d + body;
            sampleBytes = available;
        }
        pos = body + qsizetype(size) + (size & 1);
    }
    if (!format) fail(path, "no fmt chunk");
    if (!samples) fail(path, "no data chunk");
    const bool pcm = format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    const bool ieee = format == 3 && (bits == 32 || bits == 64);
    if (!pcm && !ieee) {
        fail(path, "unsupported sample format (use integer PCM of 8, 16, 24 or 32 bits, or 32/64-bit float)");
    }
    if (channels < 1 || channels > 32 || sampleRate < 1) fail(path, "invalid channel count or sample rate");

    WavAudio audio;
    audio.sampleRate = static_cast<int>(sampleRate);
    audio.channels = channels;
    const int stride = bits / 8;
    const qsizetype count = sampleBytes / stride / channels * channels;
    audio.samples.resize(static_cast<std::size_t>(count));
    for (qsizetype i = 0; i < count; ++i) {
        const char* p = samples + i * stride;
        float value;
        if (ieee && bits == 32) {
            value = qFromLittleEndian<float>(p);
        } else if (ieee) {
            value = static_cast<float>(qFromLittleEndian<double>(p));
        } else {
            value = pcmSample(p, bits);
        }
        audio.samples[static_cast<std::size_t>(i)] = value;
    }
    return audio;
}

std::vector<MidiEvent> readMidiFile(const QString& path) {
    const QByteArray data = readAll(path);
    const char* d = data.constData();
    const qsizetype n = data.size();
    if (n < 14 || std::memcmp(d, "MThd", 4) != 0 || u32be(d + 4) < 6) fail(path, "not a Standard MIDI File");
    const int tracks = u16be(d + 10);
    const quint16 division = u16be(d + 12);

    struct TickEvent {
        quint64 tick;
        int order;
        QByteArray bytes; // empty with tempo > 0 for a tempo change
        quint32 tempo = 0;
    };
    std::vector<TickEvent> events;
    int order = 0;
    qsizetype pos = 8 + qsizetype(u32be(d + 4));
    for (int track = 0; track < tracks; ++track) {
        if (pos + 8 > n || std::memcmp(d + pos, "MTrk", 4) != 0) fail(path, "missing or truncated MTrk chunk");
        const qsizetype end = std::min<qsizetype>(pos + 8 + qsizetype(u32be(d + pos + 4)), n);
        qsizetype p = pos + 8;
        quint64 tick = 0;
        unsigned char running = 0;
        const auto vlq = [&]() {
            quint32 value = 0;
            for (int i = 0; i < 4; ++i) {
                if (p >= end) fail(path, "truncated variable-length quantity");
                const unsigned char byte = static_cast<unsigned char>(d[p++]);
                value = (value << 7) | (byte & 0x7F);
                if (!(byte & 0x80)) return value;
            }
            fail(path, "variable-length quantity longer than 4 bytes");
        };
        while (p < end) {
            tick += vlq();
            if (p >= end) fail(path, "truncated event");
            unsigned char status = static_cast<unsigned char>(d[p]);
            if (status == 0xFF) { // meta
                if (p + 2 > end) fail(path, "truncated meta event");
                const unsigned char type = static_cast<unsigned char>(d[p + 1]);
                p += 2;
                const quint32 length = vlq();
                if (p + qsizetype(length) > end) fail(path, "truncated meta event");
                if (type == 0x51 && length == 3) {
                    const auto* b = reinterpret_cast<const unsigned char*>(d + p);
                    events.push_back({tick, order++, {}, (quint32(b[0]) << 16) | (quint32(b[1]) << 8) | b[2]});
                }
                p += length;
                if (type == 0x2F) break;
                continue;
            }
            if (status == 0xF0 || status == 0xF7) { // system exclusive
                ++p;
                p += vlq();
                continue;
            }
            if (status & 0x80) {
                running = status;
                ++p;
            } else if (!running) {
                fail(path, "data byte without a running status");
            }
            if (running >= 0xF0) fail(path, "unexpected system message in a track");
            const int dataBytes = (running & 0xF0) == 0xC0 || (running & 0xF0) == 0xD0 ? 1 : 2;
            if (p + dataBytes > end) fail(path, "truncated channel message");
            QByteArray message(1, static_cast<char>(running));
            message.append(d + p, dataBytes);
            p += dataBytes;
            events.push_back({tick, order++, message, 0});
        }
        pos = end;
    }

    std::stable_sort(events.begin(), events.end(),
                     [](const TickEvent& a, const TickEvent& b) { return a.tick < b.tick; });
    std::vector<MidiEvent> out;
    if (division & 0x8000) { // SMPTE: frames per second and ticks per frame, no tempo map
        const int fps = -static_cast<qint8>(division >> 8);
        const int ticksPerFrame = division & 0xFF;
        if (fps <= 0 || ticksPerFrame <= 0) fail(path, "invalid SMPTE division");
        for (const TickEvent& e : events) {
            if (!e.bytes.isEmpty()) out.push_back({double(e.tick) / (fps * ticksPerFrame), e.bytes});
        }
        return out;
    }
    if (division == 0) fail(path, "zero ticks per quarter note");
    double seconds = 0.0;
    quint64 lastTick = 0;
    double secondsPerTick = 0.5 / division; // 120 bpm until the first tempo event
    for (const TickEvent& e : events) {
        seconds += double(e.tick - lastTick) * secondsPerTick;
        lastTick = e.tick;
        if (e.bytes.isEmpty()) {
            secondsPerTick = e.tempo / 1e6 / division;
        } else {
            out.push_back({seconds, e.bytes});
        }
    }
    return out;
}

AudioReplay::AudioReplay(WavAudio audio, double lookbackSeconds)
    : m_audio(std::move(audio)), m_lookback(lookbackSeconds) {}

void AudioReplay::advanceTo(double seconds, nm::AudioInput& input) {
    const qint64 target = static_cast<qint64>(std::floor(seconds * m_audio.sampleRate));
    if (!m_started) {
        m_cursor = target - static_cast<qint64>(std::ceil(m_lookback * m_audio.sampleRate));
        m_started = true;
    }
    if (target > m_cursor) {
        const int channels = m_audio.channels;
        std::vector<float> block(static_cast<std::size_t>(target - m_cursor) * channels, 0.0f);
        const qint64 frames = m_audio.frames();
        for (qint64 f = std::max<qint64>(m_cursor, 0); f < std::min(target, frames); ++f) {
            std::memcpy(block.data() + (f - m_cursor) * channels,
                        m_audio.samples.data() + f * channels, sizeof(float) * channels);
        }
        input.pushDefault(block.data(), target - m_cursor, channels);
        m_cursor = target;
    }
    input.update();
}

MidiReplay::MidiReplay(std::vector<MidiEvent> events) : m_events(std::move(events)) {}

void MidiReplay::advanceTo(double seconds, nm::MidiState& state) {
    while (m_next < m_events.size() && m_events[m_next].seconds <= seconds) {
        const MidiEvent& event = m_events[m_next++];
        state.handleMessage(event.bytes, nullptr, event.seconds * 1000.0);
    }
}

} // namespace kit
