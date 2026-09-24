#pragma once

// midi_state.h -- engine-owned MIDI input state. Port of the reference
// shaders/src/runtime/external-input.js MidiChannelState / MidiState
// (message parsing, 14-bit CC pairing, RPN/NRPN, MPE zone configuration,
// per-port isolated state with an aggregate view, and the unscoped state).
//
// The host feeds raw MIDI message bytes with a stable port identity; it
// never builds state JSON by hand. snapshot() returns the object that
// nm::Backend::setMidiState() consumes. The library contains no MIDI
// capture or device code.

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace nm {

// Identity of one MIDI input port (reference Web MIDI {id, name}).
struct MidiPort {
    QString id;
    QString name;
    bool connected = true; // used by setPortInventory() only
};

class MidiState {
public:
    // portRegistry = true: the aggregate state with per-port isolated
    // states and an unscoped state (reference `new MidiState()`).
    // portRegistry = false: one isolated state (reference
    // `new MidiState({ portRegistry: false })`).
    explicit MidiState(bool portRegistry = true);
    ~MidiState();
    MidiState(const MidiState&) = delete;
    MidiState& operator=(const MidiState&) = delete;

    // Processes one raw MIDI message (reference handleMessage). `port` null
    // means a message without a port (the unscoped source). `timestampMs`
    // is the note-on time in epoch milliseconds (the reference uses
    // Date.now()); the overload without it uses the system clock.
    void handleMessage(const quint8* data, qsizetype length, const MidiPort* port = nullptr);
    void handleMessage(const quint8* data, qsizetype length, const MidiPort* port, double timestampMs);
    void handleMessage(const QByteArray& data, const MidiPort* port = nullptr);
    void handleMessage(const QByteArray& data, const MidiPort* port, double timestampMs);

    // Registers or reconnects a port (reference registerPort). Returns its
    // isolated state, or nullptr without a registry or with an empty id.
    MidiState* registerPort(const MidiPort& port);
    // Marks a port disconnected, resets its isolated state, and clears the
    // aggregate values that came from it (reference disconnectPort).
    void disconnectPort(const QString& id);
    // Physical port discovery, independent of which ports are open
    // (reference setPortInventory). Name selectors then resolve through it.
    void setPortInventory(const QVector<MidiPort>& ports);
    // Registered ports with their connection state (reference getPorts).
    QVector<MidiPort> ports() const;

    // Resets every channel, the clock, MPE zones and all port states.
    void reset();

    double clockCount() const { return m_clockCount; }

    // The JSON nm::Backend::setMidiState() consumes: clockCount, mpeZones,
    // channels "1".."16" (key, velocity, gate, time, keys, cc, cc14, nrpn,
    // rpn, pitchBend, pressure, polyPressure, heldNotes), and with a
    // registry also ports {id: {name, connected, state}}, unscopedState
    // and portInventory.
    QJsonObject snapshot() const;

    // Complete state including the per-origin bookkeeping, for the parity
    // gate against the reference (parity/check_midi_state.mjs).
    QJsonObject dumpState() const;

private:
    // Where a stored value came from: nothing, the unscoped source, or a port id.
    struct Origin {
        enum Kind { None, Unscoped, Port } kind = None;
        QString id;
        bool operator==(const Origin& other) const { return kind == other.kind && id == other.id; }
        bool operator!=(const Origin& other) const { return !(*this == other); }
    };
    struct HeldNote {
        int key = 0;
        int velocity = 0;
        double time = 0.0;
        double order = 0.0;
        Origin origin;
    };
    struct ParameterChange {
        bool rpn = false; // family: false = nrpn, true = rpn
        int parameter = 0;
        int value = 0;
        std::vector<int> resetChannels;
    };
    struct Channel {
        int key = 0;
        int velocity = 0;
        int gate = 0;
        double time = 0.0;
        std::array<quint8, 128> keys{};
        std::array<quint8, 128> cc{};
        std::array<quint16, 32> cc14{};
        std::array<Origin, 128> ccPorts{};
        std::array<Origin, 32> cc14Ports{};
        int pitchBend = 8192;
        int pressure = 0;
        std::array<quint8, 128> polyPressure{};
        std::vector<std::pair<int, int>> nrpn; // insertion-ordered Map
        std::vector<std::pair<int, int>> rpn;
        std::vector<HeldNote> heldNotes;       // insertion-ordered Map keyed by note.key
        std::array<std::optional<int>, 2> nrpnSelectors{};
        std::array<std::optional<int>, 2> rpnSelectors{};
        std::optional<bool> parameterFamily;   // nullopt, false = nrpn, true = rpn
        std::vector<std::pair<int, Origin>> nrpnPorts;
        std::vector<std::pair<int, Origin>> rpnPorts;
        Origin pitchBendPort;
        Origin pressurePort;
        std::array<Origin, 128> polyPressurePorts{};

        void noteOn(int key, int velocity, const HeldNote* sourceNote, const Origin& origin, double now);
        std::optional<ParameterChange> controlChange(int controller, int value);
        void resetControllers();
        void clearNotes();
        void noteOff(int key);
        void reset();
    };
    struct PortEntry {
        QString id;
        QString name;
        bool connected = true;
        std::unique_ptr<MidiState> state;
    };

    std::optional<ParameterChange> handleMessageImpl(const quint8* data, qsizetype length,
                                                     const MidiPort* port, double timestampMs);
    Channel& channel(int number);
    std::vector<int> configureMpeZone(int master, int count);
    static void clearNoteOrigin(Channel& channel, const Origin& origin);
    static void copyControllerReset(Channel& channel, const Channel& source, const Origin& origin,
                                    bool resetTimbre = false);
    void rebuildPortNameIndex();
    PortEntry* findPort(const QString& id);
    const PortEntry* findPort(const QString& id) const;
    QJsonObject channelJson(const Channel& channel, bool full) const;
    QJsonObject stateJson(bool full) const;

    std::array<Channel, 16> m_channels;
    double m_clockCount = 0.0;
    bool m_registry = true;
    std::vector<PortEntry> m_ports;                     // insertion-ordered Map keyed by id
    std::vector<std::pair<QString, QString>> m_portsByName; // name -> port id ("" = ambiguous)
    std::optional<std::vector<std::pair<QString, QString>>> m_portInventory; // name -> id ("" = ambiguous)
    std::unique_ptr<MidiState> m_unscoped;
    std::optional<int> m_mpeLower;
    std::optional<int> m_mpeUpper;
};

} // namespace nm
