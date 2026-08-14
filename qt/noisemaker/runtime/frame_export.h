#pragma once

#include <QByteArray>

#include <exception>
#include <functional>
#include <memory>
#include <vector>

#include "output_sink.h"

class QOpenGLFunctions_4_1_Core;

namespace nm {

struct ExportFrame {
    int width = 0;
    int height = 0;
    int rowStride = 0;
    QByteArray data;
};

struct FrameExportStats {
    int accepted = 0;
    int dropped = 0;
    int completed = 0;
    int failed = 0;
};

struct FrameExportOptions {
    int slotCount = 3;
    std::function<void(const ExportFrame&, double)> onFrame;
    std::function<void(std::exception_ptr)> onError;
};

class FrameExportQueue final : public OutputSink {
public:
    FrameExportQueue(QOpenGLFunctions_4_1_Core* gl, FrameExportOptions options = {});
    ~FrameExportQueue() override;

    FrameExportQueue(const FrameExportQueue&) = delete;
    FrameExportQueue& operator=(const FrameExportQueue&) = delete;

    void configure(const OutputDescriptor& descriptor) override;
    bool submit(const GpuSurface& surface, double timestamp) override;
    void close(bool backendLost = false) override;

    bool enqueue(
        const GpuSurface& surface,
        double timestamp,
        std::function<void(const ExportFrame&, double)> onFrame = {});
    void poll();
    bool available() const;
    FrameExportStats stats() const;

private:
    struct Adapter;
    struct Record;

    void release(Record& record);
    void destroySlots();
    void report(std::exception_ptr error) const;

    std::unique_ptr<Adapter> m_adapter;
    std::vector<std::unique_ptr<Record>> m_slots;
    std::function<void(const ExportFrame&, double)> m_onFrame;
    std::function<void(std::exception_ptr)> m_onError;
    bool m_configured = false;
    bool m_closed = false;
    FrameExportStats m_stats;
};

} // namespace nm
