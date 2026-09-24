#include "host_inputs.h"

#include "compiler/dsl_compiler.h"
#include "compiler/effect_registry.h"
#include "compiler/lexer.h"
#include "compiler/parser.h"
#include "compiler/validator.h"
#include "runtime/audio_state.h"
#include "runtime/backend.h"
#include "runtime/midi_state.h"
#include "runtime/png_io.h"
#include "runtime/text_texture.h"

#include <QFile>
#include <QGuiApplication>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSize>
#include <QStringList>
#include <QSurfaceFormat>

#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>

namespace {

constexpr double kLoopSeconds = 10.0; // reference host loop duration
constexpr double kFrameSeconds = 1.0 / 60.0;

const char* const kUsage =
    "usage: noisemaker-qt-export [program.dsl] [output.png] [options]\n"
    "\n"
    "  program.dsl      DSL program to render (default: this project's program.dsl)\n"
    "  output.png       PNG to write (default: output.png)\n"
    "  --size WxH       output size in pixels (default 1024x1024)\n"
    "  --time T         loop time of the output frame, 0 <= T < 1 of a 10 s loop (default 0.5)\n"
    "  --frames N       frames to render, 1/60 s apart, ending at --time (default 8)\n"
    "  --media FILE     image for every media() step (PNG, JPEG, BMP, GIF, ...)\n"
    "  --audio FILE     WAV file for scope(), spectrum() and audio(); without it they see silence\n"
    "  --midi FILE      Standard MIDI File for roll() and midi(); without it no notes are held\n"
    "  --help           show this text\n";

struct Options {
    QString program = QStringLiteral(NM_PROGRAM_PATH);
    QString output = QStringLiteral("output.png");
    QSize size{1024, 1024};
    double time = 0.5;
    int frames = 8;
    QString media;
    QString audio;
    QString midi;
};

struct UsageError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] void usageError(const QString& message) {
    throw UsageError(message.toStdString());
}

Options parseOptions(const QStringList& args) {
    Options options;
    int positional = 0;
    for (int i = 1; i < args.size(); ++i) {
        const QString& arg = args.at(i);
        const auto value = [&]() {
            if (i + 1 >= args.size()) usageError(arg + QStringLiteral(" needs a value"));
            return args.at(++i);
        };
        if (arg == QStringLiteral("--size")) {
            static const QRegularExpression re(QStringLiteral("^(\\d+)x(\\d+)$"));
            const QRegularExpressionMatch m = re.match(value());
            options.size = m.hasMatch() ? QSize(m.captured(1).toInt(), m.captured(2).toInt()) : QSize();
            if (options.size.width() < 1 || options.size.height() < 1) usageError(QStringLiteral("--size must look like 1024x1024"));
        } else if (arg == QStringLiteral("--time")) {
            bool ok = false;
            options.time = value().toDouble(&ok);
            if (!ok || options.time < 0.0 || options.time >= 1.0) usageError(QStringLiteral("--time must be a number in [0, 1)"));
        } else if (arg == QStringLiteral("--frames")) {
            bool ok = false;
            options.frames = value().toInt(&ok);
            if (!ok || options.frames < 1) usageError(QStringLiteral("--frames must be a positive integer"));
        } else if (arg == QStringLiteral("--media")) {
            options.media = value();
        } else if (arg == QStringLiteral("--audio")) {
            options.audio = value();
        } else if (arg == QStringLiteral("--midi")) {
            options.midi = value();
        } else if (arg.startsWith(QStringLiteral("--"))) {
            usageError(QStringLiteral("unknown option ") + arg);
        } else if (positional == 0) {
            options.program = arg;
            ++positional;
        } else if (positional == 1) {
            options.output = arg;
            ++positional;
        } else {
            usageError(QStringLiteral("unexpected argument ") + arg);
        }
    }
    return options;
}

QByteArray readProgram(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(("cannot open DSL program '" + path + "'").toStdString());
    }
    return file.readAll();
}

// Validation errors, one per line with their source location, before
// compiling: the compiler's own exception names only the failed stage.
void reportDiagnostics(const QString& path, const QString& source, nm::EffectRegistry& registry) {
    const QJsonObject validated = nm::validate(nm::parse(nm::lex(source)), registry);
    int errors = 0;
    for (const QJsonValue& value : validated.value(QStringLiteral("diagnostics")).toArray()) {
        const QJsonObject diagnostic = value.toObject();
        if (diagnostic.value(QStringLiteral("severity")).toString() != QStringLiteral("error")) continue;
        const QJsonObject location = diagnostic.value(QStringLiteral("location")).toObject();
        const QString where = location.isEmpty()
            ? path
            : QStringLiteral("%1:%2:%3").arg(path).arg(location.value(QStringLiteral("line")).toInt())
                  .arg(location.value(QStringLiteral("column")).toInt());
        std::fprintf(stderr, "%s: error %s: %s\n", where.toUtf8().constData(),
                     diagnostic.value(QStringLiteral("code")).toString().toUtf8().constData(),
                     diagnostic.value(QStringLiteral("message")).toString().toUtf8().constData());
        ++errors;
    }
    if (errors) {
        throw std::runtime_error(QStringLiteral("'%1' has %2 error%3").arg(path).arg(errors)
                                     .arg(errors == 1 ? QString() : QStringLiteral("s")).toStdString());
    }
}

void note(const QString& text) {
    std::fprintf(stderr, "note: %s\n", text.toUtf8().constData());
}

// media(): the reference host uploads the image with flipY = false and sets
// the step's imageSize to the uploaded size.
void supplyMedia(nm::Backend& backend, nm::Graph& graph, const nm::EffectRegistry& registry,
                 const QString& path) {
    static const QRegularExpression mediaId(QStringLiteral("^imageTex_step_(\\d+)$"));
    QImage image;
    for (const QString& texId : nm::Backend::externalTextureIds(graph)) {
        const QRegularExpressionMatch m = mediaId.match(texId);
        if (!m.hasMatch()) continue;
        if (path.isEmpty()) {
            note(QStringLiteral("media() step %1 has no image and renders transparent; pass --media FILE")
                     .arg(m.captured(1)));
            continue;
        }
        if (image.isNull()) {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            image = reader.read();
            if (image.isNull()) {
                throw std::runtime_error(("cannot read image '" + path + "': " + reader.errorString()).toStdString());
            }
        }
        const QSize size = backend.updateTextureFromSource(texId, image, nm::ExternalTextureOptions{false});
        backend.applyStepParameterValues(graph, registry,
            QJsonObject{{QStringLiteral("step_") + m.captured(1),
                         QJsonObject{{QStringLiteral("imageSize"), QJsonArray{size.width(), size.height()}}}}});
    }
}

} // namespace

int main(int argc, char** argv) {
    QSurfaceFormat format;
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);

    QGuiApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--help"))) {
        std::printf("%s", kUsage);
        return 0;
    }
    const QString dataRoot = QStringLiteral(NM_DATA_ROOT);

    try {
        const Options options = parseOptions(app.arguments());

        nm::EffectRegistry registry;
        registry.loadAll(dataRoot);
        const QString source = QString::fromUtf8(readProgram(options.program));
        reportDiagnostics(options.program, source, registry);
        nm::Graph graph = nm::compileGraph(source, registry);

        nm::Backend backend;
        backend.setup(nullptr, dataRoot, options.size);
        supplyMedia(backend, graph, registry, options.media);
        nm::updateTextTextures(backend, graph, dataRoot, options.size);

        const QJsonObject audioNeeds = backend.getAudioInputRequirements(graph);
        const bool needsAudio = audioNeeds.value(QStringLiteral("needsLegacy")).toBool()
            || !audioNeeds.value(QStringLiteral("selected")).toArray().isEmpty();
        std::unique_ptr<nm::AudioInput> audioInput;
        std::optional<kit::AudioReplay> audio;
        if (!options.audio.isEmpty()) {
            audioInput = std::make_unique<nm::AudioInput>();
            audio.emplace(kit::readWav(options.audio));
        } else if (needsAudio) {
            // A connected, silent input: waveform 0.5, spectrum and bands 0.
            audioInput = std::make_unique<nm::AudioInput>();
            note(QStringLiteral("audio input is silent; pass --audio FILE.wav"));
        }
        std::unique_ptr<nm::MidiState> midiState;
        std::optional<kit::MidiReplay> midi;
        if (!options.midi.isEmpty()) {
            midiState = std::make_unique<nm::MidiState>();
            midi.emplace(kit::readMidiFile(options.midi));
        }

        // Frames 1/60 s apart, the last one at options.time of the loop.
        // Audio and MIDI files start with the loop; earlier frames wrap to
        // the end of the previous loop and hear nothing.
        const double endSeconds = options.time * kLoopSeconds;
        for (int frame = 0; frame < options.frames; ++frame) {
            const double back = (options.frames - 1 - frame) * kFrameSeconds;
            const double seconds = endSeconds - back;
            double t = options.time - back / kLoopSeconds;
            t -= std::floor(t);
            if (audioInput) {
                if (audio) audio->advanceTo(seconds, *audioInput);
                else audioInput->update();
                backend.setAudioState(audioInput->snapshot());
            }
            if (midi) {
                midi->advanceTo(seconds, *midiState);
                backend.setMidiState(midiState->snapshot());
            }
            backend.render(graph, t);
        }

        if (!nm::savePng(backend.readSurface(), options.output)) {
            throw std::runtime_error(("cannot write PNG '" + options.output + "'").toStdString());
        }
        std::printf("Rendered %s\n", options.output.toUtf8().constData());
    } catch (const UsageError& error) {
        std::fprintf(stderr, "ERROR: %s\n\n%s", error.what(), kUsage);
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "ERROR: %s\n", error.what());
        return 1;
    }
    return 0;
}
