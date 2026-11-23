// test_ported_effect_geometry — focused render checks for the effect behavior
// this port synced from noisemaker 7f3721e9, 75027859 and 478f94ea, on the
// non-square frames where that behavior is visible (the corpus sweep renders
// every fixture at a square 256 x 256):
//
//   heightGrid (75027859): seen from directly above, the grid is gridScale
//     wide and gridScale * aspectInv deep, so its diffuse image keeps the
//     frame's proportions and its authored orientation.
//   heightmap3d (478f94ea): seen from directly above, the terrain keeps the
//     frame's proportions and shows its color image as authored.
//   renderLandscape3d (7f3721e9): a voxel ray that passes through the edge
//     shared by two cell boundaries shows the face the neighboring voxels
//     leave exposed, so a pixel on such an edge shows what the pixels beside
//     it show wherever the surface is one continuous wall.
//
// The assertions mirror the reference's own tests for these commits
// (shaders/tests/test_height_grid_orientation.mjs,
// test_heightmap_landscape.mjs, test_landscape_edge_ties.mjs), reduced to the
// single desktop-GL backend this port runs. Each case renders through
// nm-render --dsl at the case's frame size and reads the PNG back.
#include <QColor>
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QIODevice>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRgb>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

#include <cstdio>

namespace {

int g_failures = 0;

void check(bool condition, const QString& description) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description.toUtf8().constData());
    if (!condition) ++g_failures;
}

struct Box { int x0 = 0, y0 = 0, x1 = -1, y1 = -1; };

// The box of the lit pixels (the reference's litBox: a pixel with a channel
// sum under `threshold` is unlit). Returns false when none exists.
bool litBox(const QImage& frame, int threshold, Box* box) {
    box->x0 = frame.width(); box->y0 = frame.height();
    box->x1 = -1; box->y1 = -1;
    for (int y = 0; y < frame.height(); ++y) {
        for (int x = 0; x < frame.width(); ++x) {
            const QRgb p = frame.pixel(x, y);
            if (qRed(p) + qGreen(p) + qBlue(p) < threshold) continue;
            box->x0 = qMin(box->x0, x); box->x1 = qMax(box->x1, x);
            box->y0 = qMin(box->y0, y); box->y1 = qMax(box->y1, y);
        }
    }
    return box->x1 >= box->x0;
}

// The box of opaque pixels (alpha above 0): the terrain against the
// transparent background its bgAlpha: 0 gives.
Box opaqueBox(const QImage& frame) {
    Box box;
    box.x0 = frame.width(); box.y0 = frame.height();
    box.x1 = -1; box.y1 = -1;
    for (int y = 0; y < frame.height(); ++y) {
        for (int x = 0; x < frame.width(); ++x) {
            if (qAlpha(frame.pixel(x, y)) == 0) continue;
            box.x0 = qMin(box.x0, x); box.x1 = qMax(box.x1, x);
            box.y0 = qMin(box.y0, y); box.y1 = qMax(box.y1, y);
        }
    }
    return box;
}

struct Sample { int red = 0, green = 0, blue = 0; };

Sample pixel(const QImage& frame, int x, int y) {
    const QRgb p = frame.pixel(x, y);
    return { qRed(p), qGreen(p), qBlue(p) };
}

// Points toward the corners of `box`, each the mean of the lit pixels of a
// 7 by 7 window (the reference's sample()).
bool sampleCorners(const QImage& frame, const Box& box,
                   Sample* topLeft, Sample* topRight, Sample* bottomLeft) {
    const int dx = qRound((box.x1 - box.x0 + 1) * 0.27);
    const int dy = qRound((box.y1 - box.y0 + 1) * 0.27);
    struct At { const char* name; int x, y; Sample* out; };
    const At at[] = {
        {"topLeft", box.x0 + dx, box.y0 + dy, topLeft},
        {"topRight", box.x1 - dx, box.y0 + dy, topRight},
        {"bottomLeft", box.x0 + dx, box.y1 - dy, bottomLeft},
    };
    for (const At& a : at) {
        int sum[3] = {0, 0, 0};
        int lit = 0;
        for (int y = a.y - 3; y <= a.y + 3; ++y) {
            for (int x = a.x - 3; x <= a.x + 3; ++x) {
                if (x < 0 || y < 0 || x >= frame.width() || y >= frame.height()) continue;
                const QRgb p = frame.pixel(x, y);
                if (qRed(p) + qGreen(p) + qBlue(p) == 0) continue;
                sum[0] += qRed(p); sum[1] += qGreen(p); sum[2] += qBlue(p);
                ++lit;
            }
        }
        if (lit == 0) {
            std::printf("FAIL: %s: the sample window must contain lit pixels\n", a.name);
            return false;
        }
        a.out->red = sum[0] / lit; a.out->green = sum[1] / lit; a.out->blue = sum[2] / lit;
    }
    return true;
}

bool isWhite(const QImage& frame, int x, int y) {
    const QRgb p = frame.pixel(x, y);
    return qRed(p) == 255 && qGreen(p) == 255 && qBlue(p) == 255 && qAlpha(p) == 255;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        std::printf("usage: test_ported_effect_geometry <nm-render> <data-root>\n");
        return 2;
    }
    const QString nmRender = QString::fromLocal8Bit(argv[1]);
    const QString dataRoot = QString::fromLocal8Bit(argv[2]);
    QTemporaryDir dir;
    if (!dir.isValid()) return 2;
    const QString dirPath = dir.path();

    auto renderDsl = [&](const QString& name, const QString& dsl, const QSize& size, int frames,
                         const QStringList& externalTextures, QImage* out, QString* message) -> bool {
        const QString dslPath = QString("%1/%2.dsl").arg(dirPath, name);
        const QString outPath = QString("%1/%2.png").arg(dirPath, name);
        {
            QFile file(dslPath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
            file.write(dsl.toUtf8());
        }
        QStringList args = {QStringLiteral("--dsl"), dslPath, QStringLiteral("--size"),
                            QString("%1x%2").arg(size.width()).arg(size.height()),
                            QStringLiteral("--out"), outPath,
                            QStringLiteral("--frames"), QString::number(frames)};
        for (const QString& t : externalTextures) args << QStringLiteral("--external-texture") << t;
        QProcess process;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("NOISEMAKER_QT_DATA_ROOT"), dataRoot);
        process.setProcessEnvironment(env);
        process.start(nmRender, args);
        if (!process.waitForFinished(120000) || process.exitStatus() != QProcess::NormalExit
            || process.exitCode() != 0) {
            *message = QString("nm-render exited %1 for %2: %3")
                           .arg(process.exitCode())
                           .arg(name,
                                QString::fromUtf8(process.readAllStandardError()
                                                  + process.readAllStandardOutput())
                                    .left(400));
            return false;
        }
        *out = QImage(outPath);
        if (out->isNull()) { *message = QString("no readable frame for %1").arg(name); return false; }
        return true;
    };

    auto writePng = [&](const QString& name, const QImage& image) -> QString {
        const QString path = QString("%1/%2.png").arg(dirPath, name);
        image.save(path, "PNG");
        return path;
    };

    const QString referenceDsl =
        QStringLiteral("search synth\ntestPattern(pattern: uvMap).write(o0)\nrender(o0)");

    // --- heightGrid: grid proportions and orientation from directly above -----
    {
        QImage authored;
        QString message;
        const struct { int width, height, gridScale; const char* stateSize; } frames[] = {
            {128, 128, 80, "x128"},
            {256, 128, 80, "x64"},
            {128, 256, 50, "x128"},
        };
        if (renderDsl(QStringLiteral("uvmap-reference"), referenceDsl, QSize(128, 128), 1, {},
                      &authored, &message)) {
            Sample imageTopLeft, imageTopRight, imageBottomLeft;
            const Box whole{0, 0, authored.width() - 1, authored.height() - 1};
            const bool referenceOk =
                sampleCorners(authored, whole, &imageTopLeft, &imageTopRight, &imageBottomLeft);
            const int imageAcross = imageTopRight.red - imageTopLeft.red;
            const int imageDown = imageBottomLeft.green - imageTopLeft.green;
            check(referenceOk && qAbs(imageAcross) > 48 && qAbs(imageDown) > 48,
                  "the uvMap reference must vary along both axes");
            for (const auto& shape : frames) {
                const QSize size(shape.width, shape.height);
                const QString dsl = QString(
                    "search synth, points, render\n"
                    "testPattern(pattern: uvMap).write(o1)\n"
                    "solid(color: #ffffff).pointsEmit(stateSize: %1)"
                    ".heightGrid(gridScale: %2, heightScale: 0, diffuseTex: read(o1))"
                    ".pointsRender(viewMode: perspective, rotateX: 1.5708, density: 100,"
                    " intensity: 0, inputIntensity: 0).write(o0)\n"
                    "render(o0)").arg(QLatin1String(shape.stateSize)).arg(shape.gridScale);
                QImage frame;
                if (!renderDsl(QString("heightgrid-%1x%2").arg(shape.width).arg(shape.height),
                               dsl, size, 4, {}, &frame, &message)) {
                    check(false, QString("heightGrid %1x%2 renders: %3")
                                     .arg(shape.width).arg(shape.height).arg(message));
                    continue;
                }
                Box box;
                const QString label = QString("heightGrid %1x%2").arg(shape.width).arg(shape.height);
                if (!litBox(frame, 24, &box)) {
                    check(false, label + ": the frame must contain lit pixels");
                    continue;
                }
                check(box.x0 > 0 && box.y0 > 0 && box.x1 < shape.width - 1 && box.y1 < shape.height - 1,
                      label + ": the grid must fit inside the frame");
                Sample topLeft, topRight, bottomLeft;
                if (!sampleCorners(frame, box, &topLeft, &topRight, &bottomLeft)) {
                    check(false, label + ": corner samples");
                    continue;
                }
                const int across = topRight.red - topLeft.red;
                const int down = bottomLeft.green - topLeft.green;
                check(((across > 0) == (imageAcross > 0)) && qAbs(across) > 32,
                      label + ": seen from above, image right must be screen right");
                check(((down > 0) == (imageDown > 0)) && qAbs(down) > 32,
                      label + ": seen from above, image top must be screen top");
                const double frameShape = double(shape.width) / double(shape.height);
                const double boxShape = double(box.x1 - box.x0 + 1) / double(box.y1 - box.y0 + 1);
                check(qAbs(boxShape - frameShape) <= frameShape * 0.1,
                      QString("%1: seen from above, the grid is %2 by %3 pixels and its frame is"
                              " %4 by %5")
                          .arg(label)
                          .arg(box.x1 - box.x0 + 1)
                          .arg(box.y1 - box.y0 + 1)
                          .arg(shape.width)
                          .arg(shape.height));
            }
        } else {
            check(false, "the uvMap reference frame renders: " + message);
        }
    }

    // --- heightmap3d: terrain proportions and orientation from above ----------
    {
        const struct { int width, height, posZ; } frames[] = {
            {128, 128, -40},
            {256, 128, -40},
            {128, 256, -120},
        };
        for (const auto& shape : frames) {
            const QSize size(shape.width, shape.height);
            QImage authored, frame;
            QString message;
            if (!renderDsl(QString("terrain-reference-%1").arg(shape.width), referenceDsl, size, 1,
                           {}, &authored, &message)) {
                check(false, QString("uvMap reference %1x%2 renders: %3")
                                 .arg(shape.width).arg(shape.height).arg(message));
                continue;
            }
            const QString dsl = QString(
                "search synth, synth3d, render\n"
                "heightmap3d(heightTex: solid(color: #ffffff), tex: testPattern(pattern: uvMap),"
                " volumeSize: x16, heightScale: 1)\n"
                "  .renderLandscape3d(viewMode: perspective, rotateX: 1.5708, posZ: %1,"
                " bgAlpha: 0, ambient: 1, diffuseIntensity: 0, specularIntensity: 0).write(o0)\n"
                "render(o0)").arg(shape.posZ);
            const QString label = QString("heightmap3d %1x%2").arg(shape.width).arg(shape.height);
            if (!renderDsl(QString("terrain-%1x%2").arg(shape.width).arg(shape.height), dsl, size, 3,
                           {}, &frame, &message)) {
                check(false, label + " renders: " + message);
                continue;
            }
            const Box box = opaqueBox(frame);
            check(box.x1 >= box.x0, label + ": the terrain must be visible");
            check(box.x0 > 0 && box.y0 > 0 && box.x1 < shape.width - 1 && box.y1 < shape.height - 1,
                  QString("%1: the terrain must fit inside the frame (x %2 to %3, y %4 to %5)")
                      .arg(label).arg(box.x0).arg(box.x1).arg(box.y0).arg(box.y1));
            check(qAbs(box.x0 + box.x1 - (shape.width - 1)) <= 3
                      && qAbs(box.y0 + box.y1 - (shape.height - 1)) <= 3,
                  QString("%1: the terrain must be centered in the frame (x %2 to %3, y %4 to %5)")
                      .arg(label).arg(box.x0).arg(box.x1).arg(box.y0).arg(box.y1));
            const Sample imageTopLeft = pixel(authored,
                qRound(shape.width * 0.27), qRound(shape.height * 0.27));
            const Sample imageTopRight = pixel(authored,
                shape.width - 1 - qRound(shape.width * 0.27), qRound(shape.height * 0.27));
            const Sample imageBottomLeft = pixel(authored,
                qRound(shape.width * 0.27), shape.height - 1 - qRound(shape.height * 0.27));
            Sample topLeft, topRight, bottomLeft;
            if (!sampleCorners(frame, box, &topLeft, &topRight, &bottomLeft)) {
                check(false, label + ": corner samples");
                continue;
            }
            const int imageAcross = imageTopRight.red - imageTopLeft.red;
            const int imageDown = imageBottomLeft.green - imageTopLeft.green;
            const int across = topRight.red - topLeft.red;
            const int down = bottomLeft.green - topLeft.green;
            check(qAbs(imageAcross) > 64 && qAbs(imageDown) > 64,
                  label + ": the uvMap reference must vary along both axes");
            check(((across > 0) == (imageAcross > 0)) && qAbs(across) > 64,
                  label + ": seen from above, image right must be screen right");
            check(((down > 0) == (imageDown > 0)) && qAbs(down) > 64,
                  label + ": seen from above, image top must be screen top");
            check(qAbs(bottomLeft.red - topLeft.red) <= 24 && qAbs(topRight.green - topLeft.green) <= 24,
                  label + ": seen from above, the image must not be rotated");
            const double frameShape = double(shape.width) / double(shape.height);
            const double boxShape = double(box.x1 - box.x0 + 1) / double(box.y1 - box.y0 + 1);
            check(qAbs(boxShape - frameShape) <= frameShape * 0.1,
                  QString("%1: seen from above, the terrain is %2 by %3 pixels and its frame is"
                          " %4 by %5")
                      .arg(label)
                      .arg(box.x1 - box.x0 + 1)
                      .arg(box.y1 - box.y0 + 1)
                      .arg(shape.width)
                      .arg(shape.height));
        }
    }

    // --- renderLandscape3d: an edge-crossing ray shows the exposed face -------
    {
        // A white cliff, lit only from the side it faces, so its face is white
        // and every other face is black. The height image is a hard half plane
        // of the uvMap pattern: `fill` describes which side of the 16 x 16
        // image is full height, as [channel, level] of a hard threshold
        // (channel g with level 0.5 is the image's lower half; channel r with
        // level 0.75 is its right quarter), so the cliff edge lies on the
        // volume's cell boundary between columns.
        const struct { int width, height; const char* channel; const char* level;
                       const char* view; } cliffs[] = {
            // Orthographic view of a cliff that faces +z. The frame is an odd
            // number of pixels wide, so the rays of its center column cross x
            // and z boundaries at one distance all the way.
            {129, 129, "g", "0.5", "lightDirection: vec3(0, 0, 1)"},
            // Perspective view, from the volume's axis, of a cliff that faces
            // -x. Every ray of the screen diagonal crosses x and y boundaries
            // at one distance.
            {128, 128, "r", "0.75",
             "viewMode: perspective, rotateX: 0, lightDirection: vec3(-1, 0, 0)"},
        };
        for (const auto& cliff : cliffs) {
            const QString dsl = QString(
                "search synth, filter, synth3d, render\n"
                "testPattern(pattern: uvMap).channel(channel: %1)"
                ".threshold(level: %2, sharpness: 0.01).write(o1)\n"
                "heightmap3d(heightTex: read(o1), tex: solid(color: #ffffff), volumeSize: x16,"
                " heightScale: 1, baseHeight: 0)"
                ".renderLandscape3d(%3, ambient: 0, diffuseIntensity: 1, specularIntensity: 0,"
                " bgAlpha: 0).write(o0)\n"
                "render(o0)").arg(QLatin1String(cliff.channel), QLatin1String(cliff.level),
                                  QLatin1String(cliff.view));
            QImage frame;
            QString message;
            const QString label = QString("cliff %1x%2").arg(cliff.width).arg(cliff.height);
            if (!renderDsl(QString("cliff-%1x%2").arg(cliff.width).arg(cliff.height), dsl,
                           QSize(cliff.width, cliff.height), 3, {}, &frame, &message)) {
                check(false, label + " renders: " + message);
                continue;
            }
            if (cliff.width % 2 == 1) {
                // The center column crosses the lit cliff face through voxel
                // edges; every pixel of it must show what the pixels beside it
                // show.
                const int middle = (cliff.width - 1) / 2;
                int faceRows = 0;
                int offFace = 0;
                Sample firstOff{};
                int firstOffY = -1;
                for (int y = 0; y < cliff.height; ++y) {
                    if (!isWhite(frame, middle - 1, y) || !isWhite(frame, middle + 1, y)) continue;
                    ++faceRows;
                    if (!isWhite(frame, middle, y)) {
                        if (offFace == 0) { firstOff = pixel(frame, middle, y); firstOffY = y; }
                        ++offFace;
                    }
                }
                check(faceRows > 8, label + ": the center column must cross the lit cliff face");
                check(offFace == 0,
                      QString("%1: the center column looks at the lit cliff face through voxel"
                              " edges, and %2 of its %3 pixels there show (%4,%5,%6) at y %7")
                          .arg(label).arg(offFace).arg(faceRows)
                          .arg(firstOff.red).arg(firstOff.green).arg(firstOff.blue).arg(firstOffY));
            } else {
                // The diagonal pixels look at the cliff face through its voxel
                // edges; the pixels beside them do not.
                bool ok = true;
                for (const int at : {92, 96, 100}) {
                    ok = ok && isWhite(frame, at - 1, at) && isWhite(frame, at + 1, at)
                             && isWhite(frame, at, at);
                    if (!ok) {
                        const Sample onEdge = pixel(frame, at, at);
                        check(false, QString("%1: pixel %2,%2 must look at the lit cliff face"
                                             " through a voxel edge; it shows (%3,%4,%5)")
                                         .arg(label).arg(at)
                                         .arg(onEdge.red).arg(onEdge.green).arg(onEdge.blue));
                        break;
                    }
                }
                check(ok, label + ": the screen diagonal must look at the lit cliff face"
                                  " through its voxel edges");
            }
        }

        // The default heightmap3d program at volume size x128 on a 257 by 129
        // frame: on the rows where the pixels on both sides of the center
        // column show one lit wall, the column shows that wall too.
        QImage measured;
        QString message;
        const QString measuredDsl = QStringLiteral(
            "search synth, synth3d, render\n"
            "noise(scaleX: 90, scaleY: 90, colorMode: mono, speed: 0).write(o1)\n"
            "gradient(type: fourCorners, color1: #006e94, color2: #24e4ff,"
            " color3: #bcff46, color4: #efffff).write(o2)\n"
            "heightmap3d(heightTex: read(o1), tex: read(o2), volumeSize: x128)"
            ".renderLandscape3d(panY: -0.18).write(o0)\n"
            "render(o0)");
        if (renderDsl(QStringLiteral("measured-landscape"), measuredDsl, QSize(257, 129), 3, {},
                      &measured, &message)) {
            const int column = (257 - 1) / 2;
            QVector<int> wallRows;
            for (int y = 0; y < 129; ++y) {
                const Sample left = pixel(measured, column - 1, y);
                const Sample right = pixel(measured, column + 1, y);
                if (left.green < 128 || right.green < 128) continue;
                if (qAbs(left.red - right.red) > 8 || qAbs(left.green - right.green) > 8
                    || qAbs(left.blue - right.blue) > 8) continue;
                wallRows.append(y);
            }
            check(wallRows.size() >= 3,
                  "the measured program's center column must cross a lit wall");
            int offWall = 0;
            int firstOffY = -1;
            Sample firstOff{};
            for (const int y : wallRows) {
                const Sample left = pixel(measured, column - 1, y);
                const Sample right = pixel(measured, column + 1, y);
                const Sample center = pixel(measured, column, y);
                const bool onWall =
                    center.red >= qMin(left.red, right.red) - 2
                    && center.red <= qMax(left.red, right.red) + 2
                    && center.green >= qMin(left.green, right.green) - 2
                    && center.green <= qMax(left.green, right.green) + 2
                    && center.blue >= qMin(left.blue, right.blue) - 2
                    && center.blue <= qMax(left.blue, right.blue) + 2;
                if (!onWall) {
                    if (firstOffY < 0) { firstOff = center; firstOffY = y; }
                    ++offWall;
                }
            }
            check(offWall == 0,
                  QString("the measured program's center column looks at a lit wall through voxel"
                          " edges, and %1 of its %2 pixels there show (%3,%4,%5) at y %6")
                      .arg(offWall).arg(wallRows.size())
                      .arg(firstOff.red).arg(firstOff.green).arg(firstOff.blue).arg(firstOffY));
        } else {
            check(false, "the measured landscape program renders: " + message);
        }
    }

    std::printf("%s\n", g_failures == 0 ? "DONE PASS" : "DONE FAIL");
    return g_failures == 0 ? 0 : 1;
}
