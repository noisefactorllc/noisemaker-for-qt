#pragma once

// host_inputs.h -- file inputs for the exported project's renderer: WAV
// audio and Standard MIDI Files, replayed against the render clock into
// nm::AudioInput and nm::MidiState the way a live host feeds a microphone
// and a MIDI port.

#include "runtime/audio_state.h"
#include "runtime/midi_state.h"

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <vector>

namespace kit {

struct WavAudio {
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> samples; // interleaved, -1..1
    qsizetype frames() const { return channels > 0 ? qsizetype(samples.size()) / channels : 0; }
};

// Reads a RIFF/WAVE file: integer PCM of 8, 16, 24 or 32 bits and IEEE
// float of 32 or 64 bits, including WAVE_FORMAT_EXTENSIBLE. Throws
// std::runtime_error naming the file and the problem.
WavAudio readWav(const QString& path);

struct MidiEvent {
    double seconds = 0.0; // from the start of the file
    QByteArray bytes;     // one channel message, status byte first
};

// Reads a Standard MIDI File (format 0, 1 or 2; PPQN or SMPTE division)
// and returns its channel messages in time order, with running status
// expanded and the tempo map applied. Meta and system exclusive events are
// dropped. Throws std::runtime_error naming the file and the problem.
std::vector<MidiEvent> readMidiFile(const QString& path);

// Replays `audio` into `input` as a live input: advanceTo(t) pushes the
// frames up to t seconds (silence before 0 and after the end of the file),
// then runs one analysis update. The first call starts `lookbackSeconds`
// before t, so the analyser has history.
class AudioReplay {
public:
    explicit AudioReplay(WavAudio audio, double lookbackSeconds = 1.0);
    void advanceTo(double seconds, nm::AudioInput& input);

private:
    WavAudio m_audio;
    double m_lookback;
    qint64 m_cursor = 0; // next frame to push; may run before 0 or past the end
    bool m_started = false;
};

// Replays MIDI events into `state`: advanceTo(t) delivers every event at or
// before t seconds that has not been delivered yet.
class MidiReplay {
public:
    explicit MidiReplay(std::vector<MidiEvent> events);
    void advanceTo(double seconds, nm::MidiState& state);

private:
    std::vector<MidiEvent> m_events;
    std::size_t m_next = 0;
};

} // namespace kit
