// async_overlay_dump -- candidate side of parity/check_async_overlay.mjs.
//
//   async_overlay_dump overlay <effectKey> <width> <height> <paramsJson> <out>
//       the asyncInit overlay as uploaded: straight RGBA8, row 0 at the top
//   async_overlay_dump canvas <effectKey> <width> <height> <paramsJson> <out>
//       the same trace's premultiplied canvas pixels
//   async_overlay_dump strokes <in.f64> <width> <height> <out>
//       strokes a list of float64 records [x0, y0, x1, y1, lineWidth, alpha,
//       r, g, b] with round caps; writes the premultiplied canvas
//   async_overlay_dump unpremultiply <in.rgba> <out>
//       the upload conversion of premultiplied RGBA8
//   async_overlay_dump math <in.f64> <out.f64>
//       for float64 pairs [kind, x] (kind 0 sin, 1 cos, 2 log) writes the
//       results of the port's Math.sin, Math.cos and Math.log
//
// Exit status 0 on success, 2 on bad arguments or I/O errors.

#include "../noisemaker/runtime/async_overlay.h"
#include "../noisemaker/runtime/stroke_canvas.h"
#include "../noisemaker/runtime/worm_tracer.h"

#include <QByteArray>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

bool readFile(const char* path, QByteArray& out) {
    QFile file(QString::fromUtf8(path));
    if (!file.open(QIODevice::ReadOnly)) return false;
    out = file.readAll();
    return true;
}

bool writeFile(const char* path, const void* data, qsizetype size) {
    QFile file(QString::fromUtf8(path));
    if (!file.open(QIODevice::WriteOnly)) return false;
    return file.write(static_cast<const char*>(data), size) == size;
}

std::vector<double> toDoubles(const QByteArray& bytes) {
    std::vector<double> values(static_cast<size_t>(bytes.size()) / sizeof(double));
    std::memcpy(values.data(), bytes.constData(), values.size() * sizeof(double));
    return values;
}

int usage() {
    std::fprintf(stderr,
                 "usage: async_overlay_dump overlay|canvas <effectKey> <w> <h> <paramsJson> <out>\n"
                 "       async_overlay_dump strokes <in.f64> <w> <h> <out>\n"
                 "       async_overlay_dump unpremultiply <in.rgba> <out>\n"
                 "       async_overlay_dump math <in.f64> <out.f64>\n");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const QByteArray mode(argv[1]);

    if ((mode == "overlay" || mode == "canvas") && argc == 7) {
        const QString effectKey = QString::fromUtf8(argv[2]);
        const int width = std::atoi(argv[3]);
        const int height = std::atoi(argv[4]);
        QJsonParseError error{};
        const QJsonDocument params = QJsonDocument::fromJson(QByteArray(argv[5]), &error);
        if (error.error != QJsonParseError::NoError || !params.isObject() || width <= 0 || height <= 0
            || !nm::hasAsyncInit(effectKey)) {
            return usage();
        }
        nm::StrokeCanvas canvas(width, height);
        nm::runAsyncInit(effectKey, canvas, params.object());
        const std::vector<std::uint8_t> pixels = mode == "overlay" ? canvas.unpremultipliedRgba8()
                                                                   : canvas.premultiplied();
        return writeFile(argv[6], pixels.data(), static_cast<qsizetype>(pixels.size())) ? 0 : 2;
    }

    if (mode == "strokes" && argc == 6) {
        QByteArray bytes;
        const int width = std::atoi(argv[3]);
        const int height = std::atoi(argv[4]);
        if (!readFile(argv[2], bytes) || width <= 0 || height <= 0) return usage();
        const std::vector<double> records = toDoubles(bytes);
        nm::StrokeCanvas canvas(width, height);
        for (size_t i = 0; i + 9 <= records.size(); i += 9) {
            const double* r = &records[i];
            canvas.setLineWidth(r[4]);
            canvas.setStrokeColor(r[6], r[7], r[8], r[5]);
            canvas.strokeLine(r[0], r[1], r[2], r[3]);
        }
        const std::vector<std::uint8_t>& pixels = canvas.premultiplied();
        return writeFile(argv[5], pixels.data(), static_cast<qsizetype>(pixels.size())) ? 0 : 2;
    }

    if (mode == "unpremultiply" && argc == 4) {
        QByteArray bytes;
        if (!readFile(argv[2], bytes) || bytes.size() % 4 != 0) return usage();
        const std::vector<std::uint8_t> premultiplied(bytes.constBegin(), bytes.constEnd());
        const std::vector<std::uint8_t> pixels = nm::unpremultiplyForUpload(premultiplied);
        return writeFile(argv[3], pixels.data(), static_cast<qsizetype>(pixels.size())) ? 0 : 2;
    }

    if (mode == "math" && argc == 4) {
        QByteArray bytes;
        if (!readFile(argv[2], bytes)) return usage();
        const std::vector<double> pairs = toDoubles(bytes);
        std::vector<double> results;
        results.reserve(pairs.size() / 2);
        for (size_t i = 0; i + 2 <= pairs.size(); i += 2) {
            const double x = pairs[i + 1];
            const int kind = static_cast<int>(pairs[i]);
            results.push_back(kind == 0 ? nm::detail::jsSin(x) : kind == 1 ? nm::detail::jsCos(x) : nm::detail::jsLog(x));
        }
        return writeFile(argv[3], results.data(), static_cast<qsizetype>(results.size() * sizeof(double))) ? 0 : 2;
    }

    return usage();
}
