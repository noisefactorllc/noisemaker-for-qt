#pragma once

// stroke_canvas.h -- CPU model of the browser 2D canvas that the reference
// asyncInit effects (filter/fibers, filter/scratches, filter/strayHair) draw
// on. The reference traces worms on the CPU and strokes each step as a
// round-capped line segment on an HTMLCanvasElement 2D context
// (shaders/src/cpu/wormTracer.js), then uploads the canvas as the overlay
// texture. This class reproduces that canvas pixel for pixel:
//
//   - Blink culls a stroke whose bounding box, outset by lineWidth / 2,
//     does not intersect the canvas (BaseRenderingContext2D dirty rect).
//   - "rgba(r, g, b, a)" is parsed to an 8-bit colour: alpha becomes
//     floor(a * 255 + 0.5). Skia converts it to float as c * (1 / 255.0f)
//     and premultiplies in float.
//   - The canvas is GPU accelerated. Skia Graphite draws a stroked line
//     with AnalyticRRectRenderStep: a 36-vertex instance (four corners of
//     nine vertices, a 69-index triangle strip) whose fragment shader
//     computes analytic coverage from interpolated edge distances
//     (src/sksl/sksl_graphite_vert.sksl analytic_rrect_vertex_fn,
//     sksl_graphite_frag.sksl analytic_rrect_coverage_fn). This model runs
//     the same vertex and fragment arithmetic in float, snaps vertex
//     positions to the rasterizer's 1/256 pixel grid, rasterizes with the
//     top-left fill rule, and shades each pixel at most once per stroke
//     (Graphite's depth test rejects a second triangle of the same draw).
//   - Source-over blending in float: dst' = src * coverage +
//     dst * (1 - srcAlpha * coverage), stored as floor(v * 255 + 0.5).
//   - The WebGL upload with UNPACK_PREMULTIPLY_ALPHA_WEBGL false divides
//     by alpha as c / 255.0f * (1.0f / (a / 255.0f)) in float and rounds
//     the exact product with 255 to the nearest integer.
//
// Measured against Chromium 153 (headless shell, --use-angle=metal, Skia
// Graphite on Metal, Apple M4) by parity/check_async_overlay.mjs. Other
// browsers and GPU backends rasterize strokes differently; the goldens of
// this port are minted with that Chromium configuration.

#include <cstdint>
#include <vector>

namespace nm {

class StrokeCanvas {
public:
    // A transparent canvas. Width and height must be positive.
    StrokeCanvas(int width, int height);

    int width() const { return m_width; }
    int height() const { return m_height; }

    // clearRect(0, 0, width, height).
    void clear();

    // ctx.lineWidth = width. Ignores non-finite and non-positive values,
    // as the canvas does.
    void setLineWidth(double width);

    // ctx.strokeStyle = `rgba(${r}, ${g}, ${b}, ${alpha})`, the only style
    // form the reference tracer uses. Channels are clamped to 0..255 and
    // rounded; alpha is clamped to 0..1. A non-finite value makes the style
    // string invalid, so the canvas keeps its previous style.
    void setStrokeColor(double r, double g, double b, double alpha);

    // ctx.beginPath(); ctx.moveTo(x0, y0); ctx.lineTo(x1, y1); ctx.stroke()
    // with lineCap and lineJoin "round". Non-finite points draw nothing.
    void strokeLine(double x0, double y0, double x1, double y1);

    // Premultiplied RGBA8, row 0 at the top (the canvas backing store).
    const std::vector<std::uint8_t>& premultiplied() const { return m_pixels; }

    // Straight-alpha RGBA8, row 0 at the top: the bytes WebGL receives from
    // texImage2D(canvas) with UNPACK_PREMULTIPLY_ALPHA_WEBGL false.
    std::vector<std::uint8_t> unpremultipliedRgba8() const;

private:
    int m_width = 0;
    int m_height = 0;
    float m_lineWidth = 1.0f;
    float m_color[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // premultiplied
    std::vector<std::uint8_t> m_pixels;
    std::vector<std::uint8_t> m_shaded; // per-stroke "pixel already shaded" mask
};

// The WebGL upload conversion of premultiplied canvas pixels (RGBA8) to
// straight alpha, as unpremultipliedRgba8() applies it.
std::vector<std::uint8_t> unpremultiplyForUpload(const std::vector<std::uint8_t>& premultiplied);

} // namespace nm
