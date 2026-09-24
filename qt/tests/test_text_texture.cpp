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

#include <cmath>
#include <cstdio>

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
    testGraphUpload(registry);

    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
