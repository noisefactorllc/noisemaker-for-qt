#include "compiler/dsl_compiler.h"
#include "compiler/effect_registry.h"
#include "runtime/backend.h"
#include "runtime/png_io.h"

#include <QFile>
#include <QGuiApplication>
#include <QSize>
#include <QStringList>
#include <QSurfaceFormat>

#include <cstdio>
#include <exception>
#include <stdexcept>

namespace {

constexpr int kRenderSize = 1024;
constexpr int kSettleFrames = 8;
constexpr double kRenderTime = 0.5;

QByteArray readProgram(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(("cannot open DSL program '" + path + "'").toStdString());
    }
    return file.readAll();
}

} // namespace

int main(int argc, char** argv) {
    QSurfaceFormat format;
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);

    QGuiApplication app(argc, argv);
    const QStringList args = app.arguments();
    const QString programPath = args.size() > 1 ? args.at(1) : QStringLiteral(NM_PROGRAM_PATH);
    const QString outputPath = args.size() > 2 ? args.at(2) : QStringLiteral("output.png");
    const QString dataRoot = QStringLiteral(NM_DATA_ROOT);

    try {
        nm::EffectRegistry registry;
        registry.loadAll(dataRoot);
        const nm::Graph graph = nm::compileGraph(QString::fromUtf8(readProgram(programPath)), registry);

        nm::Backend backend;
        backend.setup(nullptr, dataRoot, QSize(kRenderSize, kRenderSize));
        for (int frame = 0; frame < kSettleFrames; ++frame) {
            backend.render(graph, kRenderTime);
        }

        if (!nm::savePng(backend.readSurface(), outputPath)) {
            throw std::runtime_error(("cannot write PNG '" + outputPath + "'").toStdString());
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "ERROR: %s\n", error.what());
        return 1;
    }

    std::printf("Rendered %s\n", outputPath.toUtf8().constData());
    return 0;
}
