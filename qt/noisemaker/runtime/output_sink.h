#pragma once

#include <QString>

#include <exception>
#include <functional>
#include <memory>

#include "surface.h"

namespace nm {

struct OutputDescriptor {
    int width = 0;
    int height = 0;
    QString format = QStringLiteral("rgba8unorm");
    QString colorSpace = QStringLiteral("srgb");
    QString alphaMode = QStringLiteral("premultiplied");
    double fps = 60.0;
};

class OutputSink {
public:
    virtual ~OutputSink() = default;
    virtual void configure(const OutputDescriptor& descriptor) = 0;
    virtual bool submit(const GpuSurface& surface, double timestamp) = 0;
    virtual void close(bool backendLost = false) = 0;
};

struct SinkStats {
    int accepted = 0;
    int dropped = 0;
    int failed = 0;
};

class SinkManager {
public:
    using ErrorHandler = std::function<void(
        std::exception_ptr, const std::shared_ptr<OutputSink>&)>;

    explicit SinkManager(ErrorHandler onError = {});

    std::function<void()> add(const std::shared_ptr<OutputSink>& sink);
    void remove(OutputSink* sink);
    void configure(const OutputDescriptor& descriptor);
    void submit(const GpuSurface& surface, double timestamp);
    void close(bool backendLost = false);
    SinkStats statsFor(const OutputSink* sink) const;
    bool closed() const;

private:
    struct Registration;
    struct State;

    static void removeRegistration(
        const std::shared_ptr<State>& state,
        const std::shared_ptr<Registration>& registration);
    static void compact(const std::shared_ptr<State>& state);
    static void report(
        const std::shared_ptr<State>& state,
        std::exception_ptr error,
        const std::shared_ptr<OutputSink>& sink);

    std::shared_ptr<State> m_state;
};

} // namespace nm
