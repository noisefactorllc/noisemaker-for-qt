// filter/text CPU rasterization (runtime/text_texture.h): parameter reading,
// canvas layout (justify, lines, rotation, colour, style), the bundled font,
// and the textTex upload through a compiled graph. Plain executable, run
// from the repo root (WORKING_DIRECTORY in CMakeLists.txt). Layout checks
// use the bundled Nunito, which rasterizes the same way on every platform
// (outline fill, no hinting). Chromium comparison: parity/check_text_canvas.mjs.

#include "../noisemaker/compiler/dsl_compiler.h"
#include "../noisemaker/compiler/effect_registry.h"
#include "../noisemaker/runtime/backend.h"
#include "../noisemaker/runtime/text_texture.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QPainterPath>
#include <QRawFont>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const char* description) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) ++g_failures;
}

const QString kDataRoot = QStringLiteral("qt/noisemaker");

struct Ink {
    double coverage = 0.0; // sum of alpha / 255
    double cx = 0.0;       // alpha-weighted centroid
    double cy = 0.0;
    int left = -1, top = -1, right = -1, bottom = -1; // alpha > 0 bounding box
};

Ink measure(const QImage& image, QRect region = QRect()) {
    Ink ink;
    if (region.isNull()) region = image.rect();
    double sx = 0.0, sy = 0.0;
    for (int y = region.top(); y <= region.bottom(); ++y) {
        const uchar* row = image.constScanLine(y);
        for (int x = region.left(); x <= region.right(); ++x) {
            const int a = row[x * 4 + 3];
            if (a == 0) continue;
            const double w = a / 255.0;
            ink.coverage += w;
            sx += w * x;
            sy += w * y;
            if (ink.left < 0 || x < ink.left) ink.left = x;
            if (x > ink.right) ink.right = x;
            if (ink.top < 0) ink.top = y;
            ink.bottom = y;
        }
    }
    if (ink.coverage > 0) {
        ink.cx = sx / ink.coverage;
        ink.cy = sy / ink.coverage;
    }
    return ink;
}

nm::TextTextureParams params(const QString& text, double size = 0.1) {
    nm::TextTextureParams p;
    p.text = text;
    p.size = size;
    return p;
}

void testParameterReading() {
    const nm::TextTextureParams defaults = nm::textTextureParams(QJsonObject{});
    check(defaults.text == QStringLiteral("Hello World") && defaults.font == QStringLiteral("Nunito")
              && defaults.size == 0.1 && defaults.posX == 0.5 && defaults.posY == 0.5
              && defaults.rotation == 0.0 && defaults.justify == QStringLiteral("center")
              && defaults.color == QColor(255, 255, 255) && defaults.style.isEmpty(),
          "absent uniforms keep the filter/text definition defaults");

    const nm::TextTextureParams read = nm::textTextureParams(QJsonObject{
        {QStringLiteral("text"), QStringLiteral("a\nb")},
        {QStringLiteral("font"), QStringLiteral("serif")},
        {QStringLiteral("style"), QStringLiteral("Bold Italic")},
        {QStringLiteral("size"), 0.25},
        {QStringLiteral("posX"), 0.2},
        {QStringLiteral("posY"), 0.7},
        {QStringLiteral("rotation"), -45},
        {QStringLiteral("color"), QJsonArray{1.0, 0.5, 0.0, 1.0}},
        {QStringLiteral("justify"), QStringLiteral("right")},
    });
    check(read.text == QStringLiteral("a\nb") && read.font == QStringLiteral("serif")
              && read.style == QStringLiteral("Bold Italic") && read.size == 0.25 && read.posX == 0.2
              && read.posY == 0.7 && read.rotation == -45.0 && read.justify == QStringLiteral("right"),
          "compiled text uniforms are read verbatim");
    check(read.color == QColor(255, 128, 0), "a [r, g, b, a] colour in 0..1 rounds to rgb bytes");

    check(nm::textTextureParams(QJsonObject{{QStringLiteral("color"), QStringLiteral("#00ff7f")}}).color
              == QColor(0, 255, 127),
          "a #rrggbb colour string parses");
    check(nm::textTextureParams(QJsonObject{{QStringLiteral("color"), QStringLiteral("#0f7")}}).color
              == QColor(255, 255, 255),
          "a colour string that is not #rrggbb reads as white, as Noisedeck _hexToRgb does");
}

void testBundledFont() {
    // The provenance README.md records for the bundled font.
    QFile fontFile(kDataRoot + QStringLiteral("/fonts/Nunito/Nunito-VariableFont_wght.ttf"));
    check(fontFile.open(QIODevice::ReadOnly)
              && QCryptographicHash::hash(fontFile.readAll(), QCryptographicHash::Sha256).toHex()
                     == "707f6b338cfd21e95f05a88169ef7647d01ad8da76623846c092f3118f762a08",
          "the bundled Nunito file matches the SHA-256 recorded in README.md");
    check(QFile::exists(kDataRoot + QStringLiteral("/fonts/Nunito/OFL.txt")), "the font's OFL ships beside it");
    nm::registerBundledFonts(kDataRoot);
    check(QFontDatabase::families().contains(QStringLiteral("Nunito")),
          "registerBundledFonts registers the bundled Nunito family");
    check(nm::registerBundledFonts(kDataRoot) == 0, "a second registration of the same files adds nothing");
    QFont font(QStringLiteral("Nunito"));
    check(QFontInfo(font).family() == QStringLiteral("Nunito"), "the default text family resolves to Nunito");
}

void testCanvasBasics() {
    check(nm::renderTextTexture(params(QStringLiteral("x")), QSize(0, 16)).isNull(),
          "an empty canvas size yields a null image");
    const QImage empty = nm::renderTextTexture(params(QString()), QSize(64, 32));
    check(empty.size() == QSize(64, 32) && empty.format() == QImage::Format_RGBA8888
              && measure(empty).coverage == 0.0,
          "empty text yields a transparent RGBA8888 canvas of the requested size");
    check(measure(nm::renderTextTexture(params(QStringLiteral("x"), 0.001), QSize(64, 64))).coverage == 0.0,
          "a font size that rounds to 0 px draws nothing");

    nm::TextTextureParams red = params(QStringLiteral("HHHH"), 0.3);
    red.color = QColor(255, 0, 0);
    const QImage image = nm::renderTextTexture(red, QSize(256, 256));
    bool solidIsRed = true;
    int solid = 0;
    for (int y = 0; y < image.height(); ++y) {
        const uchar* row = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            if (row[x * 4 + 3] != 255) continue;
            ++solid;
            solidIsRed = solidIsRed && row[x * 4] == 255 && row[x * 4 + 1] == 0 && row[x * 4 + 2] == 0;
        }
    }
    check(solid > 100 && solidIsRed, "opaque text pixels carry the exact fill colour (straight alpha)");
}

void testLayout() {
    const QSize canvas(512, 512);
    const int fontSize = 51; // round(0.1 * 512)
    nm::registerBundledFonts(kDataRoot);
    QFont font(QStringLiteral("Nunito"));
    font.setPixelSize(fontSize);
    font.setHintingPreference(QFont::PreferNoHinting);
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    // The bundled Nunito's wght axis defaults to 200 (ExtraLight), and
    // FreeType draws an unset axis at that default. The renderer sets the
    // CSS weight 400 on the axis, so the reference advance must too.
    font.setVariableAxis(QFont::Tag("wght"), 400.0f);
#endif
    const double advance = QFontMetricsF(font).horizontalAdvance(QStringLiteral("Hello"));

    nm::TextTextureParams p = params(QStringLiteral("Hello"));
    p.justify = QStringLiteral("left");
    const Ink left = measure(nm::renderTextTexture(p, canvas));
    p.justify = QStringLiteral("center");
    const Ink center = measure(nm::renderTextTexture(p, canvas));
    p.justify = QStringLiteral("right");
    const Ink right = measure(nm::renderTextTexture(p, canvas));
    std::printf("  advance=%.3f left.cx=%.3f center.cx=%.3f right.cx=%.3f cy=%.3f\n", advance, left.cx,
                center.cx, right.cx, center.cy);
    check(std::abs((left.cx - center.cx) - advance / 2) < 0.75
              && std::abs((center.cx - right.cx) - advance / 2) < 0.75,
          "left, center and right justification shift the line by half its advance each");
    check(left.left >= 256 && left.left <= 256 + fontSize / 5 && right.right <= 256
              && right.right >= 256 - fontSize / 5,
          "left text starts at posX, right text ends at posX (within the side bearing)");
    check(std::abs(center.coverage - left.coverage) < 1.0 && std::abs(center.cy - left.cy) < 0.01,
          "justification moves the line without changing it");

    p.justify = QStringLiteral("center");
    p.posX = 0.25;
    p.posY = 0.75;
    const Ink moved = measure(nm::renderTextTexture(p, canvas));
    check(std::abs((moved.cx - center.cx) + 128) < 0.75 && std::abs((moved.cy - center.cy) - 128) < 0.75,
          "posX and posY place the origin in canvas pixels from the top left");

    // The middle baseline centers the em box on posY: glyphs of "Hello"
    // sit above the middle, descender-free, so the ink centroid is above
    // posY but within half the font size.
    check(center.cy < 256 && center.cy > 256 - fontSize / 2.0 && center.top < 256 && center.bottom > 256,
          "textBaseline middle straddles posY");

    nm::TextTextureParams two = params(QStringLiteral("HH\nHH"));
    const QImage lines = nm::renderTextTexture(two, canvas);
    const Ink upper = measure(lines, QRect(0, 0, 512, 256));
    const Ink lower = measure(lines, QRect(0, 256, 512, 256));
    check(std::abs((lower.cy - upper.cy) - fontSize * 1.2) < 0.75 && std::abs((upper.cy + lower.cy) / 2 - 256) < 20,
          "lines are 1.2 x the font size apart and centered as a block on posY");

    nm::TextTextureParams turned = params(QStringLiteral("Hello"));
    turned.rotation = 90;
    const Ink rotated = measure(nm::renderTextTexture(turned, canvas));
    // Clockwise in y-down canvas space: an offset (dx, dy) from the origin
    // moves to (-dy, dx). Centroids are pixel indices; pixel i covers
    // [i, i + 1), so its centre is at i + 0.5.
    std::printf("  rotated cx=%.3f cy=%.3f\n", rotated.cx, rotated.cy);
    check(std::abs((rotated.right - rotated.left) - (center.bottom - center.top)) <= 2
              && std::abs((rotated.bottom - rotated.top) - (center.right - center.left)) <= 2
              && std::abs((rotated.cx + 0.5) - (256 - (center.cy + 0.5 - 256))) < 0.75
              && std::abs((rotated.cy + 0.5) - (256 + (center.cx + 0.5 - 256))) < 0.75,
          "rotation turns the text clockwise about its origin");
}

void testStyles() {
    const QSize canvas(512, 512);
    nm::TextTextureParams regular = params(QStringLiteral("Weight"), 0.2);
    nm::TextTextureParams bold = regular;
    bold.style = QStringLiteral("Bold");
    nm::TextTextureParams extraBold = regular;
    extraBold.style = QStringLiteral("Condensed ExtraBold");
    nm::TextTextureParams light = regular;
    light.style = QStringLiteral("ExtraLight");
    nm::TextTextureParams italic = regular;
    italic.style = QStringLiteral("Italic");
    const double r = measure(nm::renderTextTexture(regular, canvas)).coverage;
    const double b = measure(nm::renderTextTexture(bold, canvas)).coverage;
    const double xb = measure(nm::renderTextTexture(extraBold, canvas)).coverage;
    const double l = measure(nm::renderTextTexture(light, canvas)).coverage;
    const QImage slanted = nm::renderTextTexture(italic, canvas);
    std::printf("  coverage ExtraLight=%.1f Regular=%.1f Bold=%.1f ExtraBold=%.1f\n", l, r, b, xb);
    check(l < r && r < b && b < xb, "style labels map to increasing weights (ExtraLight < Regular < Bold < ExtraBold)");
    check(slanted != nm::renderTextTexture(regular, canvas), "an Italic style label changes the glyphs");
}

void testGenericFamilies() {
    for (const char* family : {"serif", "sans-serif", "monospace", "cursive", "fantasy", "system-ui",
                               "No Such Family 7f3a"}) {
        nm::TextTextureParams p = params(QStringLiteral("Aa"), 0.3);
        p.font = QString::fromLatin1(family);
        const QString label = QStringLiteral("family '%1' falls back to an installed font and draws").arg(p.font);
        check(measure(nm::renderTextTexture(p, QSize(128, 128))).coverage > 50.0, qPrintable(label));
    }
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
QFont nunito(int pixelSize, float wght) {
    QFont font(QStringLiteral("Nunito"));
    font.setPixelSize(pixelSize);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setKerning(true);
    font.setVariableAxis(QFont::Tag("wght"), wght);
    return font;
}

// Kerning variation deltas in font units, pinned to HarfBuzz (uharfbuzz
// 0.x shaping at scale upem * 1024: kern advance at wght minus kern advance
// at the default instance, wght 200) and to a fontTools walk of the same
// tables; the two agree within 0.0005 units. The pins are the walk values.
void testKerningVariationDeltas() {
    nm::registerBundledFonts(kDataRoot);
    const QRawFont raw = QRawFont::fromFont(nunito(102, 400.0f));
    struct Pin {
        const char* pair;
        double at300, at400, at700, at800;
    };
    const Pin pins[] = {
        {"vy", 9.155273, 18.793945, 40.0, 43.132324},
        {"Wo", -1.831055, -3.758789, -8.0, -10.192627},
        {"Te", -0.686646, -1.409546, -3.0, -3.626465},
        {"th", 2.975464, 6.108032, 13.0, 15.192627},
        {"wo", -1.144409, -2.349243, -5.0, -6.566162},
        {"AV", -2.288818, -4.698486, -10.0, -13.132324},
        {"Bo", 0.0, 0.0, 0.0, 0.0},
        {"ld", 0.0, 0.0, 0.0, 0.0},
    };
    bool pinned = true;
    for (const Pin& pin : pins) {
        const QList<quint32> glyphs = raw.glyphIndexesForString(QString::fromLatin1(pin.pair));
        const double expected[] = {pin.at300, pin.at400, pin.at700, pin.at800};
        const double weights[] = {300.0, 400.0, 700.0, 800.0};
        for (int w = 0; w < 4; ++w) {
            const std::vector<double> d = nm::detail::kerningVariationDeltas(raw, glyphs, weights[w]);
            const bool ok = d.size() == 2 && std::abs(d[0] - expected[w]) < 0.001 && d[1] == 0.0;
            if (!ok) {
                std::printf("  %s at %.0f: got %.6f, %.6f; expected %.6f, 0\n", pin.pair, weights[w],
                            d.size() > 0 ? d[0] : -1.0, d.size() > 1 ? d[1] : -1.0, expected[w]);
            }
            pinned = pinned && ok;
        }
    }
    check(pinned, "kerning variation deltas of Nunito pairs match HarfBuzz at wght 300, 400, 700 and 800");

    const QList<quint32> heavy = raw.glyphIndexesForString(QStringLiteral("Heavy"));
    const std::vector<double> heavyDeltas = nm::detail::kerningVariationDeltas(raw, heavy, 800.0);
    check(heavyDeltas.size() == 5 && heavyDeltas[0] == 0.0 && heavyDeltas[1] == 0.0 && heavyDeltas[2] == 0.0
              && std::abs(heavyDeltas[3] - 43.132324) < 0.001 && heavyDeltas[4] == 0.0,
          "in \"Heavy\" at wght 800 only the v of the vy pair carries a delta");
    const std::vector<double> atDefault = nm::detail::kerningVariationDeltas(raw, heavy, 200.0);
    const std::vector<double> atMaximum = nm::detail::kerningVariationDeltas(raw, heavy, 1000.0);
    const std::vector<double> beyond = nm::detail::kerningVariationDeltas(raw, heavy, 1400.0);
    check(atDefault == std::vector<double>(5, 0.0), "the default instance (wght 200) has no deltas");
    check(beyond == atMaximum && atMaximum[3] > heavyDeltas[3], "wght beyond the axis maximum clamps to it");

    check(nm::detail::kerningVariationDeltas(QRawFont(), heavy, 800.0) == std::vector<double>(5, 0.0)
              && nm::detail::kerningVariationDeltas(raw, heavy.mid(3, 1), 800.0) == std::vector<double>(1, 0.0),
          "an invalid font or a single glyph gets zero deltas");
}

// `a` and `b` have the same elements, exactly.
bool identicalElements(const QPainterPath& a, const QPainterPath& b) {
    if (a.elementCount() != b.elementCount()) return false;
    for (int i = 0; i < a.elementCount(); ++i) {
        const QPainterPath::Element e = a.elementAt(i);
        const QPainterPath::Element f = b.elementAt(i);
        if (e.type != f.type || e.x != f.x || e.y != f.y) return false;
    }
    return true;
}

// detail::textLinePath against QPainterPath::addText, element for element.
void testLinePath() {
    nm::registerBundledFonts(kDataRoot);
    const QPointF origins[] = {{0.0, 0.0}, {-71.8046875, 6.3}, {-150.333, -12.40625}, {37.017, 91.99}};
    bool zeroDeltaEqual = true;
    for (const QPointF& origin : origins) {
        const std::pair<QFont, QString> lines[] = {
            {nunito(102, 700.0f), QStringLiteral("Bold")},
            {nunito(61, 200.0f), QStringLiteral("Heavy vy AV")},
            {QFont(), QStringLiteral("Plain text, 12 AV")},
        };
        for (const auto& [font, text] : lines) {
            QPainterPath expected;
            expected.addText(origin, font, text);
            double delta = -1.0;
            const QPainterPath got = nm::detail::textLinePath(font, text, origin, &delta);
            zeroDeltaEqual = zeroDeltaEqual && delta == 0.0 && identicalElements(got, expected);
        }
    }
    check(zeroDeltaEqual, "without kerning deltas the line outline equals QPainterPath::addText exactly");

    // "Heavy" at wght 800: H, e, a and v stay where addText puts them (the
    // glyph-run construction truncates the origin to 1/64 px as addText
    // does); y moves right by the vy delta, 43.132324 units at 102 px =
    // 4.39950 px.
    const QFont heavy = nunito(102, 800.0f);
    const QString text = QStringLiteral("Heavy");
    const QPointF origin(-150.337, 11.1234);
    QPainterPath expected;
    expected.addText(origin, heavy, text);
    double delta = 0.0;
    const QPainterPath got = nm::detail::textLinePath(heavy, text, origin, &delta);
    const QRawFont raw = QRawFont::fromFont(heavy);
    const double shift = 43.132324 * 102.0 / raw.unitsPerEm();
    std::printf("  Heavy: elements=%d delta=%.5f px\n", expected.elementCount(), delta);
    check(std::abs(delta - shift) < 1e-6, "the line advance grows by the vy delta in pixels");

    // Where each glyph's elements end, and the largest coordinate involved.
    const QList<quint32> glyphs = raw.glyphIndexesForString(text);
    std::vector<int> glyphEnd;
    double magnitude = 0.0;
    for (const quint32 glyph : glyphs) {
        const QPainterPath local = raw.pathForGlyph(glyph);
        glyphEnd.push_back((glyphEnd.empty() ? 0 : glyphEnd.back()) + local.elementCount());
        for (int i = 0; i < local.elementCount(); ++i) {
            magnitude = std::max({magnitude, std::abs(local.elementAt(i).x), std::abs(local.elementAt(i).y)});
        }
    }
    bool singlePrecision = true;
    for (int i = 0; i < expected.elementCount(); ++i) {
        const QPainterPath::Element e = expected.elementAt(i);
        singlePrecision = singlePrecision && double(float(e.x)) == e.x && double(float(e.y)) == e.y;
        magnitude = std::max({magnitude, std::abs(e.x), std::abs(e.y)});
    }

    // Both paths hold the font engine's outlines: addText asks the engine for
    // each glyph at its position, the renderer asks for each glyph at the
    // origin and translates that outline in double precision. CoreText and
    // FreeType compute outlines in double precision; the paths agree within
    // 3e-14 px on macOS. DirectWrite returns them as D2D1_POINT_2F, single
    // precision (Qt 6.10 QWindowsFontEngineDirectWrite::addGlyphsToPath passes
    // the glyph positions to GetGlyphRunOutline as FLOAT offsets), so one path
    // rounds a point at its position on the line and the other at its
    // position in the glyph. Allowing each path two roundings to single
    // precision (the scaled point, then the point plus its offset), each
    // within half a unit in the last place of a value below 2^k px, the paths
    // agree within 4 * 2^(k - 25) = 2^(k - 23) px: 2^-15 = 3.05e-5 px here,
    // where every coordinate is below 256 px. A glyph moved by one QFixed
    // step (1/64 px) would be 512 times that. The engine is single precision
    // when every coordinate addText returns is a float.
    const double tolerance = singlePrecision ? std::ldexp(1.0, std::ilogb(magnitude) + 1 - 23) : 1e-9;
    struct Worst {
        double deviation = 0.0;
        int element = -1;
    };
    Worst others;
    Worst moved;
    const int count = expected.elementCount();
    const bool sameStructure = glyphs.size() == text.size() && got.elementCount() == count
        && glyphEnd.back() == count;
    const int yFirst = sameStructure ? glyphEnd[glyphEnd.size() - 2] : count;
    bool sameTypes = sameStructure;
    for (int i = 0; sameStructure && i < count; ++i) {
        const QPainterPath::Element e = expected.elementAt(i);
        const QPainterPath::Element f = got.elementAt(i);
        sameTypes = sameTypes && e.type == f.type;
        const double deviation = std::max(std::abs(f.x - e.x - (i >= yFirst ? delta : 0.0)), std::abs(f.y - e.y));
        Worst& worst = i >= yFirst ? moved : others;
        if (deviation > worst.deviation) worst = {deviation, i};
    }
    const auto glyphOf = [&](int element) {
        int g = 0;
        while (g + 1 < static_cast<int>(glyphEnd.size()) && element >= glyphEnd[static_cast<std::size_t>(g)]) ++g;
        return text.at(g).toLatin1();
    };
    std::printf("  Heavy: got elements=%d, glyph elements=%d, element types %s\n", got.elementCount(),
                glyphEnd.empty() ? 0 : glyphEnd.back(), sameTypes ? "equal" : "differ");
    std::printf("  Heavy: outline coordinates %s precision, largest |coordinate| %.3f px, tolerance %.3g px\n",
                singlePrecision ? "single" : "double", magnitude, tolerance);
    if (sameStructure) {
        const QPainterPath::Element o = expected.elementAt(std::max(others.element, 0));
        const QPainterPath::Element m = expected.elementAt(std::max(moved.element, 0));
        std::printf("  Heavy: H, e, a, v largest |shift| %.3g px at element %d (%c, %.4f, %.4f); "
                    "y largest |shift - delta| %.3g px at element %d (%c, %.4f, %.4f)\n",
                    others.deviation, others.element, glyphOf(std::max(others.element, 0)), o.x, o.y,
                    moved.deviation, moved.element, glyphOf(std::max(moved.element, 0)), m.x, m.y);
    }
    check(sameStructure && sameTypes && others.deviation <= tolerance && moved.deviation <= tolerance,
          "only the glyph after the kerned pair moves, by the delta (within the engine's outline precision)");
}
#endif

void testGraphUpload(nm::EffectRegistry& registry) {
    const QSize size(128, 128);
    nm::Backend backend;
    backend.setup(nullptr, kDataRoot, size);
    const nm::Graph graph = nm::compileGraph(QStringLiteral(
        "search synth, filter\n"
        "solid(color: #000000).text(text: \"TOP\", size: 0.2, posY: 0.2, color: #00ff00).write(o0)\n"
        "render(o0)"), registry);

    backend.render(graph, 0.0);
    const Ink before = measure(backend.readSurface().convertToFormat(QImage::Format_RGBA8888));
    const QStringList ids = nm::updateTextTextures(backend, graph, kDataRoot, size);
    check(ids == QStringList{QStringLiteral("textTex_step_1")}, "updateTextTextures uploads textTex_step_N");
    backend.render(graph, 0.0);
    const QImage after = backend.readSurface().convertToFormat(QImage::Format_RGBA8888);
    int green = 0;
    int greenTop = 0;
    for (int y = 0; y < after.height(); ++y) {
        const uchar* row = after.constScanLine(y);
        for (int x = 0; x < after.width(); ++x) {
            if (row[x * 4 + 1] > 200 && row[x * 4] < 40 && row[x * 4 + 2] < 40) {
                ++green;
                if (y < 64) ++greenTop;
            }
        }
    }
    std::printf("  green pixels=%d in top half=%d\n", green, greenTop);
    check(before.coverage > 0 && green > 50, "the uploaded text composites over the input");
    check(greenTop == green, "text at posY 0.2 appears in the top of the read-back image (flipY = true)");

    const nm::Graph matte = nm::compileGraph(QStringLiteral(
        "search synth, filter\n"
        "solid(color: #000000).text(text: \"\", matteColor: #ff0000, matteOpacity: 1).write(o0)\n"
        "render(o0)"), registry);
    nm::updateTextTextures(backend, matte, kDataRoot, size);
    backend.render(matte, 0.0);
    const QRgb corner = backend.readSurface().pixel(4, 4);
    check(qRed(corner) > 250 && qGreen(corner) < 5 && qBlue(corner) < 5,
          "the matte comes from the shader, not from the canvas");
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    nm::EffectRegistry registry;
    registry.loadAll(kDataRoot);

    testParameterReading();
    testBundledFont();
    testCanvasBasics();
    testLayout();
    testStyles();
    testGenericFamilies();
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    testKerningVariationDeltas();
    testLinePath();
#endif
    testGraphUpload(registry);

    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
