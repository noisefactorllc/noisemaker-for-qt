#include "backend.h"

#include "device_limits.h"
#include "pingpong.h"
#include "shader_assembly.h"

#include <QFile>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_1_Core>
#include <QSurfaceFormat>
#include <QVector>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace nm {

namespace {

// Binding point every std140 UBO program is bound to. Safe to reuse across
// programs: at most one program is ever in use (glUseProgram) at a draw
// call, and bindUniformBlock() rebinds the buffer for the active program's
// block every time it's called (backend.cpp bindUniformBlock()).
constexpr unsigned int kUboBindingPoint = 0;

// Hardcoded builtin programs -- these are NOT part of the byte-identical
// shader corpus (PORTING-GUIDE.md rule 1 governs `qt/noisemaker/shaders/
// effects/**` only). They mirror the reference's own runtime-owned
// constants: DEFAULT_VERTEX_SHADER (shaders/src/runtime/default-shaders.js)
// and the blit program's fragment source, which the reference's expander
// injects directly (shaders/src/runtime/expander.js `ensureBlitProgram`)
// rather than resolving from a file -- docs/GRAPH-JSON-SCHEMA.md: "the
// backend does NOT read shader source from [graph.programs]". Both are run
// through the same assembleShader() as every other program.
const char* const kDefaultVertexSource =
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 a_position;\n"
    "out vec2 v_texCoord;\n"
    "void main() {\n"
    "    v_texCoord = a_position * 0.5 + 0.5;\n"
    "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "}\n";

const char* const kBlitFragmentSource =
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 v_texCoord;\n"
    "uniform sampler2D src;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = texture(src, v_texCoord);\n"
    "}\n";

bool isBlit(const Pass& pass) {
    return pass.passType == QStringLiteral("blit");
}

QString serializeDefines(const QJsonObject& defines) {
    // Order doesn't affect the resulting compiled shader (see
    // shader_assembly.h); this just needs to be a stable string for a
    // given key/value set, which QJsonObject's (sorted) iteration already
    // guarantees.
    QStringList parts;
    for (auto it = defines.begin(); it != defines.end(); ++it) {
        parts << it.key() + QLatin1Char('=') + it.value().toVariant().toString();
    }
    return parts.join(QLatin1Char(','));
}

// Program cache key: (effectKey, progName, serialized defines, drawMode)
// per the T3 task brief Step 4, rather than reusing the reference's own
// node-prefixed `pass.program` id verbatim -- that would forgo cross-node
// sharing of identical effect+define combinations. `drawMode` is appended
// (T5) because it changes WHICH vertex shader gets linked into the program
// (a points/billboards pass compiles its own `<prog>.vert`, not the shared
// default vertex shader) -- a real behavioral difference the cache key
// must not conflate. Every non-agent pass has an empty drawMode, so this
// is a no-op suffix for all T3/T4-era cache keys.
QString programCacheKey(const Pass& pass) {
    return pass.effectKey + QLatin1Char('|') + pass.progName + QLatin1Char('|') + serializeDefines(pass.defines)
        + QLatin1Char('|') + pass.drawMode;
}

int fragmentOutputLocation(
    QOpenGLFunctions_4_1_Core* gl,
    unsigned int program,
    const QString& outputName) {
    int location = gl->glGetFragDataLocation(program, outputName.toUtf8().constData());
    if (location < 0 && outputName == QStringLiteral("color")) {
        location = gl->glGetFragDataLocation(program, "fragColor");
    }
    return location;
}

float jsonArrayComponent(const QJsonArray& arr, int index, float fallback) {
    if (index >= arr.size()) {
        return fallback;
    }
    const QJsonValue v = arr.at(index);
    return (v.isNull() || v.isUndefined()) ? fallback : static_cast<float>(v.toDouble());
}

constexpr double kTau = 6.283185307179586476925286766559;
constexpr int kMaxAutomationDepth = 8;

const QJsonObject kUnitRange{{QStringLiteral("min"), 0.0}, {QStringLiteral("max"), 1.0}};
const QJsonObject kOscillatorSpeedRange{{QStringLiteral("min"), -20.0}, {QStringLiteral("max"), 20.0}};
const QJsonObject kOscillatorOffsetRange{{QStringLiteral("min"), -1.0}, {QStringLiteral("max"), 1.0}};
const QJsonObject kOscillatorSeedRange{{QStringLiteral("min"), 1.0}, {QStringLiteral("max"), 9999.0}};
const QJsonObject kMidiSensitivityRange{{QStringLiteral("min"), 0.0}, {QStringLiteral("max"), 10.0}};

bool finiteNumber(const QJsonValue& value) {
    return value.isDouble() && std::isfinite(value.toDouble());
}

QString automationType(const QJsonObject& value) {
    QString type = value.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("Oscillator") || type == QStringLiteral("Midi") || type == QStringLiteral("Audio")) {
        return type;
    }
    const QString astType = value.value(QStringLiteral("_ast")).toObject().value(QStringLiteral("type")).toString();
    return astType == QStringLiteral("Oscillator") || astType == QStringLiteral("Midi") || astType == QStringLiteral("Audio")
        ? astType
        : QString();
}

bool isAutomationValue(const QJsonValue& value) {
    return value.isObject() && !automationType(value.toObject()).isEmpty();
}

double scaleAutomationValue(double value, const QJsonObject& range) {
    const QJsonValue minimum = range.value(QStringLiteral("min"));
    const QJsonValue maximum = range.value(QStringLiteral("max"));
    if (!finiteNumber(minimum) || !finiteNumber(maximum)) return value;
    return minimum.toDouble() + value * (maximum.toDouble() - minimum.toDouble());
}

double oscSine(double time) {
    return (1.0 - std::cos(time * kTau)) * 0.5;
}

double oscTriangle(double time) {
    const double fraction = time - std::floor(time);
    return 1.0 - std::abs(fraction * 2.0 - 1.0);
}

double oscSaw(double time) {
    return time - std::floor(time);
}

double oscSawInverse(double time) {
    return 1.0 - oscSaw(time);
}

double oscSquare(double time) {
    return oscSaw(time) >= 0.5 ? 1.0 : 0.0;
}

double hash21(double px, double py, double seed) {
    double x = std::fmod(px * 234.34 + seed, 1.0);
    double y = std::fmod(py * 435.345 + seed, 1.0);
    if (x < 0.0) x += 1.0;
    if (y < 0.0) y += 1.0;
    const double p = x + y + (x + y) * 34.23;
    return std::fmod(x * y * p, 1.0);
}

double noise2d(double px, double py, double seed) {
    const double ix = std::floor(px);
    const double iy = std::floor(py);
    double fx = px - ix;
    double fy = py - iy;
    fx = fx * fx * (3.0 - 2.0 * fx);
    fy = fy * fy * (3.0 - 2.0 * fy);
    const double a = hash21(ix, iy, seed);
    const double b = hash21(ix + 1.0, iy, seed);
    const double c = hash21(ix, iy + 1.0, seed);
    const double d = hash21(ix + 1.0, iy + 1.0, seed);
    return a * (1.0 - fx) * (1.0 - fy) + b * fx * (1.0 - fy)
        + c * (1.0 - fx) * fy + d * fx * fy;
}

double oscNoise(double time, double seed) {
    const double angle = std::fmod(time, 1.0) * kTau;
    const double loopX = std::cos(angle) * 2.0;
    const double loopY = std::sin(angle) * 2.0;
    return (noise2d(loopX + seed, loopY + seed, seed)
            + noise2d(loopX + seed * 2.0, loopY + seed * 2.0, seed))
        * 0.5;
}

struct IntegrationRule {
    const double* nodes;
    const double* weights;
    int size;
};

const double kNodes16[] = {
    -0.9894009349916499, -0.9445750230732326, -0.8656312023878318, -0.755404408355003,
    -0.6178762444026438, -0.4580167776572274, -0.2816035507792589, -0.0950125098376374,
    0.0950125098376374, 0.2816035507792589, 0.4580167776572274, 0.6178762444026438,
    0.755404408355003, 0.8656312023878318, 0.9445750230732326, 0.9894009349916499,
};
const double kWeights16[] = {
    0.0271524594117541, 0.0622535239386479, 0.0951585116824928, 0.1246289712555339,
    0.1495959888165767, 0.1691565193950025, 0.1826034150449236, 0.1894506104550685,
    0.1894506104550685, 0.1826034150449236, 0.1691565193950025, 0.1495959888165767,
    0.1246289712555339, 0.0951585116824928, 0.0622535239386479, 0.0271524594117541,
};
const double kNodes8[] = {
    -0.9602898564975363, -0.7966664774136267, -0.525532409916329, -0.1834346424956498,
    0.1834346424956498, 0.525532409916329, 0.7966664774136267, 0.9602898564975363,
};
const double kWeights8[] = {
    0.1012285362903763, 0.2223810344533745, 0.3137066458778873, 0.362683783378362,
    0.362683783378362, 0.3137066458778873, 0.2223810344533745, 0.1012285362903763,
};
const double kNodes4[] = {-0.8611363115940526, -0.3399810435848563, 0.3399810435848563, 0.8611363115940526};
const double kWeights4[] = {0.3478548451374538, 0.6521451548625461, 0.6521451548625461, 0.3478548451374538};
const double kNodes2[] = {-0.5773502691896257, 0.5773502691896257};
const double kWeights2[] = {1.0, 1.0};
const IntegrationRule kIntegrationRules[] = {
    {kNodes16, kWeights16, 16}, {kNodes8, kWeights8, 8},
    {kNodes4, kWeights4, 4}, {kNodes2, kWeights2, 2},
};

bool integerIn(const QJsonValue& value, int minimum, int maximum) {
    return finiteNumber(value) && std::floor(value.toDouble()) == value.toDouble()
        && value.toDouble() >= minimum && value.toDouble() <= maximum;
}

QJsonValue indexedValue(const QJsonValue& values, int index, int arrayOffset = 0) {
    if (values.isObject()) return values.toObject().value(QString::number(index));
    if (values.isArray()) {
        const auto array = values.toArray();
        const int position = index - arrayOffset;
        if (position >= 0 && position < array.size()) return array.at(position);
    }
    return QJsonValue(QJsonValue::Undefined);
}

bool hasAudioSelector(const QJsonObject& config) {
    auto source = config.value(QStringLiteral("_ast")).toObject();
    if (automationType(source) != QStringLiteral("Audio")) source = config;
    for (const auto& field : {QStringLiteral("name"), QStringLiteral("id"), QStringLiteral("channel")}) {
        if (config.contains(field) || source.contains(field)) return true;
    }
    return false;
}

bool validAudioSelector(const QJsonObject& config) {
    auto source = config.value(QStringLiteral("_ast")).toObject();
    if (automationType(source) != QStringLiteral("Audio")) source = config;
    for (const auto& field : {QStringLiteral("name"), QStringLiteral("id"), QStringLiteral("channel")}) {
        if (source.contains(field) && !config.contains(field)) return false;
    }
    for (const auto& field : {QStringLiteral("name"), QStringLiteral("id")}) {
        if (config.contains(field) && (!config.value(field).isString() || config.value(field).toString().isEmpty())) return false;
    }
    if (config.contains(QStringLiteral("id")) && !config.contains(QStringLiteral("name"))) return false;
    return integerIn(config.value(QStringLiteral("channel")), 1, 32);
}

class AutomationEvaluator {
public:
    AutomationEvaluator(const QJsonObject& midiState, const QJsonObject& audioState)
        : midiState_(midiState), audioState_(audioState) {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        wallTime_ = std::chrono::duration<double, std::milli>(now).count();
    }

    double evaluate(const QJsonObject& config, double normalizedTime, const QJsonObject& range,
                    int depth = 0) const {
        if (automationType(config).isEmpty() || depth > kMaxAutomationDepth) {
            return scaleAutomationValue(0.0, range);
        }

        double value = 0.0;
        const QString type = automationType(config);
        if (type == QStringLiteral("Oscillator")) {
            value = evaluateOscillator(config, normalizedTime, depth);
        } else if (type == QStringLiteral("Midi")) {
            const double minimum = resolveField(config.value(QStringLiteral("min")), normalizedTime,
                                                kUnitRange, depth, 0.0);
            const double maximum = resolveField(config.value(QStringLiteral("max")), normalizedTime,
                                                kUnitRange, depth, 1.0);
            const double sensitivity = resolveField(config.value(QStringLiteral("sensitivity")), normalizedTime,
                                                    kMidiSensitivityRange, depth, 1.0);
            value = evaluateMidi(config, selectedMidiState(config), minimum, maximum, sensitivity);
        } else if (config.value(QStringLiteral("_invalid")).toBool(false)) {
            value = finiteNumber(config.value(QStringLiteral("min")))
                ? config.value(QStringLiteral("min")).toDouble()
                : 0.0;
        } else {
            const double minimum = resolveField(config.value(QStringLiteral("min")), normalizedTime,
                                                kUnitRange, depth, 0.0);
            const double maximum = resolveField(config.value(QStringLiteral("max")), normalizedTime,
                                                kUnitRange, depth, 1.0);
            value = evaluateAudio(config, selectedAudioState(config), minimum, maximum);
        }
        return scaleAutomationValue(value, range);
    }

private:
    const QJsonObject& midiState_;
    const QJsonObject& audioState_;
    double wallTime_ = 0.0;

    double resolveField(const QJsonValue& value, double normalizedTime, const QJsonObject& range,
                        int depth, double fallback) const {
        if (isAutomationValue(value)) return evaluate(value.toObject(), normalizedTime, range, depth + 1);
        return finiteNumber(value) ? value.toDouble() : fallback;
    }

    static bool hasDynamicFields(const QJsonObject& config) {
        const QString type = automationType(config);
        if (type == QStringLiteral("Midi")) {
            return isAutomationValue(config.value(QStringLiteral("min")))
                || isAutomationValue(config.value(QStringLiteral("max")))
                || isAutomationValue(config.value(QStringLiteral("sensitivity")));
        }
        return isAutomationValue(config.value(QStringLiteral("min")))
            || isAutomationValue(config.value(QStringLiteral("max")));
    }

    static double oscPrimitive(int type, double x) {
        const double whole = std::floor(x);
        const double fraction = x - whole;
        if (type == 0) return x * 0.5 - std::sin(x * kTau) / (2.0 * kTau);
        if (type == 1) {
            const double partial = fraction < 0.5
                ? fraction * fraction
                : 2.0 * fraction - fraction * fraction - 0.5;
            return whole * 0.5 + partial;
        }
        if (type == 2) return whole * 0.5 + fraction * fraction * 0.5;
        if (type == 3) return x - (whole * 0.5 + fraction * fraction * 0.5);
        if (type == 4) return whole * 0.5 + std::max(0.0, fraction - 0.5);
        return 0.0;
    }

    static bool canIntegrateExactly(const QJsonObject& config) {
        const int type = config.value(QStringLiteral("oscType")).toInt(-1);
        if (type < 0 || type > 4) return false;
        for (const QString& field : {QStringLiteral("min"), QStringLiteral("max"), QStringLiteral("speed"),
                                     QStringLiteral("offset"), QStringLiteral("seed")}) {
            if (!finiteNumber(config.value(field))) return false;
        }
        return true;
    }

    double integrateSimpleOscillator(const QJsonObject& config, double normalizedTime) const {
        const int type = config.value(QStringLiteral("oscType")).toInt();
        const double minimum = config.value(QStringLiteral("min")).toDouble();
        const double maximum = config.value(QStringLiteral("max")).toDouble();
        const double speed = config.value(QStringLiteral("speed")).toDouble();
        const double offset = config.value(QStringLiteral("offset")).toDouble();
        if (speed == 0.0) return evaluateOscillator(config, 0.0, 0) * normalizedTime;
        const double rawIntegral = (oscPrimitive(type, offset + speed * normalizedTime)
                                    - oscPrimitive(type, offset)) / speed;
        return minimum * normalizedTime + (maximum - minimum) * rawIntegral;
    }

    double integrate(const QJsonObject& config, double normalizedTime, const QJsonObject& range,
                     int depth) const {
        double integral = 0.0;
        const QString type = automationType(config);
        if (type == QStringLiteral("Oscillator") && canIntegrateExactly(config)) {
            integral = integrateSimpleOscillator(config, normalizedTime);
        } else if ((type == QStringLiteral("Midi") || type == QStringLiteral("Audio"))
                   && !hasDynamicFields(config)) {
            integral = evaluate(config, normalizedTime, {}, depth + 1) * normalizedTime;
        } else {
            const IntegrationRule& rule = kIntegrationRules[std::min(depth, 3)];
            const double midpoint = normalizedTime * 0.5;
            const double halfWidth = normalizedTime * 0.5;
            double sum = 0.0;
            for (int i = 0; i < rule.size; ++i) {
                sum += rule.weights[i]
                    * evaluate(config, midpoint + halfWidth * rule.nodes[i], {}, depth + 1);
            }
            integral = halfWidth * sum;
        }
        const QJsonValue minimum = range.value(QStringLiteral("min"));
        const QJsonValue maximum = range.value(QStringLiteral("max"));
        if (!finiteNumber(minimum) || !finiteNumber(maximum)) return integral;
        return minimum.toDouble() * normalizedTime
            + integral * (maximum.toDouble() - minimum.toDouble());
    }

    double evaluateOscillator(const QJsonObject& config, double normalizedTime, int depth) const {
        const double minimum = resolveField(config.value(QStringLiteral("min")), normalizedTime,
                                            kUnitRange, depth, 0.0);
        const double maximum = resolveField(config.value(QStringLiteral("max")), normalizedTime,
                                            kUnitRange, depth, 1.0);
        const double offset = resolveField(config.value(QStringLiteral("offset")), normalizedTime,
                                           kOscillatorOffsetRange, depth, 0.0);
        const double seed = resolveField(config.value(QStringLiteral("seed")), normalizedTime,
                                         kOscillatorSeedRange, depth, 1.0);
        const QJsonValue speed = config.value(QStringLiteral("speed"));
        const double phase = isAutomationValue(speed)
            ? integrate(speed.toObject(), normalizedTime, kOscillatorSpeedRange, depth)
            : normalizedTime * (finiteNumber(speed) ? speed.toDouble() : 1.0);
        const double time = phase + offset;
        double raw = 0.0;
        switch (config.value(QStringLiteral("oscType")).toInt(-1)) {
            case 0: raw = oscSine(time); break;
            case 1: raw = oscTriangle(time); break;
            case 2: raw = oscSaw(time); break;
            case 3: raw = oscSawInverse(time); break;
            case 4: raw = oscSquare(time); break;
            case 5: raw = oscNoise(time, seed); break;
            default: break;
        }
        return minimum + raw * (maximum - minimum);
    }

    QJsonObject selectedMidiState(const QJsonObject& config) const {
        const QString id = config.value(QStringLiteral("id")).toString();
        const QString name = config.value(QStringLiteral("name")).toString();
        if (id.isEmpty() && name.isEmpty()) return midiState_;
        const QJsonObject ports = midiState_.value(QStringLiteral("ports")).toObject();
        if (!id.isEmpty()) {
            const QJsonObject entry = ports.value(id).toObject();
            if (entry.isEmpty() || !entry.value(QStringLiteral("connected")).toBool(true)) return {};
            const QJsonObject state = entry.value(QStringLiteral("state")).toObject();
            return state.isEmpty() ? entry : state;
        }
        QJsonObject match;
        for (const QJsonValue& value : ports) {
            const QJsonObject entry = value.toObject();
            if (entry.value(QStringLiteral("connected")).toBool(true)
                && entry.value(QStringLiteral("name")).toString() == name) {
                if (!match.isEmpty()) return {};
                match = entry;
            }
        }
        const QJsonObject state = match.value(QStringLiteral("state")).toObject();
        return state.isEmpty() ? match : state;
    }

    static QJsonObject zoneVoice(const QJsonObject& state, const QJsonObject& config) {
        QJsonObject newest;
        auto consider = [&](const QJsonObject& voice) {
            if (!voice.isEmpty() && (newest.isEmpty() || voice.value(QStringLiteral("order")).toDouble()
                > newest.value(QStringLiteral("order")).toDouble())) newest = voice;
        };
        if (state.contains(QStringLiteral("ports"))) {
            consider(zoneVoice(state.value(QStringLiteral("unscopedState")).toObject(), config));
            for (const auto& value : state.value(QStringLiteral("ports")).toObject()) {
                const auto entry = value.toObject();
                if (!entry.value(QStringLiteral("connected")).toBool(true)) continue;
                const auto nested = entry.value(QStringLiteral("state")).toObject();
                consider(zoneVoice(nested.isEmpty() ? entry : nested, config));
            }
            return newest;
        }
        const bool upper = config.value(QStringLiteral("zone")).toInt() == 1;
        const auto zones = state.value(QStringLiteral("mpeZones")).toObject();
        const int count = config.contains(QStringLiteral("members")) ? config.value(QStringLiteral("members")).toInt()
            : zones.value(upper ? QStringLiteral("upper") : QStringLiteral("lower")).toInt(15);
        if (count < 1 || count > 15) return {};
        const int first = upper ? 16 - count : 2;
        const int last = upper ? 15 : 1 + count;
        for (int number = first; number <= last; ++number) {
            const auto channel = indexedValue(state.value(QStringLiteral("channels")), number, 1).toObject();
            const auto held = channel.value(QStringLiteral("heldNotes"));
            QJsonArray notes;
            if (held.isArray()) notes = held.toArray();
            else for (const auto& note : held.toObject()) notes.append(note);
            for (const auto& value : notes) {
                auto note = value.toObject();
                if (note.isEmpty()) continue;
                note.insert(QStringLiteral("channel"), channel);
                consider(note);
            }
        }
        return newest;
    }

    double evaluateMidi(const QJsonObject& config, const QJsonObject& state,
                        double minimum, double maximum, double sensitivity) const {
        if (config.value(QStringLiteral("_invalid")).toBool(false) || state.isEmpty()) return minimum;
        const int mode = config.value(QStringLiteral("mode")).toInt(4);
        const bool hasZone = config.contains(QStringLiteral("zone"));
        if (hasZone && (config.contains(QStringLiteral("channel")) || !integerIn(config.value(QStringLiteral("zone")), 0, 1))) return minimum;
        if (config.contains(QStringLiteral("members")) && (!hasZone || !integerIn(config.value(QStringLiteral("members")), 1, 15))) return minimum;
        if (!hasZone && mode >= 5 && !integerIn(config.value(QStringLiteral("channel")), 1, 16)) return minimum;
        QJsonObject channel;
        QJsonObject note;
        if (hasZone) {
            note = zoneVoice(state, config);
            if (note.isEmpty()) return minimum;
            channel = note.value(QStringLiteral("channel")).toObject();
            note.insert(QStringLiteral("gate"), 1);
        } else {
            const int number = config.value(QStringLiteral("channel")).toInt(1);
            channel = indexedValue(state.value(QStringLiteral("channels")), number, 1).toObject();
            if (channel.isEmpty()) channel = indexedValue(state.value(QStringLiteral("channels")), 1, 1).toObject();
            note = channel;
        }
        if (channel.isEmpty()) return minimum;
        const double gate = note.value(QStringLiteral("gate")).toDouble();
        double raw = 0.0;
        double divisor = 127.0;
        switch (mode) {
            case 0: raw = note.value(QStringLiteral("key")).toDouble(); break;
            case 1: if (gate == 1.0) raw = note.value(QStringLiteral("key")).toDouble(); break;
            case 2: if (gate == 1.0) raw = note.value(QStringLiteral("velocity")).toDouble(); break;
            case 3:
            case 4:
            default:
                if (gate == 1.0) {
                    raw = note.value(mode == 3 ? QStringLiteral("key") : QStringLiteral("velocity")).toDouble();
                    const double decay = std::min(1.0,
                        (wallTime_ - note.value(QStringLiteral("time")).toDouble()) * sensitivity * 0.001);
                    raw *= 1.0 - decay;
                }
                break;
            case 5:
            case 6: {
                const auto cc = config.contains(QStringLiteral("cc")) ? config.value(QStringLiteral("cc")) : QJsonValue(1);
                if (!integerIn(cc, 0, mode == 6 ? 31 : 127)) return minimum;
                raw = indexedValue(channel.value(mode == 6 ? QStringLiteral("cc14") : QStringLiteral("cc")), cc.toInt()).toDouble();
                divisor = mode == 6 ? 16383.0 : 127.0;
                break;
            }
            case 7:
                if (!integerIn(config.value(QStringLiteral("nrpn")), 0, 16382)) return minimum;
                raw = indexedValue(channel.value(QStringLiteral("nrpn")), config.value(QStringLiteral("nrpn" )).toInt()).toDouble();
                divisor = 16383.0;
                break;
            case 8: raw = channel.value(QStringLiteral("pitchBend")).toDouble(8192); divisor = 16383.0; break;
            case 9: raw = channel.value(QStringLiteral("pressure")).toDouble(); break;
            case 10: raw = indexedValue(channel.value(QStringLiteral("polyPressure")), note.value(QStringLiteral("key")).toInt()).toDouble(); break;
        }
        return minimum + raw / divisor * (maximum - minimum);
    }

    QJsonObject selectedAudioState(const QJsonObject& config) const {
        if (!hasAudioSelector(config)) return audioState_;
        if (!validAudioSelector(config)) return {};
        const int channel = config.value(QStringLiteral("channel")).toInt();
        const QString id = config.value(QStringLiteral("id")).toString();
        const QString name = config.value(QStringLiteral("name")).toString();
        if (name.isEmpty() && id.isEmpty()) {
            return indexedValue(audioState_.value(QStringLiteral("defaultChannels")), channel, 1).toObject();
        }
        const QJsonObject devices = audioState_.value(QStringLiteral("devices")).toObject();
        QJsonObject entry;
        if (!id.isEmpty()) {
            entry = devices.value(id).toObject();
        } else if (!name.isEmpty()) {
            for (const QJsonValue& value : devices) {
                const QJsonObject candidate = value.toObject();
                if (candidate.value(QStringLiteral("connected")).toBool(true)
                    && candidate.value(QStringLiteral("name")).toString() == name) {
                    if (!entry.isEmpty()) return {};
                    entry = candidate;
                }
            }
        }
        if (entry.isEmpty() || !entry.value(QStringLiteral("connected")).toBool(true)) return {};
        const QJsonValue channelsValue = entry.value(QStringLiteral("channels"));
        if (channelsValue.isObject()) return channelsValue.toObject().value(QString::number(channel)).toObject();
        if (channelsValue.isArray()) {
            const QJsonArray channels = channelsValue.toArray();
            return channel <= channels.size() ? channels.at(channel - 1).toObject() : QJsonObject();
        }
        return {};
    }

    static double evaluateAudio(const QJsonObject& config, const QJsonObject& state,
                                double minimum, double maximum) {
        if (state.isEmpty()) return minimum;
        double raw = 0.0;
        switch (config.value(QStringLiteral("band")).toInt(-1)) {
            case 0: raw = state.value(QStringLiteral("low")).toDouble(); break;
            case 1: raw = state.value(QStringLiteral("mid")).toDouble(); break;
            case 2: raw = state.value(QStringLiteral("high")).toDouble(); break;
            case 3: raw = state.value(QStringLiteral("vol")).toDouble(); break;
            case 4:
                if (!state.value(QStringLiteral("rawReady")).toBool(false)
                    && !state.value(QStringLiteral("raw_ready")).toBool(false)) return minimum;
                raw = (std::clamp(state.value(QStringLiteral("raw")).toDouble(), -1.0, 1.0) + 1.0) * 0.5;
                break;
            default: break;
        }
        raw = std::clamp(raw, 0.0, 1.0);
        return minimum + raw * (maximum - minimum);
    }
};

void visitAudioRequirements(const QJsonValue& value, QJsonObject& result,
                            QMap<QString, int>& selectedKeys, int depth = 0) {
    if (depth > 64) return;
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (automationType(object) == QStringLiteral("Audio")) {
            const bool selectorIntent = hasAudioSelector(object);
            const QJsonValue bandValue = object.value(QStringLiteral("band"));
            const bool validBand = !object.value(QStringLiteral("_invalid")).toBool(false)
                && finiteNumber(bandValue) && std::floor(bandValue.toDouble()) == bandValue.toDouble()
                && bandValue.toInt() >= 0 && bandValue.toInt() <= 4;
            if (!validBand) return;
            visitAudioRequirements(object.value(QStringLiteral("min")), result, selectedKeys, depth + 1);
            visitAudioRequirements(object.value(QStringLiteral("max")), result, selectedKeys, depth + 1);

            const QString name = object.value(QStringLiteral("name")).toString();
            const QJsonValue channelValue = object.value(QStringLiteral("channel"));
            if (validAudioSelector(object)) {
                const QString id = object.value(QStringLiteral("id")).toString();
                QJsonObject requirement;
                requirement.insert(QStringLiteral("id"), id.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(id));
                requirement.insert(QStringLiteral("name"), name.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(name));
                requirement.insert(QStringLiteral("channel"), channelValue);
                requirement.insert(QStringLiteral("needsRaw"), bandValue.toInt() == 4);
                const QString key = id + QChar(0x1f) + name + QChar(0x1f) + QString::number(channelValue.toInt());
                QJsonArray selected = result.value(QStringLiteral("selected")).toArray();
                if (selectedKeys.contains(key)) {
                    const int index = selectedKeys.value(key);
                    if (requirement.value(QStringLiteral("needsRaw")).toBool()) {
                        QJsonObject existing = selected.at(index).toObject();
                        existing.insert(QStringLiteral("needsRaw"), true);
                        selected.replace(index, existing);
                    }
                } else {
                    selectedKeys.insert(key, selected.size());
                    selected.append(requirement);
                }
                result.insert(QStringLiteral("selected"), selected);
            } else if (!selectorIntent) {
                result.insert(QStringLiteral("needsLegacy"), true);
                if (bandValue.toInt() == 4) result.insert(QStringLiteral("needsLegacyRaw"), true);
            }
            return;
        }
        for (auto it = object.begin(); it != object.end(); ++it) {
            visitAudioRequirements(it.value(), result, selectedKeys, depth + 1);
        }
    } else if (value.isArray()) {
        for (const QJsonValue& item : value.toArray()) {
            visitAudioRequirements(item, result, selectedKeys, depth + 1);
        }
    }
}

} // namespace

Backend::Backend() = default;

void Backend::setMidiState(const QJsonObject& state) {
    m_midiState = state;
}

void Backend::setAudioState(const QJsonObject& state) {
    m_audioState = state;
}

QJsonValue Backend::resolveUniformValue(const QJsonValue& value, double normalizedTime,
                                        const QJsonObject& paramSpec) const {
    if (!isAutomationValue(value)) return value;
    return AutomationEvaluator(m_midiState, m_audioState).evaluate(value.toObject(), normalizedTime, paramSpec);
}

QJsonObject Backend::getAudioInputRequirements(const Graph& graph) const {
    QJsonObject result{
        {QStringLiteral("needsLegacy"), false},
        {QStringLiteral("needsLegacyRaw"), false},
        {QStringLiteral("selected"), QJsonArray{}},
    };
    const QString dataRoot = m_dataRoot.isEmpty() ? QStringLiteral("qt/noisemaker") : m_dataRoot;
    QMap<QString, int> selectedKeys;
    for (const Pass& pass : graph.passes) {
        if (!pass.effectNamespace.isEmpty() && !pass.func.isEmpty()) {
            QFile file(QStringLiteral("%1/effects/%2/%3.json").arg(dataRoot, pass.effectNamespace, pass.func));
            if (file.open(QIODevice::ReadOnly)) {
                const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
                if (document.isObject()) {
                    for (const QJsonValue& tag : document.object().value(QStringLiteral("tags")).toArray()) {
                        if (tag.toString() == QStringLiteral("audio")) {
                            result.insert(QStringLiteral("needsLegacy"), true);
                            break;
                        }
                    }
                }
            }
        }
        visitAudioRequirements(pass.uniforms, result, selectedKeys);
    }
    return result;
}

// See backend.h for the full contract (idempotent; caller must already have
// a valid context current). Extracted from the destructor's owned-context
// branch (T7 fix round 2) so an external-context host (examples/viewer) can
// run the exact same GPU cleanup while ITS context is still alive, instead
// of only on Backend destruction with a context Backend itself owns.
void Backend::releaseGl() {
    if (!m_gl) return; // nothing set up, or already released

    try {
        m_sinkManager.close();
    } catch (...) {
    }
    for (const auto& weakQueue : m_frameExportQueues) {
        if (const auto queue = weakQueue.lock()) {
            try {
                queue->close();
            } catch (...) {
            }
        }
    }
    m_frameExportQueues.clear();

    if (m_surfaces) {
        m_surfaces->releaseAll();
        m_surfaces.reset();
    }
    for (auto it = m_programs.begin(); it != m_programs.end(); ++it) {
        if (it->hasUbo) {
            m_gl->glDeleteBuffers(1, &it->uboBuffer);
        }
        m_gl->glDeleteProgram(it->handle);
    }
    m_programs.clear();
    m_maxTextureSize = 0;
    m_maxColorBytesPerSample = 0;
    m_warnedVolumeClamp = false;
    m_warnedMrtDemotion = false;
    if (m_fullscreenVao) {
        m_gl->glDeleteVertexArrays(1, &m_fullscreenVao);
        m_fullscreenVao = 0;
    }
    if (m_fullscreenVbo) {
        m_gl->glDeleteBuffers(1, &m_fullscreenVbo);
        m_fullscreenVbo = 0;
    }
    if (m_emptyVao) {
        m_gl->glDeleteVertexArrays(1, &m_emptyVao);
        m_emptyVao = 0;
    }

    delete m_gl;
    m_gl = nullptr;
}

Backend::~Backend() {
    // Owned-context path (nm-render's offscreen mode): behavior here is
    // PARITY-CRITICAL and unchanged from before releaseGl() existed --
    // same makeCurrent/cleanup/doneCurrent sequence, same GL calls, same
    // order. releaseGl() now does the actual deletion (previously inlined
    // here) and, as one of its own postconditions, deletes and nulls
    // `m_gl` itself; the unconditional `delete m_gl` below is consequently
    // a safe no-op for this path (delete-on-nullptr), not a double-delete.
    //
    // External-context path (examples/viewer and similar hosts): stays
    // NO-GL here, exactly as before -- `m_ownedContext` is null so the
    // branch below never runs. A host that wants its GPU objects freed
    // before the (externally-owned) context goes away must call
    // releaseGl() itself while that context is still current -- see
    // Viewer's QOpenGLContext::aboutToBeDestroyed hook.
    if (m_ownedContext && m_gl) {
        if (m_ownedContext->makeCurrent(m_ownedSurface)) {
            releaseGl();
            m_ownedContext->doneCurrent();
        }
    }
    delete m_gl;
    delete m_ownedSurface;
    delete m_ownedContext;
}

void Backend::setup(QOpenGLContext* context, const QString& dataRoot, QSize size) {
    m_dataRoot = dataRoot;
    m_size = size;

    if (context) {
        m_context = context;
    } else {
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setProfile(QSurfaceFormat::CoreProfile);
        format.setVersion(4, 1);

        m_ownedContext = new QOpenGLContext();
        m_ownedContext->setFormat(format);
        if (!m_ownedContext->create()) {
            throw std::runtime_error("nm::Backend::setup: failed to create OpenGL 4.1 core context");
        }

        m_ownedSurface = new QOffscreenSurface();
        m_ownedSurface->setFormat(m_ownedContext->format());
        m_ownedSurface->create();
        if (!m_ownedSurface->isValid()) {
            throw std::runtime_error("nm::Backend::setup: failed to create QOffscreenSurface");
        }

        if (!m_ownedContext->makeCurrent(m_ownedSurface)) {
            throw std::runtime_error("nm::Backend::setup: QOpenGLContext::makeCurrent() failed");
        }
        m_context = m_ownedContext;
    }

    // Qt6 moved the versioned OpenGL function wrappers to the QtOpenGL
    // module and dropped QOpenGLContext::versionFunctions<T>() (a Qt5 API);
    // the current pattern is to own the wrapper directly and initialize it
    // against whichever context is current.
    m_gl = new QOpenGLFunctions_4_1_Core();
    if (!m_gl->initializeOpenGLFunctions()) {
        throw std::runtime_error("nm::Backend::setup: failed to resolve OpenGL 4.1 core functions");
    }

    GLint maxTextureUnits = m_maxTextureUnits;
    m_gl->glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxTextureUnits);
    m_maxTextureUnits = maxTextureUnits;
    GLint maxTextureSize = 0;
    m_gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    m_maxTextureSize = maxTextureSize;
    m_maxColorBytesPerSample = probeColorBytesPerSample();

    // PORTING-GUIDE.md GL state parity rules: WebGL2 has vertex-shader
    // point size always enabled; desktop GL defaults it off. Enabled once
    // for the context's lifetime -- no non-agent pass writes gl_PointSize,
    // so this is inert for every other pass, matching the reference's own
    // constant (never toggled) behavior.
    m_gl->glEnable(GL_PROGRAM_POINT_SIZE);

    m_surfaces = std::make_unique<SurfaceCache>(m_gl);
    createFullscreenVao();
    createEmptyVao();
    if (m_sinkManager.closed()) m_sinkManager = SinkManager();
    OutputDescriptor descriptor;
    descriptor.width = size.width();
    descriptor.height = size.height();
    m_sinkManager.configure(descriptor);
}

int Backend::probeColorBytesPerSample() {
    struct ProbeCombination {
        int bytes;
        std::vector<unsigned int> formats;
    };
    const std::vector<ProbeCombination> combinations = {
        {64, {GL_RGBA32F, GL_RGBA32F, GL_RGBA32F, GL_RGBA32F}},
        {48, {GL_RGBA32F, GL_RGBA32F, GL_RGBA32F}},
        {40, {GL_RGBA32F, GL_RGBA32F, GL_RGBA16F}},
        {32, {GL_RGBA32F, GL_RGBA16F, GL_RGBA16F}},
    };

    GLint maxDrawBuffers = 0;
    GLint previousReadFramebuffer = 0;
    GLint previousDrawFramebuffer = 0;
    GLint previousTexture = 0;
    m_gl->glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);
    m_gl->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    m_gl->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
    m_gl->glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);

    unsigned int framebuffer = 0;
    m_gl->glGenFramebuffers(1, &framebuffer);
    m_gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    int budget = 16;
    for (const ProbeCombination& combination : combinations) {
        if (static_cast<int>(combination.formats.size()) > maxDrawBuffers) continue;

        std::vector<unsigned int> textures(combination.formats.size(), 0);
        std::vector<unsigned int> drawBuffers;
        drawBuffers.reserve(combination.formats.size());
        m_gl->glGenTextures(static_cast<int>(textures.size()), textures.data());
        for (int i = 0; i < static_cast<int>(textures.size()); ++i) {
            const unsigned int format = combination.formats.at(i);
            m_gl->glBindTexture(GL_TEXTURE_2D, textures.at(i));
            m_gl->glTexImage2D(
                GL_TEXTURE_2D,
                0,
                static_cast<int>(format),
                2,
                2,
                0,
                GL_RGBA,
                format == GL_RGBA32F ? GL_FLOAT : GL_HALF_FLOAT,
                nullptr);
            m_gl->glFramebufferTexture2D(
                GL_DRAW_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0 + static_cast<unsigned int>(i),
                GL_TEXTURE_2D,
                textures.at(i),
                0);
            drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + static_cast<unsigned int>(i));
        }
        m_gl->glDrawBuffers(static_cast<int>(drawBuffers.size()), drawBuffers.data());
        const bool complete = m_gl->glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        for (int i = 0; i < static_cast<int>(textures.size()); ++i) {
            m_gl->glFramebufferTexture2D(
                GL_DRAW_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0 + static_cast<unsigned int>(i),
                GL_TEXTURE_2D,
                0,
                0);
        }
        m_gl->glDeleteTextures(static_cast<int>(textures.size()), textures.data());
        if (complete) {
            budget = combination.bytes;
            break;
        }
    }
    m_gl->glDeleteFramebuffers(1, &framebuffer);
    m_gl->glBindFramebuffer(
        GL_READ_FRAMEBUFFER,
        static_cast<unsigned int>(previousReadFramebuffer));
    m_gl->glBindFramebuffer(
        GL_DRAW_FRAMEBUFFER,
        static_cast<unsigned int>(previousDrawFramebuffer));
    m_gl->glBindTexture(GL_TEXTURE_2D, static_cast<unsigned int>(previousTexture));
    while (m_gl->glGetError() != GL_NO_ERROR) {
    }
    return budget;
}

bool Backend::applyMrtFormatBudgets(Graph& graph) {
    if (m_maxColorBytesPerSample <= 0) return false;
    bool changed = false;
    for (const Pass& pass : graph.passes) {
        if (pass.outputs.size() <= 1) continue;
        const CompiledProgram& program = programFor(pass);
        QHash<QString, int> outputLocations;
        for (auto it = pass.outputs.begin(); it != pass.outputs.end(); ++it) {
            outputLocations.insert(
                it.key(),
                fragmentOutputLocation(m_gl, program.handle, it.key()));
        }
        changed = detail::applyMrtFormatBudget(
            graph,
            pass,
            outputLocations,
            m_maxColorBytesPerSample) || changed;
    }
    return changed;
}

void Backend::createFullscreenVao() {
    // FULLSCREEN_TRIANGLE_POSITIONS (default-shaders.js): a single
    // oversized triangle covering NDC, not a quad.
    static const GLfloat positions[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};

    m_gl->glGenBuffers(1, &m_fullscreenVbo);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_fullscreenVbo);
    m_gl->glBufferData(GL_ARRAY_BUFFER, sizeof(positions), positions, GL_STATIC_DRAW);

    m_gl->glGenVertexArrays(1, &m_fullscreenVao);
    m_gl->glBindVertexArray(m_fullscreenVao);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    m_gl->glBindVertexArray(0);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Backend::createEmptyVao() {
    // reference webgl2.js `emptyVAO`: agent points/billboards vertex
    // shaders derive everything from gl_VertexID via texelFetch, with NO
    // bound vertex buffer or attributes (`gl.bindVertexArray(emptyVAO);
    // gl.drawArrays(gl.POINTS, 0, count)`). Desktop GL core profile still
    // requires *some* VAO bound before any draw call (unlike WebGL2, which
    // silently tolerates none); a VAO with zero enabled attribute arrays
    // and no bound buffer is the exact structural equivalent.
    m_gl->glGenVertexArrays(1, &m_emptyVao);
}

QByteArray Backend::loadEffectSource(const Pass& pass) const {
    const QString path = QStringLiteral("%1/shaders/effects/%2/%3/%4.frag")
        .arg(m_dataRoot, pass.effectNamespace, pass.func, pass.progName);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error(
            QStringLiteral("nm::Backend: failed to open shader file '%1'").arg(path).toStdString());
    }
    return file.readAll();
}

QByteArray Backend::loadVertexSource(const Pass& pass) const {
    // Agent deposit passes carry a custom vertex stage (stage-named pair,
    // T2 fix): shaders/effects/<ns>/<func>/<prog>.vert, alongside the
    // <prog>.frag every pass has. Godot's `_load_vertex` is the structural
    // twin of this.
    const QString path = QStringLiteral("%1/shaders/effects/%2/%3/%4.vert")
        .arg(m_dataRoot, pass.effectNamespace, pass.func, pass.progName);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error(
            QStringLiteral("nm::Backend: failed to open vertex shader file '%1'").arg(path).toStdString());
    }
    return file.readAll();
}

const Backend::CompiledProgram& Backend::programFor(const Pass& pass) {
    const QString key = programCacheKey(pass);
    auto cached = m_programs.find(key);
    if (cached != m_programs.end()) {
        return cached.value();
    }

    const bool isAgentPass = isAgentDrawMode(pass.drawMode);

    const QByteArray fragRaw = isBlit(pass) ? QByteArray(kBlitFragmentSource) : loadEffectSource(pass);
    // Agent points/billboards passes compile their own corpus vertex shader
    // (gl_VertexID-driven scatter, textureFetch of agent state); every
    // other pass keeps the shared default vertex shader (backend-owned
    // constant, mirrors reference default-shaders.js DEFAULT_VERTEX_SHADER).
    const QByteArray vertRaw = isAgentPass ? loadVertexSource(pass) : QByteArray(kDefaultVertexSource);

    const bool needsPolyfill = !isBlit(pass) && shaderNeedsPackHalfPolyfill(QString::fromUtf8(fragRaw));
    const bool vertNeedsPolyfill = isAgentPass && shaderNeedsPackHalfPolyfill(QString::fromUtf8(vertRaw));

    const QByteArray fragAssembled = assembleShader(QString::fromUtf8(fragRaw), pass.defines, needsPolyfill);
    // godot `_load_vertex`/`_inject_after_version`: the SAME defines block
    // as the fragment shader is injected into an agent pass's vertex
    // shader too (harmless no-op for a vertex shader with no matching
    // #ifdef, and future-proofs a define-gated vertex shader).
    const QByteArray vertAssembled =
        assembleShader(QString::fromUtf8(vertRaw), isAgentPass ? pass.defines : QJsonObject(), vertNeedsPolyfill);

    GLuint vertShader = m_gl->glCreateShader(GL_VERTEX_SHADER);
    {
        const char* src = vertAssembled.constData();
        const GLint len = vertAssembled.size();
        m_gl->glShaderSource(vertShader, 1, &src, &len);
        m_gl->glCompileShader(vertShader);
        GLint ok = GL_FALSE;
        m_gl->glGetShaderiv(vertShader, GL_COMPILE_STATUS, &ok);
        if (ok != GL_TRUE) {
            GLint logLen = 0;
            m_gl->glGetShaderiv(vertShader, GL_INFO_LOG_LENGTH, &logLen);
            QByteArray log(logLen, Qt::Uninitialized);
            m_gl->glGetShaderInfoLog(vertShader, logLen, nullptr, log.data());
            m_gl->glDeleteShader(vertShader);
            throw std::runtime_error(
                ("nm::Backend: vertex shader compile failed for '" + pass.program + "': " + log).toStdString());
        }
    }

    GLuint fragShader = m_gl->glCreateShader(GL_FRAGMENT_SHADER);
    {
        const char* src = fragAssembled.constData();
        const GLint len = fragAssembled.size();
        m_gl->glShaderSource(fragShader, 1, &src, &len);
        m_gl->glCompileShader(fragShader);
        GLint ok = GL_FALSE;
        m_gl->glGetShaderiv(fragShader, GL_COMPILE_STATUS, &ok);
        if (ok != GL_TRUE) {
            GLint logLen = 0;
            m_gl->glGetShaderiv(fragShader, GL_INFO_LOG_LENGTH, &logLen);
            QByteArray log(logLen, Qt::Uninitialized);
            m_gl->glGetShaderInfoLog(fragShader, logLen, nullptr, log.data());
            m_gl->glDeleteShader(vertShader);
            m_gl->glDeleteShader(fragShader);
            throw std::runtime_error(
                ("nm::Backend: fragment shader compile failed for '" + pass.program + "': " + log).toStdString());
        }
    }

    GLuint program = m_gl->glCreateProgram();
    m_gl->glAttachShader(program, vertShader);
    m_gl->glAttachShader(program, fragShader);
    m_gl->glBindAttribLocation(program, 0, "a_position");
    m_gl->glLinkProgram(program);

    GLint linked = GL_FALSE;
    m_gl->glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint logLen = 0;
        m_gl->glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
        QByteArray log(logLen, Qt::Uninitialized);
        m_gl->glGetProgramInfoLog(program, logLen, nullptr, log.data());
        m_gl->glDeleteShader(vertShader);
        m_gl->glDeleteShader(fragShader);
        m_gl->glDeleteProgram(program);
        throw std::runtime_error(
            ("nm::Backend: program link failed for '" + pass.program + "': " + log).toStdString());
    }

    m_gl->glDeleteShader(vertShader);
    m_gl->glDeleteShader(fragShader);

    CompiledProgram compiled;
    compiled.handle = program;

    GLint uniformCount = 0;
    m_gl->glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &uniformCount);
    for (GLint i = 0; i < uniformCount; ++i) {
        char nameBuf[256];
        GLsizei nameLen = 0;
        GLint size = 0;
        GLenum type = 0;
        m_gl->glGetActiveUniform(program, static_cast<GLuint>(i), sizeof(nameBuf), &nameLen, &size, &type, nameBuf);
        const QString name = QString::fromLatin1(nameBuf, nameLen);
        const GLint location = m_gl->glGetUniformLocation(program, nameBuf);

        compiled.uniformLocations.insert(name, location);
        compiled.uniformTypes.insert(name, type);

        // Reference extractUniforms(): array uniforms ("foo[0]") are also
        // registered without the "[0]" suffix.
        if (name.endsWith(QStringLiteral("[0]"))) {
            const QString baseName = name.left(name.size() - 3);
            compiled.uniformLocations.insert(baseName, location);
            compiled.uniformTypes.insert(baseName, type);
        }
    }

    // std140 UBO detection (ARCHITECTURE.md/PORTING-GUIDE.md: `synth/remap`
    // is kept as the reference wrote it -- a real UBO, glUniformBlockBinding
    // -- and is the ONLY corpus user; verified by grepping every shader in
    // qt/noisemaker/shaders/effects for `layout(std140)`). Detected
    // generically off GL_ACTIVE_UNIFORM_BLOCKS rather than hardcoding the
    // effect name, matching this file's other generic-detection precedent
    // (shaderNeedsPackHalfPolyfill). The block's byte size comes straight
    // from the driver's own std140 layout of the shader's own `vec4
    // data[N]` declaration -- remap.frag declares its exact array size
    // itself, so there is no synthesized-layout size math to duplicate
    // here (contrast godot's `getPackedUniformLayoutSize`, needed there
    // because godot SYNTHESIZES the UBO declaration for every effect).
    GLint blockCount = 0;
    m_gl->glGetProgramiv(program, GL_ACTIVE_UNIFORM_BLOCKS, &blockCount);
    if (blockCount > 0) {
        GLint blockSize = 0;
        m_gl->glGetActiveUniformBlockiv(program, 0, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
        unsigned int ubo = 0;
        m_gl->glGenBuffers(1, &ubo);
        m_gl->glBindBuffer(GL_UNIFORM_BUFFER, ubo);
        m_gl->glBufferData(GL_UNIFORM_BUFFER, blockSize, nullptr, GL_DYNAMIC_DRAW);
        m_gl->glBindBuffer(GL_UNIFORM_BUFFER, 0);
        m_gl->glUniformBlockBinding(program, 0, kUboBindingPoint);
        compiled.hasUbo = true;
        compiled.uboBuffer = ubo;
        compiled.uboBlockSize = blockSize;
    }

    return m_programs.insert(key, compiled).value();
}

QJsonObject Backend::engineUniforms() const {
    // reference/04 §10.1 updateGlobalUniforms (shaders/src/runtime/pipeline.js
    // ~L1385-1427): `aspect` and `aspectRatio` are BOTH set, to the SAME
    // value, every frame — `g.aspect = g.aspectRatio = aspectValue` (or
    // `= fullAspect` under tile-region export, which T3 does not implement;
    // `_tileOffset`/`_fullResolution` are always null in our scope, so the
    // two branches collapse to the same `width/height` formula here).
    // FIX ROUND 1: `aspect` was missing entirely. Nine fullscreen-pass
    // fragment shaders in this corpus declare `uniform float aspect;`
    // (distinct from `aspectRatio`; verified via `grep -rl "uniform float
    // aspect;" qt/noisemaker/shaders/effects/`: synth/mandala, synth/osc2d,
    // synth/pattern, synth/perlin, synth/polygon (shape.frag),
    // synth/sacredGeometry, filter/repeat, filter/scale, filter/scroll —
    // plus render/meshRender/render.vert, out of T3's fullscreen-pass
    // scope). Leaving it unbound means GL's uniform default (0.0) reaches
    // those shaders instead of the real aspect ratio: `st.x *= aspect`
    // silently zeroes a coordinate (mandala: stripe/garbage output),
    // `st.x /= aspect` divides by zero (repeat/scale/scroll: Inf/NaN into
    // the rgba16f intermediate, then undefined behavior on the final
    // float->uint8 readback cast). See task report "Fix round 1" for the
    // before/after verification.
    //
    // Full engine-uniform set: time, resolution, tileOffset, fullResolution,
    // aspect, aspectRatio, renderScale. (deltaTime/frame/audio/midi are
    // pipeline bookkeeping or external-input state not read by any
    // fullscreen-effect-pass shader in this corpus and remain out of T3
    // scope.)
    QJsonObject globals;
    globals.insert(QStringLiteral("time"), m_time);
    globals.insert(QStringLiteral("resolution"), QJsonArray{m_size.width(), m_size.height()});
    globals.insert(QStringLiteral("tileOffset"), QJsonArray{0, 0});
    globals.insert(QStringLiteral("fullResolution"), QJsonArray{m_size.width(), m_size.height()});
    const double aspect = m_size.height() != 0 ? double(m_size.width()) / double(m_size.height()) : 1.0;
    globals.insert(QStringLiteral("aspect"), aspect);
    globals.insert(QStringLiteral("aspectRatio"), aspect);
    globals.insert(QStringLiteral("renderScale"), 1.0);
    return globals;
}

void Backend::setUniformValue(int location, unsigned int glType, const QJsonValue& value) {
    switch (glType) {
    case GL_FLOAT: {
        if (value.isArray()) {
            const QJsonArray arr = value.toArray();
            QVector<GLfloat> buf;
            buf.reserve(arr.size());
            for (const QJsonValue& v : arr) {
                buf.append(static_cast<GLfloat>(v.toDouble()));
            }
            m_gl->glUniform1fv(location, buf.size(), buf.constData());
        } else {
            m_gl->glUniform1f(location, static_cast<GLfloat>(value.toDouble()));
        }
        break;
    }
    case GL_INT:
    case GL_BOOL: {
        const int iv = value.isBool() ? (value.toBool() ? 1 : 0) : static_cast<int>(value.toDouble());
        m_gl->glUniform1i(location, iv);
        break;
    }
    case GL_FLOAT_VEC2: {
        const QJsonArray arr = value.isArray() ? value.toArray() : QJsonArray{value, value};
        const GLfloat v[2] = {jsonArrayComponent(arr, 0, 0.0f), jsonArrayComponent(arr, 1, 0.0f)};
        m_gl->glUniform2fv(location, 1, v);
        break;
    }
    case GL_FLOAT_VEC3: {
        const QJsonArray arr = value.isArray() ? value.toArray() : QJsonArray{value, value, value};
        const GLfloat v[3] = {
            jsonArrayComponent(arr, 0, 0.0f), jsonArrayComponent(arr, 1, 0.0f), jsonArrayComponent(arr, 2, 0.0f)};
        m_gl->glUniform3fv(location, 1, v);
        break;
    }
    case GL_FLOAT_VEC4: {
        const QJsonArray arr = value.isArray() ? value.toArray() : QJsonArray{value, value, value, value};
        const GLfloat v[4] = {jsonArrayComponent(arr, 0, 0.0f), jsonArrayComponent(arr, 1, 0.0f),
                               jsonArrayComponent(arr, 2, 0.0f), jsonArrayComponent(arr, 3, 1.0f)};
        m_gl->glUniform4fv(location, 1, v);
        break;
    }
    case GL_FLOAT_MAT3: {
        const QJsonArray arr = value.toArray();
        GLfloat m[9];
        for (int i = 0; i < 9; ++i) {
            m[i] = jsonArrayComponent(arr, i, 0.0f);
        }
        m_gl->glUniformMatrix3fv(location, 1, GL_FALSE, m);
        break;
    }
    case GL_FLOAT_MAT4: {
        const QJsonArray arr = value.toArray();
        GLfloat m[16];
        for (int i = 0; i < 16; ++i) {
            m[i] = jsonArrayComponent(arr, i, 0.0f);
        }
        m_gl->glUniformMatrix4fv(location, 1, GL_FALSE, m);
        break;
    }
    default:
        // Sampler types are bound in bindTextures(); other types (INT_VECn,
        // UINT*, BOOL_VECn, MAT2, non-square mats) are silently dropped,
        // matching the reference _setUniform's switch (reference/05
        // §8.2: "Types NOT handled ... value is silently dropped").
        break;
    }
}

void Backend::bindUniforms(const CompiledProgram& program, const Pass& pass) {
    const QJsonObject globals = engineUniforms();

    // Pass uniforms first (DSL/effect defaults), then globals -- but skip
    // any global name already present in pass.uniforms. Pass uniforms win
    // (reference/05 §8.1 "Order & precedence").
    for (auto it = pass.uniforms.begin(); it != pass.uniforms.end(); ++it) {
        if (!program.uniformLocations.contains(it.key())) {
            continue;
        }
        if (it.value().isNull() || it.value().isUndefined()) {
            continue;
        }
        const QJsonObject paramSpec = pass.uniformSpecs.value(it.key()).toObject();
        const QJsonValue value = resolveUniformValue(it.value(), m_time, paramSpec);
        setUniformValue(program.uniformLocations.value(it.key()), program.uniformTypes.value(it.key()), value);
    }
    for (auto it = globals.begin(); it != globals.end(); ++it) {
        if (pass.uniforms.contains(it.key())) {
            continue;
        }
        if (!program.uniformLocations.contains(it.key())) {
            continue;
        }
        setUniformValue(program.uniformLocations.value(it.key()), program.uniformTypes.value(it.key()), it.value());
    }
}

QJsonObject Backend::loadEffectUniformLayout(const QString& ns, const QString& func) {
    const QString key = ns + QLatin1Char('/') + func;
    const auto cached = m_uniformLayoutCache.constFind(key);
    if (cached != m_uniformLayoutCache.constEnd()) {
        return cached.value();
    }

    // Read straight from the local effect JSON, not the compiled graph's
    // own `programs` bookkeeping object -- confirmed empirically (minting
    // synth/remap and inspecting the real exported graph) that `programs`
    // holds only the `blit` cache-traceability entry in practice, matching
    // this file's existing graph.h note and godot/TD's own choice to load
    // uniformLayout from their local effect JSON copy for the identical
    // reason (nm_backend.gd `_load_effect_def`; td_backend.py
    // `_effect_uniform_layout`).
    QJsonObject layout;
    QFile file(QStringLiteral("%1/effects/%2/%3.json").arg(m_dataRoot, ns, func));
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isObject()) {
            layout = doc.object().value(QStringLiteral("uniformLayout")).toObject();
        }
    }
    m_uniformLayoutCache.insert(key, layout);
    return layout;
}

void Backend::bindUniformBlock(const CompiledProgram& program, const Pass& pass) {
    if (!program.hasUbo) {
        return;
    }

    const QJsonObject layout = loadEffectUniformLayout(pass.effectNamespace, pass.func);
    if (layout.isEmpty()) {
        // A program that linked a real UBO but whose effect JSON carries no
        // uniformLayout would leave the block entirely zeroed; not hit by
        // any corpus program (remap is the sole UBO user and always has
        // one), kept as a defensive no-op rather than a hard failure.
        return;
    }

    // merged = pass uniforms (win) union engine globals (fallback) -- the
    // SAME precedence as bindUniforms() / reference bindUniformBlocks
    // (webgl2.js ~L1424-1495): pass.uniforms values win, globalUniforms
    // (time/resolution/...) fill in anything the pass didn't carry.
    QJsonObject merged = pass.uniforms;
    const QJsonObject globals = engineUniforms();
    for (auto it = globals.begin(); it != globals.end(); ++it) {
        if (!merged.contains(it.key())) {
            merged.insert(it.key(), it.value());
        }
    }

    // reference packUniformsWithLayout / godot pack_with_layout: each
    // layout entry is {slot, components}; components is a 1-4 character
    // run of x/y/z/w, each mapped to its OWN offset within the 16-byte
    // (vec4) slot -- ported per-character (not "assume contiguous from
    // components[0]") so a hypothetical non-contiguous component string
    // would still pack correctly, matching godot's `_comp_offsets`.
    QByteArray data(program.uboBlockSize, 0);
    for (auto it = layout.begin(); it != layout.end(); ++it) {
        const QJsonObject entry = it.value().toObject();
        const int slot = entry.value(QStringLiteral("slot")).toInt();
        const QString components = entry.value(QStringLiteral("components")).toString();
        const int slotOffset = slot * 16;
        if (slotOffset < 0 || slotOffset + 16 > data.size() || components.isEmpty()) {
            continue;
        }

        const QJsonValue value = resolveUniformValue(
            merged.value(it.key()), m_time, pass.uniformSpecs.value(it.key()).toObject());
        if (value.isUndefined() || value.isNull()) {
            continue; // absent -> leaves the zero-initialized slot bytes, matching the reference's implicit 0.0
        }

        QVector<float> values;
        if (value.isArray()) {
            for (const QJsonValue& v : value.toArray()) {
                values.append(static_cast<float>(v.toDouble()));
            }
        } else if (value.isBool()) {
            values.append(value.toBool() ? 1.0f : 0.0f);
        } else {
            values.append(static_cast<float>(value.toDouble()));
        }

        const int n = std::min(components.size(), values.size());
        for (int i = 0; i < n; ++i) {
            int componentOffset = 0;
            const QChar c = components.at(i);
            if (c == QLatin1Char('y')) componentOffset = 4;
            else if (c == QLatin1Char('z')) componentOffset = 8;
            else if (c == QLatin1Char('w')) componentOffset = 12;
            const float f = values.at(i);
            std::memcpy(data.data() + slotOffset + componentOffset, &f, sizeof(float));
        }
    }

    m_gl->glBindBuffer(GL_UNIFORM_BUFFER, program.uboBuffer);
    m_gl->glBufferSubData(GL_UNIFORM_BUFFER, 0, data.size(), data.constData());
    m_gl->glBindBufferBase(GL_UNIFORM_BUFFER, kUboBindingPoint, program.uboBuffer);
    m_gl->glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void Backend::bindTextures(const Graph& graph, const CompiledProgram& program, const Pass& pass) {
    // Two phases, deliberately not interleaved. Phase 1 resolves every
    // input's GL texture handle (which can LAZILY CREATE a not-yet-seen
    // surface -- a hazard surface's "before" buffer, read for the first
    // time). SurfaceCache::createSurface() does its own
    // glBindTexture(GL_TEXTURE_2D, newTex) / ... / glBindTexture(...,0)
    // dance while setting up a freshly-created texture, on WHATEVER
    // GL_ACTIVE_TEXTURE unit happens to be current at that moment. If that
    // ran interleaved with this function's OWN glActiveTexture(unit N) +
    // glBindTexture(handle) calls for an EARLIER sampler in the SAME pass
    // (the original single-pass-loop shape), the creation's closing
    // `glBindTexture(...,0)` unbinds whatever this function had just bound
    // at that same still-active unit -- silently leaving an EARLIER
    // sampler's unit pointing at texture 0 by the time the draw call runs.
    // Root-caused via direct GPU-state tracing on physarum's pointsEmit/
    // init pass: inputTex (unit 0, bound first) sampled as an incomplete/
    // unbound texture (each driver's own fallback value) because rgbaTex's
    // first-time creation (unit 1, resolved second) ran its cleanup
    // glBindTexture(...,0) while unit 0 was still GL's active unit,
    // clobbering inputTex's binding well before the draw. Resolving every
    // handle first, with NO glActiveTexture/glBindTexture calls of our own
    // interleaved, then binding all of them in a second, creation-free
    // pass, makes this ordering hazard structurally impossible.
    struct ResolvedInput {
        QString samplerName;
        unsigned int handle;
    };
    QVector<ResolvedInput> resolved;
    resolved.reserve(pass.inputs.size());
    // Iterated in QJsonObject's own (sorted-by-key) order rather than the
    // graph JSON's original textual/insertion order (see shader_assembly.h
    // and graph.h's notes on this Qt build's QJsonObject behavior). This is
    // deliberate, not an oversight: texture-unit *numbers* are an
    // implementation detail — each sampler uniform is told its own assigned
    // unit explicitly below (`glUniform1i(loc, unit)`), so any consistent
    // permutation of which samplerName gets unit 0 vs. unit 1 etc. produces
    // the same final bindings and identical rendered output.
    for (auto it = pass.inputs.begin(); it != pass.inputs.end(); ++it) {
        const QString& texId = it.value().toString();
        unsigned int handle = 0;
        if (!texId.isEmpty() && texId != QStringLiteral("none")) {
            handle = resolveInputSurface(graph, texId).texture;
        }
        if (handle == 0) {
            // Reference bindTextures(): a missing/uninitialized input
            // silently binds the 1x1 transparent-black default texture
            // rather than leaving the unit unbound or warning.
            handle = m_surfaces->defaultTextureHandle();
        }
        resolved.push_back({it.key(), handle});
    }

    if (resolved.size() > m_maxTextureUnits) {
        // reference/05 §8.3 bindTextures / webgl2.js ~L1364-1370:
        // ERR_TOO_MANY_TEXTURES.
        throw std::runtime_error(QStringLiteral(
            "nm::Backend: pass '%1' binds more textures than the GL implementation "
            "supports (limit %2)")
                                      .arg(pass.id)
                                      .arg(m_maxTextureUnits)
                                      .toStdString());
    }

    // Phase 2: bind every already-resolved handle to its own unit. No
    // surface creation happens in this loop, so nothing can clobber an
    // earlier iteration's binding.
    for (int unit = 0; unit < resolved.size(); ++unit) {
        const ResolvedInput& input = resolved.at(unit);
        m_gl->glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        m_gl->glBindTexture(GL_TEXTURE_2D, input.handle);
        if (program.uniformLocations.contains(input.samplerName)) {
            m_gl->glUniform1i(program.uniformLocations.value(input.samplerName), unit);
        }
    }
}

GpuSurface& Backend::resolveInputSurface(const Graph& graph, const QString& texId) {
    if (texId.startsWith(QStringLiteral("global_"))) {
        const QString bare = texId.mid(7);
        if (m_pingpong.isHazard(bare)) {
            const QString physicalKey = m_pingpong.physicalRead(bare);
            return m_surfaces->getAliased(physicalKey, graph, texId, m_size, m_mergedUniforms);
        }
    }
    return m_surfaces->get(graph, texId, m_size, m_mergedUniforms);
}

GpuSurface& Backend::resolveOutputSurface(const Graph& graph, const QString& texId) {
    if (texId.startsWith(QStringLiteral("global_"))) {
        const QString bare = texId.mid(7);
        if (m_pingpong.isHazard(bare)) {
            const QString physicalKey = m_pingpong.physicalWrite(bare);
            return m_surfaces->getAliased(physicalKey, graph, texId, m_size, m_mergedUniforms);
        }
    }
    return m_surfaces->get(graph, texId, m_size, m_mergedUniforms);
}

int Backend::resolveRepeatCount(const Pass& pass) const {
    // reference/04 §10.5 resolveRepeatCount / godot `_repeat_count`: no
    // repeat -> 1; a number -> max(1,floor); a string -> the value of the
    // SAME-named pass uniform if numeric (verified against a real minted
    // navierStokes graph: every compiled pass of an effect carries the
    // effect's FULL resolved global-param set, including "iterations" on
    // the pressure pass whose own effect-definition template lists no
    // `uniforms` mapping at all -- so this is always a same-pass lookup in
    // practice, never a cross-pass merge).
    if (pass.repeat.isDouble()) {
        return std::max(1, static_cast<int>(std::floor(pass.repeat.toDouble())));
    }
    if (pass.repeat.isString()) {
        const auto it = pass.uniforms.constFind(pass.repeat.toString());
        if (it != pass.uniforms.constEnd()) {
            const QJsonValue value = resolveUniformValue(
                it.value(), m_time, pass.uniformSpecs.value(it.key()).toObject());
            if (value.isDouble()) {
                return std::max(1, static_cast<int>(std::floor(value.toDouble())));
            }
        }
    }
    return 1;
}

int Backend::resolvePointCount(const Graph& graph, const Pass& pass) {
    // reference webgl2.js executePass points/billboards branch: a literal
    // number passes straight through; "auto"/"screen"/"input" derive the
    // count from a reference texture's dimensions (agentStateSamplerPriority
    // documents the sampler-name priority: xyzTex for 2D agents, stateTex1
    // for filter3d/flow3d's volume agents, inputTex as a last resort).
    if (pass.count.isDouble()) {
        return std::max(0, static_cast<int>(pass.count.toDouble()));
    }

    const QString mode = pass.count.isString() ? pass.count.toString() : QString();
    if (mode == QStringLiteral("auto") || mode == QStringLiteral("screen") || mode == QStringLiteral("input")) {
        QString stateTexId;
        if (mode == QStringLiteral("input")) {
            for (const QString& sampler : agentStateSamplerPriority()) {
                const QString candidate = pass.inputs.value(sampler).toString();
                if (!candidate.isEmpty() && candidate != QStringLiteral("none")) {
                    stateTexId = candidate;
                    break;
                }
            }
            if (stateTexId.isEmpty()) {
                for (auto it = pass.inputs.begin(); it != pass.inputs.end(); ++it) {
                    const QString candidate = it.value().toString();
                    if (!candidate.isEmpty() && candidate != QStringLiteral("none")) {
                        stateTexId = candidate;
                        break;
                    }
                }
            }
        }
        if (!stateTexId.isEmpty()) {
            const GpuSurface& surface = resolveInputSurface(graph, stateTexId);
            if (surface.width > 0 && surface.height > 0) {
                return surface.width * surface.height;
            }
        }
        return m_size.width() * m_size.height();
    }

    return 1000; // reference `effectivePass.count || 1000`; not hit by any in-corpus fixture
}

void Backend::executePass(const Graph& graph, const Pass& pass) {
    // PORTING-GUIDE.md rule 4 "Fail loud, never approximate": drawMode
    // "triangles" (mesh rendering -- render/meshRender.json is the one
    // corpus user) is real, reachable DSL surface with no implementation
    // here. Left unguarded, it would silently fall through to the
    // fullscreen-triangle default below and mis-render (a mesh shader
    // expects per-vertex mesh attributes, not a fullscreen sweep) instead
    // of failing where the gap actually is. `points`/`billboards` are
    // implemented (isAgentDrawMode); absent/empty is the ordinary
    // fullscreen-effect case; anything else -- "triangles" today, whatever
    // comes next -- is unsupported and must say so.
    if (!pass.drawMode.isEmpty() && !isAgentDrawMode(pass.drawMode)) {
        throw std::runtime_error(QStringLiteral("nm::Backend: pass '%1' has unsupported drawMode '%2' "
                                                  "(only \"points\"/\"billboards\" agent draws and the "
                                                  "fullscreen-triangle default are implemented)")
                                      .arg(pass.id, pass.drawMode)
                                      .toStdString());
    }

    const CompiledProgram& program = programFor(pass);
    m_gl->glUseProgram(program.handle);

    if (pass.outputs.isEmpty()) {
        throw std::runtime_error(("nm::Backend: pass '" + pass.id + "' declares no output").toStdString());
    }

    const int declaredDrawBuffers = pass.drawBuffers.isDouble() ? static_cast<int>(pass.drawBuffers.toDouble()) : 0;
    const bool isMrt = pass.outputs.size() > 1 || declaredDrawBuffers > 1;

    unsigned int fboToUse = 0;
    // Viewport dimensions are captured BY VALUE, immediately after each
    // resolveOutputSurface() call -- NOT held as a GpuSurface&/* across
    // further SurfaceCache insertions. SurfaceCache::m_surfaces is a
    // QHash<QString, GpuSurface>; a later insert() (the MRT loop's 2nd/3rd
    // iteration, or the ping-pong-physical-surface allocations they
    // trigger) can rehash the table and invalidate a reference obtained
    // from an EARLIER insert() call.
    int viewportWidth = 0;
    int viewportHeight = 0;

    if (isMrt) {
        // MRT: "outputs" keys are USUALLY the shader's fragment OUT-variable
        // names (docs/GRAPH-JSON-SCHEMA.md) -- each corpus MRT shader
        // assigns its own `layout(location=N)` per out variable, so the
        // attachment index is queried from the linked program by name
        // (glGetFragDataLocation) rather than assumed from JSON key
        // iteration order (reference/05 MRT semantics; webgl2.js
        // executePass resolves this per-output too, just via its own
        // resolvedOutputIds bookkeeping -- notably NOT via a GL name
        // lookup at all: the reference has no `gl.getFragDataLocation`
        // equivalent in its MRT path, it assigns attachments purely by
        // `Object.keys(outputs)` iteration position, trusting the
        // compiler's own key order to match each shader's declared
        // `layout(location=N)` order. This port instead resolves by NAME,
        // which sidesteps needing to preserve JSON key order at all
        // (QJsonObject doesn't -- see graph.h's note) PROVIDED the graph
        // key always equals the GLSL variable name.
        //
        // FIX (final fix wave, item 2/3 investigation): that proviso holds
        // for every points/agent-family MRT shader in the corpus
        // (outXYZ/outVel/outRGBA/outData/outState1-3 -- graph key ==
        // GLSL name, verified corpus-wide), but NOT for the render3d
        // family: every synth3d `precompute.frag` and every
        // render3d/renderCubemap3d/renderLit3d/renderCubemapSurface.frag
        // declares its primary output as `layout(location=0) out vec4
        // fragColor;`, while the compiled graph's key for that exact slot
        // is `"color"` (matching the single-output branch's own "prefer
        // the color key" convention below, and docs/GRAPH-JSON-SCHEMA.md's
        // own outputs example, which lists "color" and "fragColor" as
        // separate possible names without noting they're the same role
        // under different effect families). A name-based lookup for
        // "color" against a program that only declares "fragColor" returns
        // -1, silently dropping that attachment -- found by direct GPU-
        // state readback: `node_0_volumeCache` (the actual density field)
        // stayed all-zero while the sibling `geoOut` attachment (whose key
        // DOES match its GLSL name) populated correctly, and every
        // downstream synth3d/render3d/renderCubemap3d fixture rendered
        // degenerate output as a result -- not a boolean-define bug, a
        // separate, pre-existing MRT name-resolution gap this investigation
        // surfaced. Fixed with a narrow, corpus-verified fallback rather
        // than switching to positional resolution (which would require
        // preserving true JSON key order through QJsonObject, a much
        // larger change for a corpus where exactly one alias pair exists).
        QMap<int, unsigned int> attachments;
        bool haveViewport = false;
        for (auto it = pass.outputs.begin(); it != pass.outputs.end(); ++it) {
            const QString texId = it.value().toString();
            GpuSurface& surface = resolveOutputSurface(graph, texId);
            const int location = fragmentOutputLocation(m_gl, program.handle, it.key());
            const unsigned int textureHandle = surface.texture;
            const int surfaceWidth = surface.width;
            const int surfaceHeight = surface.height;
            if (location < 0) {
                continue; // shader doesn't declare this out variable under either name
            }
            attachments.insert(location, textureHandle);
            if (!haveViewport) {
                viewportWidth = surfaceWidth;
                viewportHeight = surfaceHeight;
                haveViewport = true;
            }
        }
        if (attachments.isEmpty()) {
            throw std::runtime_error(("nm::Backend: MRT pass '" + pass.id + "' resolved no attachments").toStdString());
        }
        fboToUse = m_surfaces->mrtFramebuffer(attachments);
    } else {
        // Single output, preferring the "color" key when present else the
        // first (only) value (reference executePass:
        // `outputs?.color || Object.values(outputs||{})[0]`).
        QString outputId;
        if (pass.outputs.contains(QStringLiteral("color"))) {
            outputId = pass.outputs.value(QStringLiteral("color")).toString();
        } else {
            outputId = pass.outputs.begin().value().toString();
        }
        GpuSurface& surface = resolveOutputSurface(graph, outputId);
        fboToUse = surface.fbo;
        viewportWidth = surface.width;
        viewportHeight = surface.height;
    }

    // Resolve every INPUT surface (bindTextures) and, for an agent pass,
    // its live agent count (resolvePointCount, which itself reads an input
    // surface's dimensions) BEFORE binding the draw target FBO. Any of
    // these can be the FIRST-EVER reference to a given physical surface
    // (a hazard surface's "before" buffer, read for the first time on the
    // very first frame that touches it -- never true in the T3/T4
    // fullscreen-chain-only scope, where every input was necessarily an
    // EARLIER pass's already-created output, but routine once ping-pong
    // hazard surfaces exist), and SurfaceCache::createSurface() binds its
    // OWN fbo/clears/unbinds to 0 as a side effect of lazily creating a
    // new texture -- which would silently steal the GL_FRAMEBUFFER binding
    // out from under this pass's draw target if that happened AFTER we'd
    // already bound it. Root-caused via GPU-state tracing on physarum:
    // fboToUse was a valid, fully-attached MRT FBO, but
    // GL_FRAMEBUFFER_BINDING at draw time was still 0 -- bindTextures()'s
    // lazy creation of a hazard surface's read-side buffer, called AFTER
    // the (then-)explicit bind, had rebound 0 on its way out. Binding the
    // draw target LAST, after every surface a pass could possibly
    // reference now exists, makes that reordering impossible to
    // reintroduce by accident.
    //
    // resolvePointCount() is resolved BEFORE bindTextures() for the same
    // family of reason, one level more subtle: it can ALSO lazily create a
    // surface (an agent pass's own state-texture dimension lookup), and
    // that creation's internal texture-unit bind/unbind (see bindTextures()
    // doc) must not run AFTER bindTextures() has already done its real,
    // final per-unit sampler binding for the draw -- ordering it first
    // guarantees bindTextures() is the last thing to touch any texture
    // unit before the draw call.
    const bool isAgentPass = isAgentDrawMode(pass.drawMode);
    const int agentCount = isAgentPass ? resolvePointCount(graph, pass) : 0;
    bindTextures(graph, program, pass);

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, fboToUse);
    m_gl->glViewport(0, 0, viewportWidth, viewportHeight);

    bindUniforms(program, pass);
    bindUniformBlock(program, pass);

    // PORTING-GUIDE.md GL state parity rules / reference executePass
    // "Handle Blending": additive deposits declare `blend` (true, or an
    // explicit [src,dst] factor-name pair); everything else is replace
    // (blend disabled) -- independent of drawMode, matching the reference
    // exactly (not gated on isAgentDrawMode()).
    if (pass.blend.isBool() ? pass.blend.toBool() : pass.blend.isArray()) {
        m_gl->glEnable(GL_BLEND);
        GLenum srcFactor = GL_ONE;
        GLenum dstFactor = GL_ONE;
        if (pass.blend.isArray()) {
            const QJsonArray factors = pass.blend.toArray();
            if (factors.size() == 2) {
                srcFactor = resolveBlendFactor(factors.at(0).toString());
                dstFactor = resolveBlendFactor(factors.at(1).toString());
            }
        }
        m_gl->glBlendFunc(srcFactor, dstFactor);
    } else {
        m_gl->glDisable(GL_BLEND);
    }

    if (isAgentPass) {
        // Deposits: no clear (accumulate onto whatever the preceding copy
        // pass established), procedural draw with no bound vertex buffer
        // -- gl_VertexID alone drives the vertex shader's texelFetch scatter.
        const int vertexCount = agentVertexCount(pass.drawMode, agentCount);
        const bool billboards = pass.drawMode == QStringLiteral("billboards");
        m_gl->glBindVertexArray(m_emptyVao);
        m_gl->glDrawArrays(billboards ? GL_TRIANGLES : GL_POINTS, 0, vertexCount);
        m_gl->glBindVertexArray(0);
    } else {
        m_gl->glBindVertexArray(m_fullscreenVao);
        m_gl->glDrawArrays(GL_TRIANGLES, 0, 3);
        m_gl->glBindVertexArray(0);
    }

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_gl->glUseProgram(0);
    m_gl->glDisable(GL_BLEND);
}

void Backend::render(const Graph& graph, double t) {
    renderInternal(graph, t, std::nullopt);
}

void Backend::render(const Graph& graph, double t, double presentationTimestamp) {
    renderInternal(graph, t, presentationTimestamp);
}

void Backend::renderInternal(
    const Graph& graph,
    double t,
    std::optional<double> presentationTimestamp) {
    Graph effectiveGraph = graph;
    if (detail::clampGraphVolumeSizes(effectiveGraph, m_maxTextureSize)
        && !m_warnedVolumeClamp) {
        qWarning().noquote()
            << QStringLiteral("nm::Backend: clamped volumeSize to fit GL_MAX_TEXTURE_SIZE %1")
                   .arg(m_maxTextureSize);
        m_warnedVolumeClamp = true;
    }
    if (applyMrtFormatBudgets(effectiveGraph) && !m_warnedMrtDemotion) {
        qWarning().noquote()
            << QStringLiteral("nm::Backend: demoted trailing MRT attachments to fit the device's %1-byte color budget")
                   .arg(m_maxColorBytesPerSample);
        m_warnedMrtDemotion = true;
    }

    m_time = t;
    m_currentRenderSurface = effectiveGraph.renderSurface;
    m_mergedUniforms = mergeAllPassUniforms(effectiveGraph);
    m_pingpong.syncGraph(effectiveGraph);
    m_pingpong.beginFrame();

    for (const Pass& pass : effectiveGraph.passes) {
        const int repeatCount = resolveRepeatCount(pass);
        for (int iter = 0; iter < repeatCount; ++iter) {
            executePass(effectiveGraph, pass);
            m_pingpong.updateFrameBindings(pass);
            if (repeatCount > 1) {
                // reference §10.6 / godot: adopt EVERY iteration, not only
                // the last (a preceding non-repeat pass earlier in the same
                // frame may have left the persistent record stale -- see
                // pingpong.h adoptIterationBindings doc).
                m_pingpong.adoptIterationBindings(pass);
            }
        }
    }

    m_pingpong.endFrame();
    m_gl->glFlush();
    if (const GpuSurface* surface = currentRenderSurface()) {
        if (!presentationTimestamp) {
            const auto now = std::chrono::steady_clock::now().time_since_epoch();
            presentationTimestamp =
                std::chrono::duration<double, std::milli>(now).count();
        }
        m_sinkManager.submit(*surface, *presentationTimestamp);
    }
}

std::function<void()> Backend::addSink(const std::shared_ptr<OutputSink>& sink) {
    return m_sinkManager.add(sink);
}

void Backend::removeSink(OutputSink* sink) {
    m_sinkManager.remove(sink);
}

SinkStats Backend::sinkStats(const OutputSink* sink) const {
    return m_sinkManager.statsFor(sink);
}

std::shared_ptr<FrameExportQueue> Backend::createFrameExportQueue(
    FrameExportOptions options) {
    if (!m_gl) {
        throw std::runtime_error(
            "nm::Backend::createFrameExportQueue: setup must be called first");
    }
    auto queue = std::make_shared<FrameExportQueue>(m_gl, std::move(options));
    m_frameExportQueues.push_back(queue);
    return queue;
}

QString Backend::currentRenderSurfaceId() const {
    if (m_currentRenderSurface.isEmpty()) return QString();
    if (m_pingpong.isHazard(m_currentRenderSurface)) {
        return m_pingpong.physicalRead(m_currentRenderSurface);
    }
    return QStringLiteral("global_") + m_currentRenderSurface;
}

const GpuSurface* Backend::currentRenderSurface() const {
    const QString texId = currentRenderSurfaceId();
    return texId.isEmpty() || !m_surfaces ? nullptr : m_surfaces->find(texId);
}

QImage Backend::readSurface() const {
    if (m_currentRenderSurface.isEmpty()) {
        throw std::runtime_error("nm::Backend::readSurface: graph has no renderSurface");
    }
    // Ping-pong aware: if the render surface itself is a hazard surface,
    // read whatever this frame's LAST write bound as its read target (see
    // pingpong.h -- endFrame() deliberately leaves this queryable). The
    // common case (a render surface written once, never read in-graph) is
    // never a hazard, so this falls through to the original flat lookup
    // unchanged -- zero behavior change for Tier-1.
    const GpuSurface* surface = currentRenderSurface();
    if (!surface) {
        const QString texId = currentRenderSurfaceId();
        throw std::runtime_error(
            ("nm::Backend::readSurface: surface '" + texId + "' was never written").toStdString());
    }

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, surface->fbo);
    // PORTING-GUIDE.md GL state parity rules: PACK_ALIGNMENT 1 before
    // readback (row padding otherwise corrupts non-multiple-of-4 widths).
    m_gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);

    QVector<float> buffer(surface->width * surface->height * 4);
    m_gl->glReadPixels(0, 0, surface->width, surface->height, GL_RGBA, GL_FLOAT, buffer.data());
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);

    QImage image(surface->width, surface->height, QImage::Format_RGBA8888);
    for (int y = 0; y < surface->height; ++y) {
        // glReadPixels is bottom-up; flip to top-down once here, matching
        // the reference's own PNG capture exactly (PORTING-GUIDE.md: "a
        // second flip or a gamma pass shows up as instant whole-image FAIL").
        const int srcRow = surface->height - 1 - y;
        const float* src = buffer.constData() + static_cast<qsizetype>(srcRow) * surface->width * 4;
        uchar* dst = image.scanLine(y);
        for (int x = 0; x < surface->width; ++x) {
            for (int c = 0; c < 4; ++c) {
                double v = std::round(static_cast<double>(src[x * 4 + c]) * 255.0);
                v = std::clamp(v, 0.0, 255.0);
                dst[x * 4 + c] = static_cast<uchar>(v);
            }
        }
    }
    return image;
}

} // namespace nm
