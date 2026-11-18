// SPDX-License-Identifier: MIT
// Qt Core smoke with a fake device: no OS input is ever injected.
#include "../src/gui/quick_clicker_service.hpp"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); std::abort(); } } while (false)
#include <memory>
#include <vector>

using clicknova::gui::IQuickClickerDevice;
using clicknova::gui::QuickClickerService;
using clicknova::quick::ClickerPoint;
using clicknova::platform::WindowTarget;

struct FakeDevice final : IQuickClickerDevice {
    bool safe{true};
    bool awake{false};
    bool held{false};
    bool failRelease{false};
    int presses{0};
    int releases{0};
    int moves{0};
    ClickerPoint cursor{10, 20};
    bool validateTarget(const WindowTarget&, bool, QString& error) override {
        if (safe) return true;
        error = QStringLiteral("Window unsafe"); return false;
    }
    bool resolveClientPoint(const WindowTarget&, ClickerPoint p, ClickerPoint& out,
                            QString& error) override {
        if (!safe) { error = QStringLiteral("Window unsafe"); return false; }
        out = p; return true;
    }
    bool checkScreenPoint(const WindowTarget&, ClickerPoint, QString& error) override {
        if (safe) return true;
        error = QStringLiteral("Target lost focus"); return false;
    }
    bool cursorPosition(ClickerPoint& point, QString&) override { point = cursor; return true; }
    bool movePointer(ClickerPoint point, QString&) override {
        cursor = point; ++moves; return true;
    }
    bool press(int, QString&) override {
        CHECK(!held); held = true; ++presses; return true;
    }
    bool release(int, bool, QString& error) override {
        CHECK(held);
        if (failRelease) { error = QStringLiteral("Simulated OS release failure"); return false; }
        held = false; ++releases; return true;
    }
    void keepAwake(bool value) override { awake = value; }
};

static void pump(int millis) {
    QEventLoop loop;
    QTimer::singleShot(millis, &loop, &QEventLoop::quit);
    loop.exec();
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    auto device = std::make_unique<FakeDevice>();
    auto* fake = device.get();
    QuickClickerService service(std::move(device));
    CHECK(!service.start()); // Cannot run without a reserved global Stop.
    int acquire = 0, release = 0;
    bool otherWorkflowIdle = true;
    service.setSafetyHooks([&] { ++acquire; return true; },
                           [&] { ++release; }, [&] { return otherWorkflowIdle; });
    CHECK(!service.configure({{QStringLiteral("intervalMs"), 9}}));
    CHECK(!service.configure({{QStringLiteral("arbitraryHandle"), 123}}));
    CHECK(!service.configure({{QStringLiteral("count"), 2.5}}));
    CHECK(service.configure({{QStringLiteral("intervalMs"), 10},
                              {QStringLiteral("count"), 3},
                              {QStringLiteral("delayMs"), 0},
                              {QStringLiteral("keepAwake"), true}}));
    CHECK(service.start());
    CHECK(fake->awake);
    pump(100);
    CHECK(!service.active());
    CHECK(service.state() == QuickClickerService::Completed);
    CHECK(service.completed() == 3);
    CHECK(fake->presses == 3 && fake->releases == 3 && !fake->held);
    CHECK(acquire == 1 && release == 1 && !fake->awake);

    // Stop during countdown must not dispatch even one click.
    CHECK(service.configure({{QStringLiteral("delayMs"), 60000}}));
    CHECK(service.start());
    CHECK(service.state() == QuickClickerService::Countdown);
    service.stop();
    CHECK(fake->presses == 3);
    CHECK(acquire == 2 && release == 2 && !fake->awake);
    pump(20);
    CHECK(fake->presses == 3);

    // An indefinitely held mouse button MUST be released by Stop.
    CHECK(service.configure({{QStringLiteral("delayMs"), 0},
                              {QStringLiteral("action"), 3},
                              {QStringLiteral("count"), 1}}));
    CHECK(service.start());
    pump(20);
    CHECK(fake->held && service.active());
    service.stop();
    CHECK(!fake->held && fake->presses == 4 && fake->releases == 4);
    CHECK(acquire == 3 && release == 3);

    // A bound window must be re-selected and validated on every run.
    CHECK(service.configure({{QStringLiteral("action"), 0},
                              {QStringLiteral("targetWindow"), true}}));
    CHECK(!service.start());
    WindowTarget target{};
    target.handle = 123;
    target.processId = 555;
    CHECK(service.setSelectedWindow(target));
    fake->safe = false;
    CHECK(!service.start());
    CHECK(acquire == 3);
    fake->safe = true;
    otherWorkflowIdle = false;
    CHECK(!service.start());
    otherWorkflowIdle = true;

    // Move-only must not press the mouse button.
    CHECK(service.configure({{QStringLiteral("action"), 1},
                              {QStringLiteral("destinationConfigured"), true},
                              {QStringLiteral("destinationX"), 15},
                              {QStringLiteral("destinationY"), 25},
                              {QStringLiteral("movementMs"), 0},
                              {QStringLiteral("count"), 1}}));
    CHECK(service.start());
    pump(20);
    CHECK(service.state() == QuickClickerService::Completed);
    CHECK(fake->presses == 4 && fake->releases == 4 && fake->moves >= 1);
    CHECK(acquire == 4 && release == 4);

    // Focus loss mid-drag: state must become Failed and cleanup releases.
    CHECK(service.configure({{QStringLiteral("action"), 2},
                              {QStringLiteral("movementMs"), 200},
                              {QStringLiteral("count"), 1}}));
    CHECK(service.start());
    pump(25);
    CHECK(fake->held);
    fake->safe = false;
    pump(40);
    CHECK(service.state() == QuickClickerService::Failed);
    CHECK(!fake->held);
    CHECK(acquire == 5 && release == 5);
    fake->safe = true;

    // A conflicted emergency key is a hard failure, with no OS input.
    int denied = 0;
    service.setSafetyHooks([&] { ++denied; return false; },
                           [&] { ++release; }, [&] { return true; });
    const int before = fake->presses;
    CHECK(!service.start());
    pump(20);
    CHECK(denied == 1 && fake->presses == before);
    CHECK(release == 5);

    // If even emergency cleanup fails, forbid new runs until Stop recovers.
    service.setSafetyHooks([&] { ++acquire; return true; },
                           [&] { ++release; }, [&] { return true; });
    CHECK(service.configure({{QStringLiteral("action"), 3},
                             {QStringLiteral("targetWindow"), false},
                             {QStringLiteral("destinationConfigured"), false},
                             {QStringLiteral("count"), 1}}));
    CHECK(service.start());
    pump(20);
    CHECK(fake->held);
    fake->failRelease = true;
    service.stop();
    CHECK(service.state() == QuickClickerService::Failed && fake->held);
    CHECK(service.needsCleanup());
    CHECK(!service.start());
    CHECK(acquire == 6 && release == 5); // Fails closed: keep global Stop reserved.
    fake->failRelease = false;
    service.stop(); // Explicit retry
    CHECK(!fake->held && !service.needsCleanup());
    CHECK(acquire == 6 && release == 6);

    // Holding a bound mouse button must not survive target focus loss.
    CHECK(service.configure({{QStringLiteral("action"), 3},
                             {QStringLiteral("targetWindow"), true},
                             {QStringLiteral("count"), 1}}));
    CHECK(service.start());
    pump(35);
    CHECK(fake->held);
    fake->safe = false;
    pump(180); // 100 ms safety watch detects focus loss and releases.
    CHECK(service.state() == QuickClickerService::Failed);
    CHECK(!fake->held && !service.needsCleanup());
    CHECK(acquire == 7 && release == 7);
    return 0;
}
