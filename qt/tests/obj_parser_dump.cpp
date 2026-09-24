// obj_parser_dump -- candidate side of parity/check_obj_parser.mjs.
//
//   obj_parser_dump <manifest.json>
//
// The manifest is a JSON array of {"path": <file>, "mode": "string"|"file"}.
// "string" decodes the file as UTF-8 (keeping an initial BOM, like
// TextDecoder with ignoreBOM) and calls nm::parseOBJ, the counterpart of the
// reference parseOBJ(objText). "file" calls nm::loadOBJ(path), the
// counterpart of the reference loadOBJ(url) (fetch().text() decoding).
// Prints one JSON object per entry: the parsed arrays as Float32 bit
// patterns in hex (NaN canonicalized to 7fc00000), the 256x256 packed
// arrays as SHA-256 digests of their little-endian bytes, and the 4x4
// packed arrays in hex.

#include "noisemaker/runtime/obj_parser.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringDecoder>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <vector>

namespace {

std::uint32_t canonicalBits(float value) {
    if (std::isnan(value)) return 0x7fc00000u;
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

QString hexArray(const std::vector<float>& values) {
    QString out;
    out.reserve(static_cast<qsizetype>(values.size()) * 8);
    for (const float v : values) {
        out += QStringLiteral("%1").arg(canonicalBits(v), 8, 16, QLatin1Char('0'));
    }
    return out;
}

QString digest(const std::vector<float>& values) {
    QByteArray bytes;
    bytes.reserve(static_cast<qsizetype>(values.size()) * 4);
    for (const float v : values) {
        const std::uint32_t bits = canonicalBits(v);
        const char le[4] = {static_cast<char>(bits & 0xffu), static_cast<char>((bits >> 8) & 0xffu),
                            static_cast<char>((bits >> 16) & 0xffu), static_cast<char>((bits >> 24) & 0xffu)};
        bytes.append(le, 4);
    }
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QJsonObject dumpEntry(const QJsonObject& entry) {
    const QString path = entry.value(QStringLiteral("path")).toString();
    const QString mode = entry.value(QStringLiteral("mode")).toString();
    QJsonObject out;
    out.insert(QStringLiteral("path"), path);
    out.insert(QStringLiteral("mode"), mode);
    try {
        nm::ObjMeshData mesh;
        if (mode == QStringLiteral("file")) {
            mesh = nm::loadOBJ(path);
        } else {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                throw std::runtime_error("cannot open corpus file");
            }
            QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::ConvertInitialBom);
            const QString text = decoder.decode(file.readAll());
            mesh = nm::parseOBJ(text);
        }
        out.insert(QStringLiteral("vertexCount"), static_cast<double>(mesh.vertexCount));
        out.insert(QStringLiteral("positions"), hexArray(mesh.positions));
        out.insert(QStringLiteral("normals"), hexArray(mesh.normals));
        out.insert(QStringLiteral("uvs"), hexArray(mesh.uvs));

        const nm::PackedMeshData grid256 = nm::packMeshDataForTextures(mesh, 256, 256);
        out.insert(QStringLiteral("packed256"), QJsonObject{
            {QStringLiteral("vertexCount"), grid256.vertexCount},
            {QStringLiteral("positionData"), digest(grid256.positionData)},
            {QStringLiteral("normalData"), digest(grid256.normalData)},
            {QStringLiteral("uvData"), digest(grid256.uvData)},
        });
        const nm::PackedMeshData grid4 = nm::packMeshDataForTextures(mesh, 4, 4);
        out.insert(QStringLiteral("packed4"), QJsonObject{
            {QStringLiteral("vertexCount"), grid4.vertexCount},
            {QStringLiteral("positionData"), hexArray(grid4.positionData)},
            {QStringLiteral("normalData"), hexArray(grid4.normalData)},
            {QStringLiteral("uvData"), hexArray(grid4.uvData)},
        });
    } catch (const std::exception& e) {
        out.insert(QStringLiteral("error"), QString::fromUtf8(e.what()));
    }
    return out;
}

// The truncation warning is expected for the 4x4 packing of most inputs.
void quietTruncationWarnings(QtMsgType type, const QMessageLogContext&, const QString& message) {
    if (type == QtWarningMsg && message.startsWith(QStringLiteral("[OBJ] "))) return;
    std::fprintf(stderr, "%s\n", message.toLocal8Bit().constData());
}

} // namespace

int main(int argc, char** argv) {
    qInstallMessageHandler(quietTruncationWarnings);
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        std::fprintf(stderr, "usage: obj_parser_dump <manifest.json>\n");
        return 2;
    }
    QFile manifestFile(QString::fromLocal8Bit(argv[1]));
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot open manifest %s\n", argv[1]);
        return 2;
    }
    const QJsonDocument manifest = QJsonDocument::fromJson(manifestFile.readAll());
    if (!manifest.isArray()) {
        std::fprintf(stderr, "manifest is not a JSON array\n");
        return 2;
    }
    QJsonArray results;
    for (const QJsonValue& entry : manifest.array()) {
        results.append(dumpEntry(entry.toObject()));
    }
    const QByteArray json = QJsonDocument(results).toJson(QJsonDocument::Compact);
    std::fwrite(json.constData(), 1, static_cast<std::size_t>(json.size()), stdout);
    std::fputc('\n', stdout);
    return 0;
}
