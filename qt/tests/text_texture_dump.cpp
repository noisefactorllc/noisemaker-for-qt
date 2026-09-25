// text_texture_dump -- candidate side of parity/check_text_canvas.mjs.
//
//   text_texture_dump <dataRoot> <cases.json> <outDir>
//
// cases.json is an array of {name, width, height, uniforms}, where uniforms
// are filter/text pass uniforms. For each case this registers the fonts
// under <dataRoot>/fonts, draws nm::renderTextTexture() and writes
// <outDir>/<name>.rgba: straight RGBA8, row 0 at the top, width * height * 4
// bytes, and <outDir>/<name>.face: the family of the font it drew with and
// that font's PostScript name ('name' table record 6), one per line, UTF-8.
// Exit 0 on success, 2 on a usage or I/O error.

#include "runtime/text_texture.h"

#include <QDir>
#include <QFile>
#include <QFontInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRawFont>
#include <QtEndian>

#include <cstdio>

namespace {

// Record 6 of the font's 'name' table, preferring a Windows (UTF-16BE)
// record to a Macintosh (Roman) one.
QString postScriptName(const QRawFont& font) {
    const QByteArray table = font.fontTable("name");
    if (table.size() < 6) return QString();
    const auto* bytes = reinterpret_cast<const uchar*>(table.constData());
    const int count = qFromBigEndian<quint16>(bytes + 2);
    const int strings = qFromBigEndian<quint16>(bytes + 4);
    QString roman;
    for (int i = 0; i < count && 6 + 12 * (i + 1) <= table.size(); ++i) {
        const uchar* record = bytes + 6 + 12 * i;
        const int platform = qFromBigEndian<quint16>(record);
        const int length = qFromBigEndian<quint16>(record + 8);
        const int offset = strings + qFromBigEndian<quint16>(record + 10);
        if (qFromBigEndian<quint16>(record + 6) != 6 || offset + length > table.size()) continue;
        if (platform == 3) {
            QString name;
            for (int k = 0; k + 1 < length; k += 2) name.append(QChar(qFromBigEndian<quint16>(bytes + offset + k)));
            return name;
        }
        if (platform == 1 && roman.isEmpty()) roman = QString::fromLatin1(table.constData() + offset, length);
    }
    return roman;
}

} // namespace

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
        const nm::TextTextureParams params = nm::textTextureParams(item.value(QStringLiteral("uniforms")).toObject());
        const QImage image = nm::renderTextTexture(params, size);
        QFile out(outDir.filePath(name + QStringLiteral(".rgba")));
        QFile face(outDir.filePath(name + QStringLiteral(".face")));
        if (name.isEmpty() || image.isNull() || !out.open(QIODevice::WriteOnly) || !face.open(QIODevice::WriteOnly)) {
            std::fprintf(stderr, "cannot write case '%s'\n", qPrintable(name));
            return 2;
        }
        for (int row = 0; row < image.height(); ++row) {
            out.write(reinterpret_cast<const char*>(image.constScanLine(row)), qint64(image.width()) * 4);
        }
        const QFont font = nm::detail::textFont(params, size);
        face.write((QFontInfo(font).family() + QLatin1Char('\n') + postScriptName(QRawFont::fromFont(font))
                    + QLatin1Char('\n')).toUtf8());
    }
    return 0;
}
