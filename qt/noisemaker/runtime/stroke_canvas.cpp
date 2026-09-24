#include "stroke_canvas.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace nm {

namespace {

// ---------------------------------------------------------------- instance
// Skia Graphite AnalyticRRectRenderStep, restricted to the geometry the
// reference tracer draws: a stroked line with round caps, identity
// transform. The instance attributes for that case are
//   xRadiiOrFlags = (-2, 1, strokeRadius, -1)   stroked line, round cap
//   radiiOrQuadXs = (0, 0, 0, 0)
//   ltrbOrQuadYs  = (x0, y0, x1, y1)
//   center        = (bounds center, kSolidInterior = 1, kComplexAAInsets = -1)
// (AnalyticRRectRenderStep::writeVertices: a line always takes the
// kComplexAAInsets path because strokeInset = -strokeRadius <= aaRadius).

struct TemplateVertex {
    int corner;
    float posX, posY;
    float normalX, normalY;
    float normalScale;  // +1 device outset, 0 outer anchor, -1 inset
    float centerWeight; // 1 for the center-fill vertex
};

constexpr int kCornerVertexCount = 9;
constexpr int kVertexCount = 4 * kCornerVertexCount;
constexpr int kIndexCount = 69;

// get_per_corner_vertex_attrs<kCornerID>() repeated for TL, TR, BR, BL.
std::array<TemplateVertex, kVertexCount> makeTemplate() {
    const float hr2 = 0.5f * 1.41421356f; // SK_FloatSqrt2
    std::array<TemplateVertex, kVertexCount> out{};
    for (int c = 0; c < 4; ++c) {
        const TemplateVertex corner[kCornerVertexCount] = {
            {c, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f},
            {c, 1.0f, 0.0f, hr2, hr2, 1.0f, 0.0f},
            {c, 0.0f, 1.0f, hr2, hr2, 1.0f, 0.0f},
            {c, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f},
            {c, 1.0f, 0.0f, hr2, hr2, 0.0f, 0.0f},
            {c, 0.0f, 1.0f, hr2, hr2, 0.0f, 0.0f},
            {c, 1.0f, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f},
            {c, 0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 0.0f},
            {c, 1.0f, 0.0f, 1.0f, 0.0f, -1.0f, 1.0f},
        };
        for (int i = 0; i < kCornerVertexCount; ++i) out[static_cast<size_t>(c * kCornerVertexCount + i)] = corner[i];
    }
    return out;
}

// write_index_buffer(): a triangle strip over the 36 vertices.
constexpr int kIndices[kIndexCount] = {
    0, 4, 1, 5, 2, 3, 5,  9, 13, 10, 14, 11, 12, 14,  18, 22, 19, 23, 20, 21, 23,
    27, 31, 28, 32, 29, 30, 32,  0, 4,
    4, 6, 5, 7,  13, 15, 14, 16,  22, 24, 23, 25,  31, 33, 32, 34,  4, 6,
    6, 8, 7,  7, 17,  15, 17, 16,  16, 26,  24, 26, 25,  25, 35,  33, 35, 34,  34, 8,  6};

struct Varyings {
    float jacobian[4];
    float edgeDistances[4];
    float strokeRadius;
    float joinStyle;
    float perPixelX;
    float perPixelY;
};

struct DeviceVertex {
    float x = 0.0f;
    float y = 0.0f;
    Varyings v{};
};

// The per-instance part of analytic_rrect_vertex_fn: identical for all 36
// vertices of a stroke, so it is evaluated once.
struct LineInstance {
    float xs[4];      // ltrb.LLRR, corners ordered TL, TR, BR, BL
    float ys[4];      // ltrb.TTBB
    float dx[4];      // normalized edge vectors, ordered L, T, R, B
    float dy[4];
    float edgeAA[4];
    float strokeRadius;
    float centerX;
    float centerY;
};

LineInstance makeLineInstance(float x0, float y0, float x1, float y1, float strokeRadius) {
    LineInstance line{};
    const float xs[4] = {x0, x0, x1, x1};
    const float ys[4] = {y0, y0, y1, y1};
    float edgeSquaredLen[4];
    float edgeMask[4];
    for (int i = 0; i < 4; ++i) {
        line.xs[i] = xs[i];
        line.ys[i] = ys[i];
        line.edgeAA[i] = 1.0f;
        const int w = (i + 3) % 4; // .wxyz
        float dx = xs[i] - xs[w];
        float dy = ys[i] - ys[w];
        const float invMag = 1.0f / std::max(std::fabs(dx), std::max(std::fabs(dy), 1.0f));
        dx *= invMag;
        dy *= invMag;
        line.dx[i] = dx;
        line.dy[i] = dy;
        edgeSquaredLen[i] = dx * dx + dy * dy;
        edgeMask[i] = edgeSquaredLen[i] > 0.0f ? 1.0f : 0.0f; // sign() of a non-negative value
    }
    {
        // A line has two empty edges (the caps); each takes the left-hand
        // normal of the adjacent edge. mix(a, b, t) = a + (b - a) * t. A
        // zero-length line (all four edges empty) never gets here: the
        // canvas draws nothing for it (strokeLine).
        float nx[4];
        float ny[4];
        float nl[4];
        float naa[4];
        for (int i = 0; i < 4; ++i) {
            const int j = (i + 1) % 4; // .yzwx
            const float edgeX = line.dy[j];
            const float edgeY = -line.dx[j];
            nx[i] = edgeX + (line.dx[i] - edgeX) * edgeMask[i];
            ny[i] = edgeY + (line.dy[i] - edgeY) * edgeMask[i];
            nl[i] = edgeSquaredLen[j] + (edgeSquaredLen[i] - edgeSquaredLen[j]) * edgeMask[i];
            naa[i] = line.edgeAA[j] + (line.edgeAA[i] - line.edgeAA[j]) * edgeMask[i];
        }
        for (int i = 0; i < 4; ++i) {
            line.dx[i] = nx[i];
            line.dy[i] = ny[i];
            edgeSquaredLen[i] = nl[i];
            line.edgeAA[i] = naa[i];
        }
    }
    for (int i = 0; i < 4; ++i) {
        const float inverseEdgeLen = 1.0f / std::sqrt(edgeSquaredLen[i]);
        line.dx[i] *= inverseEdgeLen;
        line.dy[i] *= inverseEdgeLen;
    }
    line.strokeRadius = strokeRadius;
    // bounds.center() of the line's bounding box.
    line.centerX = (std::min(x0, x1) + std::max(x0, x1)) * 0.5f;
    line.centerY = (std::min(y0, y1) + std::max(y0, y1)) * 0.5f;
    return line;
}

// The per-vertex part of analytic_rrect_vertex_fn.
DeviceVertex lineVertex(const TemplateVertex& t, const LineInstance& line) {
    const float kRoundScale = 0.41421356237f;
    const int cornerID = t.corner;
    const int nextID = (cornerID + 1) % 4;
    const float joinScale = kRoundScale; // round cap == round join

    const float xAxisX = -line.dx[nextID];
    const float xAxisY = -line.dy[nextID];
    const float yAxisX = line.dx[cornerID];
    const float yAxisY = line.dy[cornerID];

    float localX;
    float localY;
    if (t.normalScale < 0.0f) {
        // Inset vertices snap to the center (center.w < 0).
        localX = line.centerX;
        localY = line.centerY;
    } else {
        // (cornerRadii + strokeRadius) * (position + joinScale * position.yx),
        // then from the corner basis to local coordinates.
        const float px = line.strokeRadius * (t.posX + joinScale * t.posY);
        const float py = line.strokeRadius * (t.posY + joinScale * t.posX);
        localX = line.xs[cornerID] + xAxisX * px + yAxisX * py;
        localY = line.ys[cornerID] + xAxisY * px + yAxisY * py;
    }

    DeviceVertex out;
    for (int i = 0; i < 4; ++i) {
        out.v.edgeDistances[i] = line.dy[i] * (line.xs[i] - localX) - line.dx[i] * (line.ys[i] - localY);
    }
    out.x = localX;
    out.y = localY;
    if (t.normalScale > 0.0f) {
        // Device-space AA outset by one pixel along the corner normal.
        const float normalX = line.edgeAA[cornerID] * t.normalX;
        const float normalY = line.edgeAA[nextID] * t.normalY;
        // perp(-yAxis) and perp(xAxis), perp(v) = (-v.y, v.x).
        const float sumX = normalX * yAxisY + normalY * -xAxisY;
        const float sumY = normalX * -yAxisX + normalY * xAxisX;
        const float inverseLength = 1.0f / std::sqrt(sumX * sumX + sumY * sumY);
        out.x += sumX * inverseLength;
        out.y += sumY * inverseLength;
        out.v.perPixelY = -1.0f;
    } else {
        out.v.perPixelY = 0.0f;
    }
    out.v.perPixelX = t.centerWeight != 0.0f ? 1.0f : 0.0f;
    // The fragment shader works in the line's own basis.
    out.v.jacobian[0] = line.dy[0];
    out.v.jacobian[1] = -line.dy[1];
    out.v.jacobian[2] = -line.dx[0];
    out.v.jacobian[3] = line.dx[1];
    out.v.strokeRadius = line.strokeRadius;
    out.v.joinStyle = -1.0f;
    return out;
}

// $inverse_grad_len(localGrad, J) with J's columns (J0, J1) and (J2, J3).
float inverseGradLength(float gx, float gy, const float jacobian[4]) {
    const float a = gx * jacobian[0] + gy * jacobian[1];
    const float b = gx * jacobian[2] + gy * jacobian[3];
    return 1.0f / std::sqrt(a * a + b * b);
}

// analytic_rrect_coverage_fn for a stroked line (solid interior, round
// corners of radius 0 with a stroke radius).
float lineCoverage(const Varyings& v) {
    if (v.perPixelX > 0.0f) return 1.0f;
    const float* J = v.jacobian;
    const float invGradX = inverseGradLength(1.0f, 0.0f, J);
    const float invGradY = inverseGradLength(0.0f, 1.0f, J);
    const float s = v.strokeRadius;
    const float* e = v.edgeDistances;
    const float outerX = invGradX * (s + std::min(e[0], e[2]));
    const float outerY = invGradY * (s + std::min(e[1], e[3]));
    float distOuter = std::min(outerX, outerY);
    float distInner = -1.0f;

    const float dimX = invGradX * (e[0] + e[2] + 2.0f * s);
    const float dimY = invGradY * (e[1] + e[3] + 2.0f * s);
    const float scale = std::min(std::min(dimX, dimY), 1.0f);
    const float bias = 1.0f - 0.5f * scale;

    // $corner_distances: TL (L,T), TR (R,T), BR (R,B), BL (L,B).
    const int cornerEdges[4][2] = {{0, 1}, {2, 1}, {2, 3}, {0, 3}};
    const float flips[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
    for (int k = 0; k < 4; ++k) {
        const float u = 0.0f - e[cornerEdges[k][0]];
        const float w = 0.0f - e[cornerEdges[k][1]];
        if (!(u > 0.0f && w > 0.0f)) continue;
        if (!(s > 0.0f && v.joinStyle < 0.0f)) continue;
        // $elliptical_distance(uv * xyFlip, radii = 0, strokeRadius, J)
        const float uu = u * flips[k][0];
        const float ww = w * flips[k][1];
        const float invR2 = 1.0f / (0.0f * 0.0f + s * s);
        const float nu = invR2 * uu;
        const float nw = invR2 * ww;
        const float invGrad = inverseGradLength(nu, nw, J);
        const float f = 0.5f * invGrad * ((uu * nu + ww * nw) - 1.0f);
        const float width = 0.0f * s * invR2 * invGrad;
        distOuter = std::min(distOuter, width - f);
        // radii.x - strokeRadius <= 0: the inner curve collapsed.
        distInner = std::min(distInner, 1.0f);
    }

    const float outset = std::min(v.perPixelY, 0.0f);
    const float coverage = scale * (std::min(distOuter + outset, -distInner) + bias);
    return std::clamp(coverage, 0.0f, 1.0f);
}

// A texel of an RGBA8 render target read as float: c / 255.
struct Unorm8Table {
    float values[256];
    Unorm8Table() {
        for (int c = 0; c < 256; ++c) values[c] = static_cast<float>(c) / 255.0f;
    }
    float operator[](std::uint8_t c) const { return values[c]; }
};
const Unorm8Table kUnorm8ToFloat;

std::uint8_t toUnorm8(float value) {
    const double clamped = std::clamp(static_cast<double>(value), 0.0, 1.0);
    return static_cast<std::uint8_t>(std::floor(clamped * 255.0 + 0.5));
}

// Rasterizer fixed point: 8 bits of subpixel precision.
constexpr double kSubpixel = 256.0;

// Device coordinate -> NDC (Graphite's rtAdjust, fused) -> viewport ->
// the 1/256 pixel grid, rounding half up.
long long snapX(float x, float dimension) {
    const float scale = 2.0f / dimension;
    const float half = dimension * 0.5f;
    const float ndc = std::fma(x, scale, -1.0f);
    const float back = std::fma(ndc, half, half);
    return static_cast<long long>(std::floor(static_cast<double>(back) * kSubpixel + 0.5));
}

long long snapY(float y, float dimension) {
    const float scale = 2.0f / dimension;
    const float half = dimension * 0.5f;
    const float ndc = std::fma(y, -scale, 1.0f);
    const float back = std::fma(-ndc, half, half);
    return static_cast<long long>(std::floor(static_cast<double>(back) * kSubpixel + 0.5));
}

// A top or left edge of a triangle with its interior on the right (y down).
bool isTopLeft(long long dx, long long dy) {
    return dy < 0 || (dy == 0 && dx > 0);
}

} // namespace

StrokeCanvas::StrokeCanvas(int width, int height) : m_width(width), m_height(height) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("nm::StrokeCanvas: width and height must be positive");
    }
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    m_pixels.assign(count * 4, 0);
    m_shaded.assign(count, 0);
}

void StrokeCanvas::clear() {
    std::fill(m_pixels.begin(), m_pixels.end(), std::uint8_t{0});
}

void StrokeCanvas::setLineWidth(double width) {
    if (!std::isfinite(width) || width <= 0.0) return;
    m_lineWidth = static_cast<float>(width);
}

void StrokeCanvas::setStrokeColor(double r, double g, double b, double alpha) {
    if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(alpha)) return;
    auto channel = [](double value) {
        return std::floor(std::clamp(value, 0.0, 255.0) + 0.5);
    };
    const double alpha8 = std::floor(std::clamp(alpha, 0.0, 1.0) * 255.0 + 0.5);
    // SkColor4f::FromColor: c * (1 / 255.0f); SkColor4f::premul().
    const float inv255 = 1.0f / 255.0f;
    const float a = static_cast<float>(alpha8) * inv255;
    m_color[0] = static_cast<float>(channel(r)) * inv255 * a;
    m_color[1] = static_cast<float>(channel(g)) * inv255 * a;
    m_color[2] = static_cast<float>(channel(b)) * inv255 * a;
    m_color[3] = a;
}

void StrokeCanvas::strokeLine(double x0d, double y0d, double x1d, double y1d) {
    if (!std::isfinite(x0d) || !std::isfinite(y0d) || !std::isfinite(x1d) || !std::isfinite(y1d)) return;
    // Nothing to draw with a transparent source-over paint.
    if (m_color[3] == 0.0f) return;
    const float x0 = static_cast<float>(x0d);
    const float y0 = static_cast<float>(y0d);
    const float x1 = static_cast<float>(x1d);
    const float y1 = static_cast<float>(y1d);
    // The canvas draws nothing for a segment whose end points are equal
    // once converted to the path's float coordinates (measured at widths 1
    // and 3), while a segment one float ulp long draws a round dot. A length
    // whose square underflows would give the GPU NaN vertices, which
    // rasterize nothing.
    if (!((x0 - x1) * (x0 - x1) + (y0 - y1) * (y0 - y1) > 0.0f)) return;
    const float strokeRadius = 0.5f * m_lineWidth;

    // Blink InflateStrokeRect + ComputeDirtyRect: the path bounds outset by
    // lineWidth / 2, rounded out, must intersect the canvas.
    {
        const float left = std::min(x0, x1) - strokeRadius;
        const float top = std::min(y0, y1) - strokeRadius;
        const float width = (std::max(x0, x1) - std::min(x0, x1)) + 2.0f * strokeRadius;
        const float height = (std::max(y0, y1) - std::min(y0, y1)) + 2.0f * strokeRadius;
        const double l = std::floor(static_cast<double>(left));
        const double t = std::floor(static_cast<double>(top));
        const double r = std::ceil(static_cast<double>(left + width));
        const double b = std::ceil(static_cast<double>(top + height));
        if (r <= 0.0 || b <= 0.0 || l >= static_cast<double>(m_width) || t >= static_cast<double>(m_height)) return;
    }

    static const std::array<TemplateVertex, kVertexCount> kTemplate = makeTemplate();
    const LineInstance line = makeLineInstance(x0, y0, x1, y1, strokeRadius);

    DeviceVertex vertices[kVertexCount];
    long long fixedX[kVertexCount];
    long long fixedY[kVertexCount];
    const float widthF = static_cast<float>(m_width);
    const float heightF = static_cast<float>(m_height);
    long long minX = 0;
    long long maxX = 0;
    long long minY = 0;
    long long maxY = 0;
    for (int i = 0; i < kVertexCount; ++i) {
        vertices[i] = lineVertex(kTemplate[static_cast<size_t>(i)], line);
        fixedX[i] = snapX(vertices[i].x, widthF);
        fixedY[i] = snapY(vertices[i].y, heightF);
        if (i == 0 || fixedX[i] < minX) minX = fixedX[i];
        if (i == 0 || fixedX[i] > maxX) maxX = fixedX[i];
        if (i == 0 || fixedY[i] < minY) minY = fixedY[i];
        if (i == 0 || fixedY[i] > maxY) maxY = fixedY[i];
    }
    // Pixels whose centers can lie inside the instance, clipped to the canvas.
    auto firstPixel = [](long long fixedMin) {
        return static_cast<long long>(std::ceil((static_cast<double>(fixedMin) - 128.0) / kSubpixel));
    };
    auto lastPixel = [](long long fixedMax) {
        return static_cast<long long>(std::floor((static_cast<double>(fixedMax) - 128.0) / kSubpixel));
    };
    const int boxX0 = static_cast<int>(std::max<long long>(0, firstPixel(minX)));
    const int boxY0 = static_cast<int>(std::max<long long>(0, firstPixel(minY)));
    const int boxX1 = static_cast<int>(std::min<long long>(m_width - 1, lastPixel(maxX)));
    const int boxY1 = static_cast<int>(std::min<long long>(m_height - 1, lastPixel(maxY)));
    if (boxX0 > boxX1 || boxY0 > boxY1) return;

    for (int t = 0; t + 2 < kIndexCount; ++t) {
        int ia = kIndices[t];
        int ib = kIndices[t + 1];
        int ic = kIndices[t + 2];
        long long ax = fixedX[ia], ay = fixedY[ia];
        long long bx = fixedX[ib], by = fixedY[ib];
        long long cx = fixedX[ic], cy = fixedY[ic];
        long long area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (area == 0) continue;
        if (area < 0) {
            std::swap(ib, ic);
            std::swap(bx, cx);
            std::swap(by, cy);
            area = -area;
        }
        const Varyings& va = vertices[ia].v;
        const Varyings& vb = vertices[ib].v;
        const Varyings& vc = vertices[ic].v;
        const bool topLeftAB = isTopLeft(bx - ax, by - ay);
        const bool topLeftBC = isTopLeft(cx - bx, cy - by);
        const bool topLeftCA = isTopLeft(ax - cx, ay - cy);
        const int px0 = static_cast<int>(std::max<long long>(boxX0, firstPixel(std::min({ax, bx, cx}))));
        const int px1 = static_cast<int>(std::min<long long>(boxX1, lastPixel(std::max({ax, bx, cx}))));
        const int py0 = static_cast<int>(std::max<long long>(boxY0, firstPixel(std::min({ay, by, cy}))));
        const int py1 = static_cast<int>(std::min<long long>(boxY1, lastPixel(std::max({ay, by, cy}))));
        const float areaF = static_cast<float>(area);
        // Edge functions at the first pixel center of the box, and their
        // steps per pixel (256 fixed-point units).
        const long long startX = static_cast<long long>(px0) * 256 + 128;
        const long long startY = static_cast<long long>(py0) * 256 + 128;
        long long rowC = (bx - ax) * (startY - ay) - (by - ay) * (startX - ax);
        long long rowA = (cx - bx) * (startY - by) - (cy - by) * (startX - bx);
        long long rowB = (ax - cx) * (startY - cy) - (ay - cy) * (startX - cx);
        const long long stepXC = -(by - ay) * 256, stepYC = (bx - ax) * 256;
        const long long stepXA = -(cy - by) * 256, stepYA = (cx - bx) * 256;
        const long long stepXB = -(ay - cy) * 256, stepYB = (ax - cx) * 256;
        for (int y = py0; y <= py1; ++y, rowC += stepYC, rowA += stepYA, rowB += stepYB) {
            long long wC = rowC;
            long long wA = rowA;
            long long wB = rowB;
            for (int x = px0; x <= px1; ++x, wC += stepXC, wA += stepXA, wB += stepXB) {
                if (wC < 0 || (wC == 0 && !topLeftAB)) continue;
                if (wA < 0 || (wA == 0 && !topLeftBC)) continue;
                if (wB < 0 || (wB == 0 && !topLeftCA)) continue;
                const size_t index = static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x);
                if (m_shaded[index]) continue;
                m_shaded[index] = 1;

                const float b0 = static_cast<float>(wA) / areaF;
                const float b1 = static_cast<float>(wB) / areaF;
                const float b2 = static_cast<float>(wC) / areaF;
                Varyings v;
                for (int i = 0; i < 4; ++i) {
                    v.jacobian[i] = b0 * va.jacobian[i] + b1 * vb.jacobian[i] + b2 * vc.jacobian[i];
                    v.edgeDistances[i] = b0 * va.edgeDistances[i] + b1 * vb.edgeDistances[i]
                        + b2 * vc.edgeDistances[i];
                }
                v.strokeRadius = b0 * va.strokeRadius + b1 * vb.strokeRadius + b2 * vc.strokeRadius;
                v.joinStyle = b0 * va.joinStyle + b1 * vb.joinStyle + b2 * vc.joinStyle;
                v.perPixelX = b0 * va.perPixelX + b1 * vb.perPixelX + b2 * vc.perPixelX;
                v.perPixelY = b0 * va.perPixelY + b1 * vb.perPixelY + b2 * vc.perPixelY;
                const float coverage = lineCoverage(v);

                std::uint8_t* pixel = &m_pixels[index * 4];
                float source[4];
                for (int i = 0; i < 4; ++i) source[i] = m_color[i] * coverage;
                const float inverseAlpha = 1.0f - source[3];
                for (int i = 0; i < 4; ++i) {
                    pixel[i] = toUnorm8(source[i] + kUnorm8ToFloat[pixel[i]] * inverseAlpha);
                }
            }
        }
    }
    for (int y = boxY0; y <= boxY1; ++y) {
        std::fill_n(m_shaded.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(boxX0)),
                    boxX1 - boxX0 + 1, std::uint8_t{0});
    }
}

std::vector<std::uint8_t> StrokeCanvas::unpremultipliedRgba8() const {
    return unpremultiplyForUpload(m_pixels);
}

std::vector<std::uint8_t> unpremultiplyForUpload(const std::vector<std::uint8_t>& premultiplied) {
    std::vector<std::uint8_t> out(premultiplied.size());
    for (size_t i = 0; i + 3 < premultiplied.size(); i += 4) {
        const std::uint8_t alpha = premultiplied[i + 3];
        out[i + 3] = alpha;
        if (alpha == 0) {
            out[i] = out[i + 1] = out[i + 2] = 0;
            continue;
        }
        const float reciprocal = 1.0f / (static_cast<float>(alpha) / 255.0f);
        for (size_t c = 0; c < 3; ++c) {
            const float value = (static_cast<float>(premultiplied[i + c]) / 255.0f) * reciprocal;
            out[i + c] = static_cast<std::uint8_t>(std::min(255.0, std::floor(static_cast<double>(value) * 255.0 + 0.5)));
        }
    }
    return out;
}

} // namespace nm
