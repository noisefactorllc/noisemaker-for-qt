#include "output_sink.h"

#include <QHash>
#include <QVector>

#include <algorithm>
#include <stdexcept>

namespace nm {

struct SinkManager::Registration {
    std::shared_ptr<OutputSink> sink;
    SinkStats stats;
    bool active = true;
};

struct SinkManager::State {
    explicit State(ErrorHandler handler) : onError(std::move(handler)) {}

    ErrorHandler onError;
    QVector<std::shared_ptr<Registration>> registrations;
    QHash<OutputSink*, std::shared_ptr<Registration>> bySink;
    OutputDescriptor descriptor;
    bool configured = false;
    bool closed = false;
    int iterationDepth = 0;
    bool hasTombstones = false;
};

SinkManager::SinkManager(ErrorHandler onError)
    : m_state(std::make_shared<State>(std::move(onError))) {}

std::function<void()> SinkManager::add(const std::shared_ptr<OutputSink>& sink) {
    if (m_state->closed) throw std::runtime_error("SinkManager is closed");
    if (!sink) throw std::invalid_argument("Sink must not be null");
    if (m_state->bySink.contains(sink.get())) {
        throw std::runtime_error("Sink is already registered");
    }
    if (m_state->configured) sink->configure(m_state->descriptor);
    if (m_state->closed) throw std::runtime_error("SinkManager is closed");
    if (m_state->bySink.contains(sink.get())) {
        throw std::runtime_error("Sink is already registered");
    }

    auto registration = std::make_shared<Registration>();
    registration->sink = sink;
    m_state->registrations.push_back(registration);
    m_state->bySink.insert(sink.get(), registration);

    const std::weak_ptr<State> weakState = m_state;
    const std::weak_ptr<Registration> weakRegistration = registration;
    return [weakState, weakRegistration] {
        const auto state = weakState.lock();
        const auto current = weakRegistration.lock();
        if (state && current) SinkManager::removeRegistration(state, current);
    };
}

void SinkManager::remove(OutputSink* sink) {
    removeRegistration(m_state, m_state->bySink.value(sink));
}

void SinkManager::removeRegistration(
    const std::shared_ptr<State>& state,
    const std::shared_ptr<Registration>& registration) {
    if (!registration || !registration->active) return;

    const auto sink = registration->sink;
    registration->active = false;
    registration->sink.reset();
    state->hasTombstones = true;
    if (state->bySink.value(sink.get()) == registration) {
        state->bySink.remove(sink.get());
    }

    try {
        sink->close();
    } catch (...) {
        if (state->iterationDepth == 0) compact(state);
        throw;
    }
    if (state->iterationDepth == 0) compact(state);
}

void SinkManager::compact(const std::shared_ptr<State>& state) {
    if (!state->hasTombstones) return;
    state->registrations.erase(
        std::remove_if(
            state->registrations.begin(),
            state->registrations.end(),
            [](const std::shared_ptr<Registration>& registration) {
                return !registration->active;
            }),
        state->registrations.end());
    state->hasTombstones = false;
}

void SinkManager::report(
    const std::shared_ptr<State>& state,
    std::exception_ptr error,
    const std::shared_ptr<OutputSink>& sink) {
    if (!state->onError) return;
    try {
        state->onError(std::move(error), sink);
    } catch (...) {
    }
}

void SinkManager::configure(const OutputDescriptor& descriptor) {
    if (m_state->closed) return;
    m_state->descriptor = descriptor;
    m_state->configured = true;
    ++m_state->iterationDepth;
    const auto registrations = m_state->registrations;
    for (const auto& registration : registrations) {
        if (!registration->active) continue;
        const auto sink = registration->sink;
        try {
            sink->configure(descriptor);
        } catch (...) {
            ++registration->stats.failed;
            report(m_state, std::current_exception(), sink);
        }
    }
    --m_state->iterationDepth;
    if (m_state->iterationDepth == 0) compact(m_state);
}

void SinkManager::submit(const GpuSurface& surface, double timestamp) {
    if (m_state->closed) return;
    ++m_state->iterationDepth;
    const auto registrations = m_state->registrations;
    for (const auto& registration : registrations) {
        if (!registration->active) continue;
        const auto sink = registration->sink;
        try {
            if (sink->submit(surface, timestamp)) {
                ++registration->stats.accepted;
            } else {
                ++registration->stats.dropped;
            }
        } catch (...) {
            ++registration->stats.failed;
            report(m_state, std::current_exception(), sink);
        }
    }
    --m_state->iterationDepth;
    if (m_state->iterationDepth == 0) compact(m_state);
}

void SinkManager::close(bool backendLost) {
    if (m_state->closed) return;
    m_state->closed = true;
    std::exception_ptr firstError;
    for (const auto& registration : m_state->registrations) {
        if (!registration->active) continue;
        const auto sink = registration->sink;
        registration->active = false;
        registration->sink.reset();
        try {
            sink->close(backendLost);
        } catch (...) {
            if (!firstError) firstError = std::current_exception();
        }
    }
    m_state->registrations.clear();
    m_state->bySink.clear();
    m_state->hasTombstones = false;
    if (firstError) std::rethrow_exception(firstError);
}

SinkStats SinkManager::statsFor(const OutputSink* sink) const {
    const auto registration = m_state->bySink.value(const_cast<OutputSink*>(sink));
    return registration ? registration->stats : SinkStats{};
}

bool SinkManager::closed() const {
    return m_state->closed;
}

} // namespace nm
