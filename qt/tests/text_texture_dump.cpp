// text_texture_dump -- candidate side of parity/check_text_canvas.mjs.
//
//   text_texture_dump <dataRoot> <cases.json> <outDir>
//
// cases.json is an array of {name, width, height, uniforms}, where uniforms
// are filter/text pass uniforms. For each case this registers the fonts
// under <dataRoot>/fonts, draws nm::renderTextTexture() and writes
// <outDir>/<name>.rgba: straight RGBA8, row 0 at the top, width * height * 4
// bytes. Exit 0 on success, 2 on a usage or I/O error.

#include "runtime/text_texture.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() != 4) {
        std::fprintf(stderr, "usage: text_texture_dump <dataRoot> <cases.json> <outDir>\n");
        return 2;
    }
    QFile file(args.at(2));
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot open %s\n", qPrintable(args.at(2)));
        return 2;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray()) {
        std::fprintf(stderr, "%s is not a JSON array\n", qPrintable(args.at(2)));
        return 2;
    }
    nm::registerBundledFonts(args.at(1));
    const QDir outDir(args.at(3));
    for (const QJsonValue& value : doc.array()) {
        const QJsonObject item = value.toObject();
        const QString name = item.value(QStringLiteral("name")).toString();
        const QSize size(item.value(QStringLiteral("width")).toInt(), item.value(QStringLiteral("height")).toInt());
        const QImage image = nm::renderTextTexture(
            nm::textTextureParams(item.value(QStringLiteral("uniforms")).toObject()), size);
        QFile out(outDir.filePath(name + QStringLiteral(".rgba")));
        if (name.isEmpty() || image.isNull() || !out.open(QIODevice::WriteOnly)) {
            std::fprintf(stderr, "cannot write case '%s'\n", qPrintable(name));
            return 2;
        }
        for (int row = 0; row < image.height(); ++row) {
            out.write(reinterpret_cast<const char*>(image.constScanLine(row)), qint64(image.width()) * 4);
        }
    }
    return 0;
}
