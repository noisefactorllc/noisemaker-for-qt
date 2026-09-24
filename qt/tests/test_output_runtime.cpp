#include "../noisemaker/runtime/output_sink.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
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

class TestSink final : public nm::OutputSink {
public:
    std::function<void()> onConfigure;
    std::function<bool()> onSubmit;
    std::function<bool()> onDeferRender;
    nm::OutputDescriptor descriptor;
    int configureCalls = 0;
    int submitCalls = 0;
    int closeCalls = 0;
    int deferRenderCalls = 0;
    bool throwOnConfigure = false;
    bool throwOnSubmit = false;
    bool throwOnClose = false;
    bool throwOnDeferRender = false;

    void configure(const nm::OutputDescriptor& value) override {
        ++configureCalls;
        descriptor = value;
        if (onConfigure) onConfigure();
        if (throwOnConfigure) throw std::runtime_error("configure failed");
    }

    bool submit(const nm::GpuSurface&, double) override {
        ++submitCalls;
        if (throwOnSubmit) throw std::runtime_error("submit failed");
        return onSubmit ? onSubmit() : true;
    }

    void close(bool) override {
        ++closeCalls;
        if (throwOnClose) throw std::runtime_error("close failed");
    }

    bool deferRender() override {
        ++deferRenderCalls;
        if (throwOnDeferRender) throw std::runtime_error("deferRender failed");
        return onDeferRender ? onDeferRender() : false;
    }
};

} // namespace

int main() {
    const nm::GpuSurface surface{17, 23, 2, 3};
    const nm::OutputDescriptor descriptor{
        2, 3, QStringLiteral("rgba8unorm"), QStringLiteral("srgb"),
        QStringLiteral("straight"), 60.0
    };

    {
        nm::SinkManager manager;
        auto first = std::make_shared<TestSink>();
        auto later = std::make_shared<TestSink>();
        manager.add(first);
        manager.configure(descriptor);
        manager.add(later);

        check(first->configureCalls == 1, "configure reaches an existing sink");
        check(later->configureCalls == 1, "a later sink receives the current descriptor");
        check(later->descriptor.width == 2 && later->descriptor.height == 3,
              "descriptor extent is preserved");
    }

    {
        nm::SinkManager manager;
        manager.configure(descriptor);
        auto closing = std::make_shared<TestSink>();
        closing->onConfigure = [&] { manager.close(); };

        bool addThrew = false;
        try {
            manager.add(closing);
        } catch (const std::runtime_error&) {
            addThrew = true;
        }

        check(addThrew && manager.closed(),
              "closing during immediate configure prevents registration");
        check(closing->closeCalls == 0,
              "a sink rejected during immediate configure is not owned or closed");
    }

    {
        nm::SinkManager manager;
        manager.configure(descriptor);
        auto reentrant = std::make_shared<TestSink>();
        bool adding = false;
        reentrant->onConfigure = [&] {
            if (!adding) {
                adding = true;
                manager.add(reentrant);
            }
        };

        bool outerAddThrew = false;
        try {
            manager.add(reentrant);
        } catch (const std::runtime_error&) {
            outerAddThrew = true;
        }
        manager.submit(surface, 0.0);
        manager.close();

        check(outerAddThrew,
              "reentrant same-sink registration rejects the outer duplicate");
        check(reentrant->submitCalls == 1 && reentrant->closeCalls == 1,
              "reentrant same-sink registration retains exactly one owner");
    }

    {
        nm::SinkManager manager;
        auto adding = std::make_shared<TestSink>();
        auto added = std::make_shared<TestSink>();
        bool addedOnce = false;
        adding->onConfigure = [&] {
            if (!addedOnce) {
                addedOnce = true;
                manager.add(added);
            }
        };
        manager.add(adding);

        manager.configure(descriptor);

        check(adding->configureCalls == 1 && added->configureCalls == 1,
              "a sink added during configure receives the descriptor exactly once");
    }

    {
        nm::SinkManager manager;
        auto adding = std::make_shared<TestSink>();
        auto added = std::make_shared<TestSink>();
        bool addedOnce = false;
        adding->onSubmit = [&] {
            if (!addedOnce) {
                addedOnce = true;
                manager.add(added);
            }
            return true;
        };
        manager.add(adding);

        manager.submit(surface, 1.0);
        check(added->submitCalls == 0,
              "a sink added during submit starts with the next submission");
        manager.submit(surface, 2.0);
        check(added->submitCalls == 1,
              "a sink added reentrantly participates in later submissions");
    }

    {
        nm::SinkManager manager;
        auto closing = std::make_shared<TestSink>();
        auto later = std::make_shared<TestSink>();
        closing->onSubmit = [&] {
            manager.close();
            return true;
        };
        manager.add(closing);
        manager.add(later);

        manager.submit(surface, 3.0);

        check(closing->closeCalls == 1 && later->closeCalls == 1,
              "closing during submit closes every sink exactly once");
        check(later->submitCalls == 0,
              "closing during submit prevents later callbacks");
    }

    {
        nm::SinkManager manager;
        auto closing = std::make_shared<TestSink>();
        auto later = std::make_shared<TestSink>();
        closing->onConfigure = [&] { manager.close(); };
        manager.add(closing);
        manager.add(later);

        manager.configure(descriptor);

        check(closing->closeCalls == 1 && later->closeCalls == 1,
              "closing during configure closes every sink exactly once");
        check(later->configureCalls == 0,
              "closing during configure prevents later callbacks");
    }

    {
        std::vector<std::string> errors;
        nm::SinkManager manager([&errors](std::exception_ptr error, const std::shared_ptr<nm::OutputSink>&) {
            try {
                std::rethrow_exception(error);
            } catch (const std::exception& exception) {
                errors.emplace_back(exception.what());
            }
            throw std::runtime_error("reporter failure must be isolated");
        });
        auto accepted = std::make_shared<TestSink>();
        auto dropped = std::make_shared<TestSink>();
        auto failed = std::make_shared<TestSink>();
        auto later = std::make_shared<TestSink>();
        dropped->onSubmit = [] { return false; };
        failed->throwOnSubmit = true;
        manager.add(accepted);
        manager.add(dropped);
        manager.add(failed);
        manager.add(later);

        manager.submit(surface, 12.5);

        check(manager.statsFor(accepted.get()).accepted == 1, "accepted submissions are counted");
        check(manager.statsFor(dropped.get()).dropped == 1, "dropped submissions are counted");
        check(manager.statsFor(failed.get()).failed == 1, "failed submissions are counted");
        check(later->submitCalls == 1, "a failed sink does not stop later sinks");
        check(errors == std::vector<std::string>{"submit failed"},
              "sink failures are reported and reporter failures are isolated");
    }

    {
        nm::SinkManager manager;
        auto removing = std::make_shared<TestSink>();
        auto later = std::make_shared<TestSink>();
        std::function<void()> remove;
        removing->onSubmit = [&remove] {
            remove();
            return true;
        };
        remove = manager.add(removing);
        manager.add(later);

        manager.submit(surface, 20.0);
        remove();

        check(removing->closeCalls == 1, "removal is idempotent and closes once");
        check(later->submitCalls == 1, "self-removal does not skip a later sink");
    }

    {
        nm::SinkManager manager;
        auto first = std::make_shared<TestSink>();
        auto second = std::make_shared<TestSink>();
        first->throwOnClose = true;
        manager.add(first);
        manager.add(second);

        bool closeThrew = false;
        try {
            manager.close();
        } catch (const std::runtime_error&) {
            closeThrew = true;
        }
        manager.close();
        bool addThrew = false;
        try {
            manager.add(std::make_shared<TestSink>());
        } catch (const std::runtime_error&) {
            addThrew = true;
        }

        check(closeThrew, "close reports the first sink failure");
        check(first->closeCalls == 1 && second->closeCalls == 1,
              "close remains terminal and closes every sink once");
        check(addThrew, "a closed manager rejects new sinks");
    }

    {
        nm::SinkManager manager;
        auto nonDeferring = std::make_shared<TestSink>();
        auto deferring = std::make_shared<TestSink>();
        deferring->onDeferRender = [] { return true; };
        manager.add(nonDeferring);
        check(!manager.shouldDeferRender(), "shouldDeferRender returns false when no sink defers");

        auto removeDeferring = manager.add(deferring);
        check(manager.shouldDeferRender(), "shouldDeferRender returns true when an active sink defers");

        removeDeferring();
        check(!manager.shouldDeferRender(), "shouldDeferRender returns false after removing deferring sink");

        manager.add(deferring);
        manager.close();
        check(!manager.shouldDeferRender(), "shouldDeferRender returns false when manager is closed");
    }

    {
        std::vector<std::string> errors;
        nm::SinkManager manager([&errors](std::exception_ptr error, const std::shared_ptr<nm::OutputSink>&) {
            try {
                std::rethrow_exception(error);
            } catch (const std::exception& exception) {
                errors.emplace_back(exception.what());
            }
        });
        auto throwingSink = std::make_shared<TestSink>();
        throwingSink->throwOnDeferRender = true;
        auto laterDeferring = std::make_shared<TestSink>();
        laterDeferring->onDeferRender = [] { return true; };

        manager.add(throwingSink);
        check(!manager.shouldDeferRender(), "a throwing deferRender does not defer by itself");
        check(manager.statsFor(throwingSink.get()).failed == 1,
              "a throwing deferRender increments failed count");
        check(errors == std::vector<std::string>{"deferRender failed"},
              "a throwing deferRender reports to onError");

        manager.add(laterDeferring);
        check(manager.shouldDeferRender(),
              "a throwing deferRender does not prevent subsequent sinks from deferring");
        check(manager.statsFor(throwingSink.get()).failed == 2,
              "second shouldDeferRender call increments failed count again on throwing sink");
    }

    if (g_failures == 0) {
        std::printf("ALL PASS (test_output_runtime)\n");
        return 0;
    }
    std::printf("%d FAILURE(S) (test_output_runtime)\n", g_failures);
    return 1;
}
