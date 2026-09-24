#include "text_texture.h"

#include "backend.h"
#include "graph.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QRawFont>
#include <QRegularExpression>
#include <QSet>
#include <QtEndian>
#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
#include <QFontVariableAxis>
#endif

#include <algorithm>
#include <cmath>
#include <utility>

namespace nm {

namespace {

constexpr double kLineHeight = 1.2; // reference: lineHeight = fontSize * 1.2

double numberOr(const QJsonObject& uniforms, const char* key, double fallback) {
    const QJsonValue value = uniforms.value(QLatin1String(key));
    return value.isDouble() ? value.toDouble() : fallback;
}

QString stringOr(const QJsonObject& uniforms, const char* key, const QString& fallback) {
    const QJsonValue value = uniforms.value(QLatin1String(key));
    return value.isString() ? value.toString() : fallback;
}

int channelByte(double value) {
    return std::clamp(static_cast<int>(std::lround(value * 255.0)), 0, 255);
}

// Noisedeck _hexToRgb: an array keeps its first three components; a string
// must be #rrggbb, anything else is white.
QColor colorFrom(const QJsonValue& value, const QColor& fallback) {
    if (value.isArray()) {
        const QJsonArray rgb = value.toArray();
        if (rgb.size() < 3) return fallback;
        return QColor(channelByte(rgb.at(0).toDouble()), channelByte(rgb.at(1).toDouble()),
                      channelByte(rgb.at(2).toDouble()));
    }
    if (value.isString()) {
        static const QRegularExpression hex(QStringLiteral("^#?([a-f\\d]{2})([a-f\\d]{2})([a-f\\d]{2})$"),
                                            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = hex.match(value.toString());
        if (!match.hasMatch()) return QColor(255, 255, 255);
        return QColor(match.captured(1).toInt(nullptr, 16), match.captured(2).toInt(nullptr, 16),
                      match.captured(3).toInt(nullptr, 16));
    }
    return fallback;
}

// Noisedeck TextCanvasRenderer.renderTextToCanvas weight table: longest
// first and word-anchored, so "Condensed ExtraBold" reads as ExtraBold.
int weightFromStyle(const QString& style) {
    static const std::pair<const char*, int> kWeights[] = {
        {"ExtraBlack", 1000}, {"ExtraLight", 200}, {"ExtraBold", 800}, {"SemiLight", 350},
        {"SemiBold", 600},    {"Regular", 400},    {"Variable", 400},  {"Medium", 500},
        {"Black", 900},       {"Light", 300},      {"Bold", 700},      {"Thin", 100},
        {"Text", 450},
    };
    for (const auto& [name, weight] : kWeights) {
        const QRegularExpression word(QStringLiteral("\\b%1\\b").arg(QLatin1String(name)),
                                      QRegularExpression::CaseInsensitiveOption);
        if (word.match(style).hasMatch()) return weight;
    }
    return 400;
}

bool isItalicStyle(const QString& style) {
    return style.contains(QStringLiteral("italic"), Qt::CaseInsensitive);
}

// CSS generic families, which a canvas font string leaves unquoted.
bool genericStyleHint(const QString& family, QFont::StyleHint* hint) {
    const QString name = family.trimmed().toLower();
    if (name == QStringLiteral("serif") || name == QStringLiteral("ui-serif")) {
        *hint = QFont::Serif;
    } else if (name == QStringLiteral("sans-serif") || name == QStringLiteral("ui-sans-serif")
               || name == QStringLiteral("ui-rounded")) {
        *hint = QFont::SansSerif;
    } else if (name == QStringLiteral("monospace") || name == QStringLiteral("ui-monospace")) {
        *hint = QFont::Monospace;
    } else if (name == QStringLiteral("cursive")) {
        *hint = QFont::Cursive;
    } else if (name == QStringLiteral("fantasy")) {
        *hint = QFont::Fantasy;
    } else if (name == QStringLiteral("system-ui")) {
        *hint = QFont::System;
    } else {
        return false;
    }
    return true;
}

QFont fontFor(const TextTextureParams& params, int pixelSize) {
    QFont font;
    QFont::StyleHint hint = QFont::AnyStyle;
    if (genericStyleHint(params.font, &hint)) {
        if (hint == QFont::System) {
            font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        } else {
            font.setStyleHint(hint);
            font.setFamily(font.defaultFamily());
        }
    } else {
        font.setFamily(params.font.trimmed());
    }
    const int weight = weightFromStyle(params.style);
    font.setPixelSize(pixelSize);
    font.setWeight(static_cast<QFont::Weight>(std::min(weight, 900)));
    font.setItalic(isItalicStyle(params.style));
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setKerning(true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    // A browser drives a variable font's wght axis with the exact CSS
    // weight; QFont::setWeight() only picks the nearest named instance.
    const QFont::Tag wght("wght");
    for (const QFontVariableAxis& axis : QFontInfo(font).variableAxes()) {
        if (axis.tag() == wght) {
            font.setVariableAxis(wght, static_cast<float>(std::clamp(static_cast<qreal>(weight),
                                                                     axis.minimumValue(), axis.maximumValue())));
        }
    }
#endif
    return font;
}

// Chromium TextMetrics::GetFontBaseline(kMiddleTextBaseline): half the
// difference of the OS/2 typographic ascent and descent after both are
// scaled to sum to the font size and rounded to 1/64 px (LayoutUnit). Fonts
// without usable typographic metrics fall back to ascent and descent.
double middleBaselineOffset(const QFont& font, int pixelSize) {
    double ascent = 0.0;
    double descent = 0.0;
    const QByteArray os2 = QRawFont::fromFont(font).fontTable("OS/2");
    if (os2.size() >= 72) {
        const auto* bytes = reinterpret_cast<const uchar*>(os2.constData());
        ascent = qFromBigEndian<qint16>(bytes + 68);
        descent = -static_cast<double>(qFromBigEndian<qint16>(bytes + 70));
    }
    double height = ascent + descent;
    if (height <= 0 || ascent < 0 || ascent > height) {
        const QFontMetricsF metrics(font);
        ascent = metrics.ascent();
        descent = metrics.descent();
        height = ascent + descent;
        if (height <= 0 || ascent < 0 || ascent > height) return 0.0;
    }
    const auto layoutUnit = [](double value) { return std::round(value * 64.0) / 64.0; };
    const double normalizedAscent = layoutUnit(pixelSize * ascent / height);
    const double normalizedDescent = layoutUnit(pixelSize * descent / height);
    return (normalizedAscent - normalizedDescent) / 2.0;
}

} // namespace

TextTextureParams textTextureParams(const QJsonObject& uniforms) {
    TextTextureParams params;
    params.text = stringOr(uniforms, "text", params.text);
    params.font = stringOr(uniforms, "font", params.font);
    params.style = stringOr(uniforms, "style", params.style);
    params.size = numberOr(uniforms, "size", params.size);
    params.posX = numberOr(uniforms, "posX", params.posX);
    params.posY = numberOr(uniforms, "posY", params.posY);
    params.rotation = numberOr(uniforms, "rotation", params.rotation);
    params.color = colorFrom(uniforms.value(QStringLiteral("color")), params.color);
    params.justify = stringOr(uniforms, "justify", params.justify);
    return params;
}

int registerBundledFonts(const QString& dataRoot) {
    static QMutex mutex;
    static QSet<QString> registered;
    const QString fontsDir = QDir(dataRoot).filePath(QStringLiteral("fonts"));
    int added = 0;
    QDirIterator it(fontsDir, {QStringLiteral("*.ttf"), QStringLiteral("*.otf")}, QDir::Files,
                    QDirIterator::Subdirectories);
    QStringList files;
    while (it.hasNext()) files.append(QFileInfo(it.next()).canonicalFilePath());
    files.sort();
    QMutexLocker lock(&mutex);
    for (const QString& file : files) {
        if (file.isEmpty() || registered.contains(file)) continue;
        if (QFontDatabase::addApplicationFont(file) < 0) {
            qWarning("nm: cannot register font %s", qPrintable(file));
            continue;
        }
        registered.insert(file);
        ++added;
    }
    return added;
}

QImage renderTextTexture(const TextTextureParams& params, QSize canvasSize) {
    if (canvasSize.width() <= 0 || canvasSize.height() <= 0) return QImage();
    QImage canvas(canvasSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);

    const int fontSize = static_cast<int>(
        std::round(params.size * std::min(canvasSize.width(), canvasSize.height())));
    if (fontSize > 0 && !params.text.isEmpty()) {
        const QFont font = fontFor(params, fontSize);
        const QFontMetricsF metrics(font);
        const double middle = middleBaselineOffset(font, fontSize);
        const QStringList lines = params.text.split(QLatin1Char('\n'));
        const double lineHeight = fontSize * kLineHeight;
        const double startY = -(lines.size() - 1) * lineHeight / 2.0;

        QPainterPath path;
        for (int i = 0; i < lines.size(); ++i) {
            const QString& line = lines.at(i);
            if (line.isEmpty()) continue;
            const double width = metrics.horizontalAdvance(line);
            double x = 0.0;
            if (params.justify == QStringLiteral("center")) {
                x = -width / 2.0;
            } else if (params.justify == QStringLiteral("right") || params.justify == QStringLiteral("end")) {
                x = -width;
            }
            path.addText(QPointF(x, startY + i * lineHeight + middle), font, line);
        }

        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.translate(params.posX * canvasSize.width(), params.posY * canvasSize.height());
        painter.rotate(params.rotation);
        painter.fillPath(path, QColor(params.color.red(), params.color.green(), params.color.blue()));
    }
    return canvas.convertToFormat(QImage::Format_RGBA8888);
}

QStringList updateTextTextures(Backend& backend, const Graph& graph, const QString& dataRoot,
                               QSize canvasSize) {
    static const QRegularExpression textTexId(QStringLiteral("^textTex_step_\\d+$"));
    QStringList uploaded;
    for (const Pass& pass : graph.passes) {
        for (auto it = pass.inputs.begin(); it != pass.inputs.end(); ++it) {
            const QString texId = it.value().toString();
            if (!textTexId.match(texId).hasMatch() || uploaded.contains(texId)) continue;
            if (uploaded.isEmpty()) registerBundledFonts(dataRoot);
            const QImage image = renderTextTexture(textTextureParams(pass.uniforms), canvasSize);
            backend.updateTextureFromSource(texId, image, ExternalTextureOptions{true});
            uploaded.append(texId);
        }
    }
    return uploaded;
}

} // namespace nm
