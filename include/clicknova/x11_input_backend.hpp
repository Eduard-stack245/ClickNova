// SPDX-License-Identifier: Apache-2.0
#pragma once
#if defined(__linux__) && defined(CLICKNOVA_WITH_X11)
#include "clicknova/input_actions.hpp"
#include "clicknova/window_target.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <map>

struct _XDisplay;

namespace clicknova::input {
// XTEST sends foreground X11 input ONLY. No Wayland or privileged /dev/uinput fallback.
// The caller must start an X11EmergencyStop before invoking play().
class X11InputBackend final : public IBackend {
public:
    explicit X11InputBackend(std::shared_ptr<std::atomic_bool> emergency,
                            std::optional<platform::WindowTarget> target = {});
    ~X11InputBackend() override;
    X11InputBackend(const X11InputBackend&) = delete;
    X11InputBackend& operator=(const X11InputBackend&) = delete;
    bool ready() const;
    bool dispatch(const Event& event) override;
    bool releaseOnCleanup(const Event& event) override;
    std::optional<PointerPosition> cursorPosition() override;
    std::optional<PointerPosition> clientToScreen(int x, int y) override;
    bool boundToWindow() const override;
    bool prepareStepWindow(const StepWindow& step, std::stop_token stop) override;
private:
    bool send(const Event& event);
    bool key(int vk, bool press);
    bool unicode(char16_t unit, bool press);
    std::shared_ptr<std::atomic_bool> emergency_;
    std::optional<platform::WindowTarget> defaultTarget_;
    std::optional<platform::WindowTarget> target_;
    _XDisplay* display_{};
    void* xtst_{};
    std::map<int, unsigned int> keysHeld_;
    std::map<char16_t, std::pair<unsigned int, unsigned int>> unicodeHeld_;
};

// Passive, exclusive F12 grab on each X11 root. If another program owns F12,
// start() fails and playback MUST NOT begin. The callback is invoked from a
// watcher thread; it must not touch Qt widgets directly.
class X11EmergencyStop final {
public:
    X11EmergencyStop() = default;
    ~X11EmergencyStop();
    X11EmergencyStop(const X11EmergencyStop&) = delete;
    X11EmergencyStop& operator=(const X11EmergencyStop&) = delete;
    bool start(std::function<void()> onStop);
    void stop();
    std::shared_ptr<std::atomic_bool> triggered() const { return triggered_; }
    bool active() const { return active_.load(); }
private:
    _XDisplay* display_{};
    std::jthread watcher_;
    std::shared_ptr<std::atomic_bool> triggered_{std::make_shared<std::atomic_bool>(false)};
    std::function<void()> onStop_;
    std::atomic_bool active_{false};
    unsigned int keycode_{};
};
} // namespace clicknova::input
#endif
