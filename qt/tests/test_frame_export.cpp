#include "../noisemaker/compiler/dsl_compiler.h"
#include "../noisemaker/runtime/backend.h"
#include "../noisemaker/runtime/frame_export.h"

#include <QGuiApplication>
#include <QImage>
#include <QThread>

#include <cstdio>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const char* description) {
    if (condition) {
        std::printf("PASS: %s\n", description);
    } else {
        std::printf("FAIL: %s\n", description);
        ++g_failures;
    }
}

bool waitFor(const std::shared_ptr<nm::FrameExportQueue>& queue, int completed) {
    for (int attempt = 0; attempt < 2000; ++attempt) {
        queue->poll();
        if (queue->stats().completed >= completed) return true;
        QThread::usleep(500);
    }
    return false;
}

bool waitForFailures(const std::shared_ptr<nm::FrameExportQueue>& queue, int failed) {
    for (int attempt = 0; attempt < 2000; ++attempt) {
        queue->poll();
        if (queue->stats().failed >= failed) return true;
        QThread::usleep(500);
    }
    return false;
}

QByteArray imageBytes(const QImage& image) {
    return QByteArray(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes());
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    {
        nm::Backend unopened;
        bool threw = false;
        try {
            unopened.createFrameExportQueue();
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "frame export requires a configured backend");
    }
    nm::Backend backend;
    backend.setup(nullptr, QStringLiteral("qt/noisemaker"), QSize(8, 8));

    {
        nm::FrameExportOptions invalid;
        invalid.slotCount = 1;
        bool threw = false;
        try {
            backend.createFrameExportQueue(invalid);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "frame export enforces a two-through-eight slot bound");
    }

    std::vector<nm::ExportFrame> frames;
    std::vector<double> timestamps;
    int reportedErrors = 0;
    bool throwFromCallback = false;
    nm::FrameExportOptions options;
    options.slotCount = 2;
    options.onFrame = [&frames, &timestamps, &throwFromCallback](const nm::ExportFrame& frame, double timestamp) {
        if (throwFromCallback) throw std::runtime_error("callback failed");
        frames.push_back(frame);
        timestamps.push_back(timestamp);
    };
    options.onError = [&reportedErrors](std::exception_ptr) {
        ++reportedErrors;
        throw std::runtime_error("reporter failure must be isolated");
    };
    auto queue = backend.createFrameExportQueue(options);
    auto remove = backend.addSink(queue);

    nm::OutputDescriptor descriptor;
    descriptor.width = 8;
    descriptor.height = 8;
    descriptor.format = QStringLiteral("rgba8unorm");
    descriptor.colorSpace = QStringLiteral("srgb");
    descriptor.alphaMode = QStringLiteral("straight");
    descriptor.fps = 60.0;
    queue->configure(descriptor);

    const nm::Graph gradient = nm::compileGraph(
        QStringLiteral("search synth\ngradient(type: linear, rotation: 45).write(o0)\nrender(o0)\n"));
    backend.render(gradient, 0.0, -42.0);
    const QImage synchronous = backend.readSurface();

    check(waitFor(queue, 1), "asynchronous readback completes");
    check(frames.size() == 1 && frames[0].width == 8 && frames[0].height == 8
              && frames[0].rowStride == 32,
          "exported frame descriptor is RGBA8 with a tight row stride");
    check(!frames.empty() && frames[0].data == imageBytes(synchronous),
          "frame export preserves the synchronous top-down pixel order");
    check(!frames.empty() && frames[0].data.left(32) != frames[0].data.right(32),
          "orientation check uses visibly distinct top and bottom rows");
    check(timestamps == std::vector<double>{-42.0},
          "an explicit negative presentation timestamp reaches the callback");

    frames.clear();
    timestamps.clear();
    const nm::Graph solid = nm::compileGraph(
        QStringLiteral("search synth\nsolid(color: #ff8040, alpha: 0.5).write(o0)\nrender(o0)\n"));

    descriptor.alphaMode = QStringLiteral("straight");
    queue->configure(descriptor);
    backend.render(solid, 0.0, 1.0);
    check(waitFor(queue, 2), "straight-alpha frame completes");
    const QByteArray straight = frames.empty() ? QByteArray(4, '\0') : frames.back().data.left(4);

    descriptor.alphaMode = QStringLiteral("opaque");
    queue->configure(descriptor);
    backend.render(solid, 0.0, 2.0);
    check(waitFor(queue, 3), "opaque-alpha frame completes");
    const QByteArray opaque = frames.empty() ? QByteArray(4, '\0') : frames.back().data.left(4);

    descriptor.alphaMode = QStringLiteral("premultiplied");
    queue->configure(descriptor);
    backend.render(solid, 0.0, 3.0);
    check(waitFor(queue, 4), "premultiplied-alpha frame completes");
    const QByteArray premultiplied = frames.empty() ? QByteArray(4, '\0') : frames.back().data.left(4);

    check(static_cast<unsigned char>(straight[3]) < 255
              && static_cast<unsigned char>(opaque[3]) == 255,
          "opaque mode forces alpha without changing straight mode");
    check(straight.left(3) == opaque.left(3), "opaque mode preserves RGB");
    check(static_cast<unsigned char>(premultiplied[0])
              < static_cast<unsigned char>(straight[0])
              && premultiplied[3] == straight[3],
          "premultiplied mode scales RGB and preserves alpha");

    throwFromCallback = true;
    descriptor.alphaMode = QStringLiteral("straight");
    queue->configure(descriptor);
    backend.render(solid, 0.0, 4.0);
    check(waitForFailures(queue, 1), "callback failure is observed without stalling the queue");
    check(reportedErrors == 1 && queue->available(),
          "callback and reporter failures are isolated and the slot is reusable");
    throwFromCallback = false;

    descriptor.width = 4;
    queue->configure(descriptor);
    backend.render(solid, 0.0, 5.0);
    check(queue->stats().failed == 2, "source/descriptor extent mismatch fails the enqueue");
    check(backend.sinkStats(queue.get()).dropped == 1,
          "the sink manager counts a rejected frame as dropped");
    descriptor.width = 8;

    descriptor.alphaMode = QStringLiteral("straight");
    queue->configure(descriptor);
    backend.render(gradient, 0.0, 10.0);
    backend.render(gradient, 0.0, 11.0);
    backend.render(gradient, 0.0, 12.0);
    check(queue->stats().dropped == 1, "a bounded two-slot queue drops overflow");
    check(waitFor(queue, 6), "both accepted overflow-test frames complete");

    remove();
    remove();
    check(!queue->available(), "removing the queue closes it once and makes it unavailable");

    std::vector<std::uintptr_t> storageAddresses;
    std::shared_ptr<nm::FrameExportQueue> reuseQueue;
    nm::FrameExportOptions reuseOptions;
    reuseOptions.slotCount = 2;
    reuseOptions.onFrame = [&](const nm::ExportFrame& frame, double) {
        storageAddresses.push_back(
            reinterpret_cast<std::uintptr_t>(frame.data.constData()));
        if (storageAddresses.size() == 1) {
            backend.render(gradient, 0.0, 21.0);
            for (int attempt = 0;
                 attempt < 2000 && storageAddresses.size() < 2;
                 ++attempt) {
                reuseQueue->poll();
                QThread::usleep(500);
            }
        }
    };
    reuseQueue = backend.createFrameExportQueue(reuseOptions);
    auto removeReuseQueue = backend.addSink(reuseQueue);
    reuseQueue->configure(descriptor);
    backend.render(gradient, 0.0, 20.0);
    check(waitFor(reuseQueue, 2), "second reusable-slot frame completes");
    check(storageAddresses.size() == 2
              && storageAddresses[0] == storageAddresses[1],
          "reusing a slot preserves its preallocated CPU storage");
    removeReuseQueue();

    if (g_failures == 0) {
        std::printf("ALL PASS (test_frame_export)\n");
        return 0;
    }
    std::printf("%d FAILURE(S) (test_frame_export)\n", g_failures);
    return 1;
}
