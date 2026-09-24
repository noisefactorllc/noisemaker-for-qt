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
#include <QGlyphRun>
#include <QJsonArray>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QRawFont>
#include <QRegularExpression>
#include <QSet>
#include <QTextLayout>
#include <QTextOption>
#include <QTransform>
#include <QtEndian>
#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
#include <QFontVariableAxis>
#endif

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
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

// Chromium draws italic text in a family without an italic face as a
// synthetic oblique: SkFont skewX -1/4 about the baseline
// (FontPlatformData::SetupSkFont). Font engines slant by their own amounts
// (Qt's CoreText engine by tan 14 degrees, FreeType by about 12), so the
// renderer applies the browser's skew itself.
constexpr double kSyntheticItalicSkew = 0.25;

bool hasItalicFace(const QFont& font) {
    const QString family = QFontInfo(font).family();
    const QStringList styles = QFontDatabase::styles(family);
    return std::any_of(styles.begin(), styles.end(),
                       [&family](const QString& style) { return QFontDatabase::italic(family, style); });
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

// ---------------------------------------------------------------- kerning
// Variation deltas of GPOS 'kern' pair adjustments (see
// detail::kerningVariationDeltas in text_texture.h). Table layouts follow
// the OpenType specification (fvar, avar, GPOS, GDEF, ItemVariationStore);
// the arithmetic follows HarfBuzz, which the browser shapes with: axis
// values normalized to F2Dot14 with roundf, the avar segment map, and
// region scalars from the F2Dot14 coordinates.

// Big-endian reads with bounds checks: an out-of-range read returns 0 and
// marks the reader failed, so a malformed table yields no deltas.
class TableReader {
public:
    explicit TableReader(QByteArray data) : m_data(std::move(data)) {}
    bool empty() const { return m_data.isEmpty(); }
    bool failed() const { return m_failed; }
    quint16 u16(qint64 offset) const { return read<quint16>(offset); }
    qint16 i16(qint64 offset) const { return read<qint16>(offset); }
    quint32 u32(qint64 offset) const { return read<quint32>(offset); }
    qint32 i32(qint64 offset) const { return read<qint32>(offset); }
    qint8 i8(qint64 offset) const {
        if (offset < 0 || offset >= m_data.size()) {
            m_failed = true;
            return 0;
        }
        return static_cast<qint8>(m_data.at(static_cast<qsizetype>(offset)));
    }

private:
    template <typename T>
    T read(qint64 offset) const {
        if (offset < 0 || offset > m_data.size() - static_cast<qint64>(sizeof(T))) {
            m_failed = true;
            return 0;
        }
        return qFromBigEndian<T>(m_data.constData() + offset);
    }
    QByteArray m_data;
    mutable bool m_failed = false;
};

constexpr quint32 fourCC(char a, char b, char c, char d) {
    return (static_cast<quint32>(static_cast<uchar>(a)) << 24) | (static_cast<quint32>(static_cast<uchar>(b)) << 16)
        | (static_cast<quint32>(static_cast<uchar>(c)) << 8) | static_cast<quint32>(static_cast<uchar>(d));
}

// hb_ot_var normalize_axis_value: clamp, scale to -1..1 around the default,
// round to F2Dot14.
int normalizedAxisValue(double value, double minimum, double defaultValue, double maximum) {
    value = std::clamp(value, std::min(minimum, defaultValue), std::max(maximum, defaultValue));
    double v = 0.0;
    if (value < defaultValue && defaultValue > minimum) {
        v = (value - defaultValue) / (defaultValue - minimum);
    } else if (value > defaultValue && maximum > defaultValue) {
        v = (value - defaultValue) / (maximum - defaultValue);
    }
    return static_cast<int>(std::round(v * 16384.0));
}

// avar SegmentMaps::map (version 1), on F2Dot14 integers.
int avarMap(const TableReader& avar, qint64 segment, int count, int value) {
    const auto from = [&](int i) { return static_cast<int>(avar.i16(segment + 2 + 4 * i)); };
    const auto to = [&](int i) { return static_cast<int>(avar.i16(segment + 4 + 4 * i)); };
    if (count < 2) return count == 0 ? value : value - from(0) + to(0);
    if (value <= from(0)) return value - from(0) + to(0);
    int i = 1;
    while (i < count - 1 && value > from(i)) ++i;
    if (value >= from(i)) return value - from(i) + to(i);
    if (from(i - 1) == from(i)) return to(i - 1);
    const double interpolated = to(i - 1)
        + static_cast<double>(to(i) - to(i - 1)) * (value - from(i - 1)) / (from(i) - from(i - 1));
    return static_cast<int>(std::round(interpolated));
}

// Normalized F2Dot14 coordinates of every fvar axis, with wght at `wght`
// and the others at their defaults. Empty when the font has no wght axis.
std::vector<int> normalizedCoordinates(const QRawFont& font, double wght) {
    const TableReader fvar(font.fontTable("fvar"));
    if (fvar.empty() || fvar.u16(0) != 1) return {};
    const qint64 axesOffset = fvar.u16(4);
    const int axisCount = fvar.u16(8);
    const int axisSize = fvar.u16(10);
    std::vector<int> coords(static_cast<std::size_t>(axisCount), 0);
    bool haveWght = false;
    for (int a = 0; a < axisCount; ++a) {
        const qint64 record = axesOffset + static_cast<qint64>(a) * axisSize;
        if (fvar.u32(record) != fourCC('w', 'g', 'h', 't')) continue;
        coords[static_cast<std::size_t>(a)] = normalizedAxisValue(
            wght, fvar.i32(record + 4) / 65536.0, fvar.i32(record + 8) / 65536.0, fvar.i32(record + 12) / 65536.0);
        haveWght = true;
    }
    if (!haveWght || fvar.failed()) return {};

    const TableReader avar(font.fontTable("avar"));
    if (!avar.empty() && avar.u16(0) == 1 && avar.u16(6) == axisCount) {
        qint64 segment = 8;
        for (int a = 0; a < axisCount; ++a) {
            const int count = avar.u16(segment);
            coords[static_cast<std::size_t>(a)] = avarMap(avar, segment, count, coords[static_cast<std::size_t>(a)]);
            segment += 2 + 4 * static_cast<qint64>(count);
        }
        if (avar.failed()) return {};
    }
    return coords;
}

// Coverage index of `glyph`, or -1.
int coverageIndex(const TableReader& t, qint64 coverage, quint32 glyph) {
    const int format = t.u16(coverage);
    const int count = t.u16(coverage + 2);
    if (format == 1) {
        int lo = 0;
        int hi = count - 1;
        while (lo <= hi) {
            const int mid = (lo + hi) / 2;
            const quint32 g = t.u16(coverage + 4 + 2 * mid);
            if (g == glyph) return mid;
            if (g < glyph) lo = mid + 1; else hi = mid - 1;
        }
    } else if (format == 2) {
        for (int r = 0; r < count; ++r) {
            const qint64 range = coverage + 4 + 6 * static_cast<qint64>(r);
            const quint32 start = t.u16(range);
            const quint32 end = t.u16(range + 2);
            if (glyph >= start && glyph <= end) return t.u16(range + 4) + static_cast<int>(glyph - start);
        }
    }
    return -1;
}

// Class of `glyph` in a ClassDef table (0 when absent or unlisted).
int glyphClass(const TableReader& t, qint64 classDef, quint32 glyph) {
    if (classDef <= 0) return 0;
    const int format = t.u16(classDef);
    if (format == 1) {
        const quint32 start = t.u16(classDef + 2);
        const quint32 count = t.u16(classDef + 4);
        if (glyph >= start && glyph < start + count) return t.u16(classDef + 6 + 2 * static_cast<qint64>(glyph - start));
    } else if (format == 2) {
        const int count = t.u16(classDef + 2);
        for (int r = 0; r < count; ++r) {
            const qint64 range = classDef + 4 + 6 * static_cast<qint64>(r);
            if (glyph >= t.u16(range) && glyph <= t.u16(range + 2)) return t.u16(range + 4);
        }
    }
    return 0;
}

int valueRecordSize(int valueFormat) {
    int fields = 0;
    for (int bit = 0; bit < 8; ++bit) fields += (valueFormat >> bit) & 1;
    return 2 * fields;
}

// Offset of the XAdvDevice field inside a value record, or -1.
int xAdvanceDeviceField(int valueFormat) {
    if (!(valueFormat & 0x0040)) return -1;
    return valueRecordSize(valueFormat & 0x003f);
}

class ItemVariationStore {
public:
    ItemVariationStore(const TableReader& gdef, qint64 store, std::vector<int> coords)
        : m_t(&gdef), m_store(store), m_coords(std::move(coords)) {}

    // Delta of the item (outer, inner) at the coordinates.
    double delta(int outer, int inner) {
        if (outer == 0xffff && inner == 0xffff) return 0.0; // NO_VARIATION_INDEX
        if (m_t->u16(m_store) != 1 || outer >= m_t->u16(m_store + 6)) return 0.0;
        const qint64 data = m_store + m_t->u32(m_store + 8 + 4 * static_cast<qint64>(outer));
        const int itemCount = m_t->u16(data);
        const int wordField = m_t->u16(data + 2);
        const int regionCount = m_t->u16(data + 4);
        if (inner >= itemCount) return 0.0;
        const bool longWords = wordField & 0x8000;
        const int wordCount = wordField & 0x7fff;
        const int wordSize = longWords ? 4 : 2;
        const int shortSize = longWords ? 2 : 1;
        const qint64 rowSize = static_cast<qint64>(wordCount) * wordSize + static_cast<qint64>(regionCount - wordCount) * shortSize;
        qint64 cell = data + 6 + 2 * static_cast<qint64>(regionCount) + inner * rowSize;
        double sum = 0.0;
        for (int k = 0; k < regionCount; ++k) {
            double d = 0.0;
            if (k < wordCount) {
                d = longWords ? m_t->i32(cell) : m_t->i16(cell);
                cell += wordSize;
            } else {
                d = longWords ? m_t->i16(cell) : m_t->i8(cell);
                cell += shortSize;
            }
            if (d != 0.0) sum += d * regionScalar(m_t->u16(data + 6 + 2 * static_cast<qint64>(k)));
        }
        return sum;
    }

private:
    // VarRegionList region scalar (HarfBuzz VarRegionAxis::evaluate).
    double regionScalar(int region) {
        const auto cached = m_scalars.find(region);
        if (cached != m_scalars.end()) return cached->second;
        const qint64 list = m_store + m_t->u32(m_store + 2);
        const int axisCount = m_t->u16(list);
        double scalar = region < m_t->u16(list + 2) ? 1.0 : 0.0;
        for (int a = 0; a < axisCount && scalar != 0.0; ++a) {
            const qint64 axis = list + 4 + (static_cast<qint64>(region) * axisCount + a) * 6;
            const int start = m_t->i16(axis);
            const int peak = m_t->i16(axis + 2);
            const int end = m_t->i16(axis + 4);
            const int coord = a < static_cast<int>(m_coords.size()) ? m_coords[static_cast<std::size_t>(a)] : 0;
            double factor = 0.0;
            if (peak == 0 || coord == peak) factor = 1.0;
            else if (coord == 0) factor = 0.0;
            else if (start > peak || peak > end) factor = 1.0;
            else if (start < 0 && end > 0) factor = 1.0;
            else if (coord <= start || end <= coord) factor = 0.0;
            else if (coord < peak) factor = static_cast<double>(coord - start) / (peak - start);
            else factor = static_cast<double>(end - coord) / (end - peak);
            scalar *= factor;
        }
        m_scalars.emplace(region, scalar);
        return scalar;
    }

    const TableReader* m_t;
    qint64 m_store;
    std::vector<int> m_coords;
    std::map<int, double> m_scalars;
};

// Delta of a VariationIndex device table at `device` (0 for other formats).
double deviceDelta(const TableReader& gpos, qint64 device, ItemVariationStore& store) {
    if (gpos.u16(device + 4) != 0x8000) return 0.0;
    return store.delta(gpos.u16(device), gpos.u16(device + 2));
}

struct PairMatch {
    bool matched = false;
    double firstDelta = 0.0;
    double secondDelta = 0.0;
    bool skipSecond = false; // valueFormat2 != 0: HarfBuzz continues after the second glyph
};

// PairPos format 1 or 2 subtable at `subtable` applied to (first, second).
PairMatch pairAdjustment(const TableReader& gpos, qint64 subtable, quint32 first, quint32 second,
                         ItemVariationStore& store) {
    PairMatch match;
    const int format = gpos.u16(subtable);
    const int index = coverageIndex(gpos, subtable + gpos.u16(subtable + 2), first);
    if (index < 0 || (format != 1 && format != 2)) return match;
    const int valueFormat1 = gpos.u16(subtable + 4);
    const int valueFormat2 = gpos.u16(subtable + 6);
    const int size1 = valueRecordSize(valueFormat1);
    const int size2 = valueRecordSize(valueFormat2);
    qint64 base = 0;
    qint64 record = 0;
    if (format == 1) {
        if (index >= gpos.u16(subtable + 8)) return match;
        base = subtable + gpos.u16(subtable + 10 + 2 * static_cast<qint64>(index)); // PairSet
        const int count = gpos.u16(base);
        const qint64 recordSize = 2 + size1 + size2;
        int lo = 0;
        int hi = count - 1;
        record = -1;
        while (lo <= hi) {
            const int mid = (lo + hi) / 2;
            const quint32 g = gpos.u16(base + 2 + mid * recordSize);
            if (g == second) {
                record = base + 2 + mid * recordSize + 2;
                break;
            }
            if (g < second) lo = mid + 1; else hi = mid - 1;
        }
        if (record < 0) return match;
    } else {
        base = subtable;
        const int class1 = glyphClass(gpos, subtable + gpos.u16(subtable + 8), first);
        const int class2 = glyphClass(gpos, subtable + gpos.u16(subtable + 10), second);
        const int class1Count = gpos.u16(subtable + 12);
        const int class2Count = gpos.u16(subtable + 14);
        if (class1 >= class1Count || class2 >= class2Count) return match;
        record = subtable + 16 + (static_cast<qint64>(class1) * class2Count + class2) * (size1 + size2);
    }
    match.matched = true;
    match.skipSecond = valueFormat2 != 0;
    if (const int field = xAdvanceDeviceField(valueFormat1); field >= 0) {
        if (const quint16 offset = gpos.u16(record + field)) match.firstDelta = deviceDelta(gpos, base + offset, store);
    }
    if (const int field = xAdvanceDeviceField(valueFormat2); field >= 0) {
        if (const quint16 offset = gpos.u16(record + size1 + field)) match.secondDelta = deviceDelta(gpos, base + offset, store);
    }
    return match;
}

// One laid-out line: the glyph runs of a design-metrics QTextLayout and the
// pixel offset each glyph moves by from the kerning variation deltas of the
// glyphs before it.
struct LineOutline {
    struct Run {
        QRawFont font;
        QList<quint32> glyphs;
        QList<QPointF> positions;
        std::vector<double> offsets;
    };
    QFont font;
    QString text;
    std::vector<Run> runs;
    bool hasDeltas = false;
    double deltaAdvance = 0.0;

    // The outline with the left end of the baseline at `origin`.
    QPainterPath path(QPointF origin) const {
        QPainterPath outline;
        if (!hasDeltas) {
            outline.addText(origin, font, text);
            return outline;
        }
        // QPainterPath::addText places glyphs at QFixed::fromReal(origin),
        // which truncates to 1/64 px, plus the layout's QFixed positions.
        const double ox = std::trunc(origin.x() * 64.0) / 64.0;
        const double oy = std::trunc(origin.y() * 64.0) / 64.0;
        for (const Run& run : runs) {
            for (qsizetype g = 0; g < run.glyphs.size(); ++g) {
                const QPointF at = run.positions.at(g);
                outline.addPath(run.font.pathForGlyph(run.glyphs.at(g))
                                    .translated(at.x() + ox + run.offsets[static_cast<std::size_t>(g)], at.y() + oy));
            }
        }
        return outline;
    }
};

LineOutline layoutLine(const QFont& font, const QString& text) {
    LineOutline line;
    line.font = font;
    line.text = text;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    const QFont::Tag wght("wght");
    if (!font.isVariableAxisSet(wght)) return line;
    const double weight = font.variableAxisValue(wght);

    QTextLayout layout(text, font);
    QTextOption option = layout.textOption();
    option.setUseDesignMetrics(true); // as QPainterPath::addText
    layout.setTextOption(option);
    layout.beginLayout();
    const QTextLine textLine = layout.createLine();
    layout.endLayout();
    const double ascent = textLine.ascent();

    double offset = 0.0;
    for (const QGlyphRun& glyphRun : layout.glyphRuns()) {
        LineOutline::Run run;
        run.font = glyphRun.rawFont();
        run.glyphs = glyphRun.glyphIndexes();
        run.positions = glyphRun.positions();
        for (QPointF& position : run.positions) position.ry() -= ascent; // baseline at y = 0
        const std::vector<double> deltas = glyphRun.isRightToLeft()
            ? std::vector<double>(static_cast<std::size_t>(run.glyphs.size()), 0.0)
            : detail::kerningVariationDeltas(run.font, run.glyphs, weight);
        const double scale = run.font.unitsPerEm() > 0 ? run.font.pixelSize() / run.font.unitsPerEm() : 0.0;
        run.offsets.resize(deltas.size());
        for (std::size_t g = 0; g < deltas.size(); ++g) {
            run.offsets[g] = offset;
            offset += deltas[g] * scale;
            line.hasDeltas = line.hasDeltas || deltas[g] != 0.0;
        }
        line.runs.push_back(std::move(run));
    }
    line.deltaAdvance = offset;
#endif
    return line;
}

} // namespace

namespace detail {

std::vector<double> kerningVariationDeltas(const QRawFont& font, const QList<quint32>& glyphs, double wght) {
    std::vector<double> deltas(static_cast<std::size_t>(glyphs.size()), 0.0);
    if (glyphs.size() < 2 || !font.isValid()) return deltas;
    const std::vector<int> coords = normalizedCoordinates(font, wght);
    if (std::all_of(coords.begin(), coords.end(), [](int c) { return c == 0; })) return deltas;

    const TableReader gdef(font.fontTable("GDEF"));
    const TableReader gpos(font.fontTable("GPOS"));
    if (gdef.empty() || gpos.empty() || gdef.u16(0) != 1 || gdef.u16(2) < 3 || gpos.u16(0) != 1) return deltas;
    const qint64 storeOffset = gdef.u32(14);
    if (storeOffset == 0) return deltas;
    const qint64 glyphClassDef = gdef.u16(4);
    ItemVariationStore store(gdef, storeOffset, coords);

    // The lookups of every 'kern' feature, in lookup-list order.
    const qint64 featureList = gpos.u16(6);
    const qint64 lookupList = gpos.u16(8);
    std::set<int> kernLookups;
    const int featureCount = gpos.u16(featureList);
    for (int f = 0; f < featureCount; ++f) {
        const qint64 record = featureList + 2 + 6 * static_cast<qint64>(f);
        if (gpos.u32(record) != fourCC('k', 'e', 'r', 'n')) continue;
        const qint64 feature = featureList + gpos.u16(record + 4);
        const int indexCount = gpos.u16(feature + 2);
        for (int k = 0; k < indexCount; ++k) kernLookups.insert(gpos.u16(feature + 4 + 2 * static_cast<qint64>(k)));
    }

    const int lookupCount = gpos.u16(lookupList);
    for (const int index : kernLookups) {
        if (index >= lookupCount) continue;
        const qint64 lookup = lookupList + gpos.u16(lookupList + 2 + 2 * static_cast<qint64>(index));
        const int type = gpos.u16(lookup);
        const int flag = gpos.u16(lookup + 2);
        // Mark attachment classes (0xff00) and mark filtering sets (0x0010)
        // are not supported: such a lookup contributes no deltas.
        if ((type != 2 && type != 9) || (flag & 0xff10)) continue;
        std::vector<qint64> subtables;
        const int subtableCount = gpos.u16(lookup + 4);
        for (int k = 0; k < subtableCount; ++k) {
            qint64 subtable = lookup + gpos.u16(lookup + 6 + 2 * static_cast<qint64>(k));
            if (type == 9) {
                if (gpos.u16(subtable) != 1 || gpos.u16(subtable + 2) != 2) continue;
                subtable += gpos.u32(subtable + 4);
            }
            subtables.push_back(subtable);
        }
        const auto ignored = [&](quint32 glyph) {
            if (!(flag & 0x000e)) return false;
            const int glyphType = glyphClass(gdef, glyphClassDef, glyph);
            return ((flag & 0x0002) && glyphType == 1) || ((flag & 0x0004) && glyphType == 2)
                || ((flag & 0x0008) && glyphType == 3);
        };
        // hb PairPos application: at each glyph the lookup is not set to
        // ignore, pair it with the next such glyph; the first subtable that
        // matches applies. After a match, continue at the second glyph, or
        // after it when the second value record is not empty.
        qsizetype i = 0;
        while (i < glyphs.size()) {
            if (ignored(glyphs.at(i))) {
                ++i;
                continue;
            }
            qsizetype j = i + 1;
            while (j < glyphs.size() && ignored(glyphs.at(j))) ++j;
            if (j >= glyphs.size()) break;
            PairMatch match;
            for (const qint64 subtable : subtables) {
                match = pairAdjustment(gpos, subtable, glyphs.at(i), glyphs.at(j), store);
                if (match.matched) break;
            }
            if (!match.matched) {
                ++i;
                continue;
            }
            deltas[static_cast<std::size_t>(i)] += match.firstDelta;
            deltas[static_cast<std::size_t>(j)] += match.secondDelta;
            i = match.skipSecond ? j + 1 : j;
        }
    }
    if (gpos.failed() || gdef.failed()) std::fill(deltas.begin(), deltas.end(), 0.0);
    return deltas;
}

QPainterPath textLinePath(const QFont& font, const QString& line, QPointF origin, double* deltaAdvance) {
    const LineOutline outline = layoutLine(font, line);
    if (deltaAdvance) *deltaAdvance = outline.deltaAdvance;
    return outline.path(origin);
}

} // namespace detail

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
        QFont font = fontFor(params, fontSize);
        const bool syntheticItalic = font.italic() && !hasItalicFace(font);
        if (syntheticItalic) font.setItalic(false);
        const QFontMetricsF metrics(font);
        const double middle = middleBaselineOffset(font, fontSize);
        const QStringList lines = params.text.split(QLatin1Char('\n'));
        const double lineHeight = fontSize * kLineHeight;
        const double startY = -(lines.size() - 1) * lineHeight / 2.0;

        QPainterPath path;
        for (int i = 0; i < lines.size(); ++i) {
            const QString& line = lines.at(i);
            if (line.isEmpty()) continue;
            const LineOutline outline = layoutLine(font, line);
            const double width = metrics.horizontalAdvance(line) + outline.deltaAdvance;
            double x = 0.0;
            if (params.justify == QStringLiteral("center")) {
                x = -width / 2.0;
            } else if (params.justify == QStringLiteral("right") || params.justify == QStringLiteral("end")) {
                x = -width;
            }
            const double baseline = startY + i * lineHeight + middle;
            if (syntheticItalic) {
                path.addPath(QTransform(1.0, 0.0, -kSyntheticItalicSkew, 1.0, 0.0, 0.0)
                                 .map(outline.path(QPointF(0.0, 0.0)))
                                 .translated(x, baseline));
            } else {
                path.addPath(outline.path(QPointF(x, baseline)));
            }
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
