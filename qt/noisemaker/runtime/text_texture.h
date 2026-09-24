#pragma once

// text_texture.h -- CPU text rasterization for filter/text. The reference
// hosts draw the text on a 2D canvas and upload it as textTex_step_N
// (reference demo UIController._renderTextToCanvas, Noisedeck
// TextCanvasRenderer.renderTextToCanvas). This module draws the same layout
// with QPainter:
//
//   - a transparent canvas of the render size; the matte is the shader's job;
//   - font size round(size * min(width, height)) pixels, line height 1.2 x
//     the font size, lines split on "\n";
//   - the origin at (posX * width, posY * height), rotated clockwise by
//     `rotation` degrees; the lines centered vertically on that origin;
//   - canvas textBaseline "middle" (the middle of the em box, from the OS/2
//     typographic ascent and descent normalized to one em, as Chromium
//     computes it) and textAlign left, center or right;
//   - the fill colour rgba(round(r * 255), round(g * 255), round(b * 255), 1);
//   - the `style` label mapped to a weight and italic flag the way Noisedeck
//     maps it ("Bold" -> 700, "ExtraLight" -> 200, "... Italic" -> italic).
//
// Glyphs are filled as outlines (QPainterPath) without hinting, so the same
// font file rasterizes the same way on every platform. Qt shapes a variable
// font with the default instance's GPOS kerning; a browser (HarfBuzz)
// applies the kerning's variation deltas for the requested weight as well.
// The renderer adds those deltas itself (detail::kerningVariationDeltas). Pixel differences
// from a browser canvas remain at glyph edges: browsers rasterize glyph
// masks with the platform scaler. parity/check_text_canvas.mjs measures them
// against Chromium.
//
// The engine ships Nunito, the effect's default family, under
// <dataRoot>/fonts. Generic CSS families (serif, sans-serif, monospace,
// cursive, fantasy, system-ui) resolve to the platform's default family for
// the matching QFont::StyleHint, which need not be the family a browser
// picks. Other names resolve through QFontDatabase, which falls back to a
// system family when the name is not installed.

#include <QColor>
#include <QFont>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QRawFont>
#include <QSize>
#include <QString>
#include <QStringList>

#include <vector>

namespace nm {

class Backend;
struct Graph;

struct TextTextureParams {
    QString text = QStringLiteral("Hello World");
    QString font = QStringLiteral("Nunito");
    QString style;              // e.g. "Bold Italic"; empty selects regular
    double size = 0.1;          // fraction of min(canvas width, canvas height)
    double posX = 0.5;          // fraction of canvas width
    double posY = 0.5;          // fraction of canvas height, from the top
    double rotation = 0.0;      // degrees, clockwise
    QColor color = QColor(255, 255, 255);
    QString justify = QStringLiteral("center"); // left | center | right
};

// Reads the text parameters of a filter/text pass (pass.uniforms). Absent
// or malformed values keep the filter/text definition defaults. `color`
// accepts the compiled [r, g, b(, a)] array in 0..1 or a "#rrggbb" string.
TextTextureParams textTextureParams(const QJsonObject& uniforms);

// Registers every .ttf/.otf file under <dataRoot>/fonts with
// QFontDatabase. Needs a QGuiApplication. A file registers once per
// process; returns the number of files newly registered by this call.
int registerBundledFonts(const QString& dataRoot);

// Draws `params` on a transparent canvas of `canvasSize`. Returns straight
// (non-premultiplied) RGBA8888, row 0 at the top, as a canvas reads back.
// Returns a null image for an empty size.
QImage renderTextTexture(const TextTextureParams& params, QSize canvasSize);

// For every filter/text pass in `graph`: registers the fonts under
// `dataRoot`, draws the pass's parameters at `canvasSize` and uploads the
// result as its textTex_step_N with flipY = true, as the reference hosts do.
// Call after compiling, after Backend::resize() (the canvas has the render
// size), and after changing a text parameter. A host GL context must be
// current. Returns the uploaded texture ids.
QStringList updateTextTextures(Backend& backend, const Graph& graph, const QString& dataRoot,
                               QSize canvasSize);

namespace detail {
// Kerning variation deltas, in font design units, for `glyphs` in logical
// order at the user-space wght axis value `wght`: for each glyph, the change
// of its GPOS 'kern' pair adjustment (the XAdvance VariationIndex device of
// the pair's value records) between the default instance and `wght`. This
// is what HarfBuzz adds and Qt's shaping leaves out. Supports PairPos
// formats 1 and 2 (also inside Extension lookups), the IgnoreBaseGlyphs,
// IgnoreLigatures and IgnoreMarks lookup flags, the fvar/avar
// normalization and the GDEF ItemVariationStore, as HarfBuzz evaluates
// them. Returns zeros for a font without fvar, GPOS or GDEF variation data,
// and skips lookups that use mark attachment classes or mark filtering sets.
std::vector<double> kerningVariationDeltas(const QRawFont& font, const QList<quint32>& glyphs, double wght);

// One line of text as renderTextTexture draws it: the glyphs of a
// design-metrics QTextLayout (the layout QPainterPath::addText uses), each
// advance increased by its kerning variation delta for the font's wght axis
// value, with the left end of the baseline at `origin` truncated to 1/64 px
// as QPainterPath::addText does. `deltaAdvance` receives the added advance
// in pixels. Without deltas the path equals addText(origin, font, line).
QPainterPath textLinePath(const QFont& font, const QString& line, QPointF origin, double* deltaAdvance = nullptr);
} // namespace detail

} // namespace nm
