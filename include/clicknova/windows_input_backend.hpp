// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "clicknova/input_actions.hpp"
#include "clicknova/window_target.hpp"
#include <optional>
#include <utility>

namespace clicknova::input {
// Uses Win32 SendInput: delivers to the foreground desktop input stream.
// Cannot target a particular physical keyboard/mouse or inactive window.
class WindowsInputBackend final : public IBackend {
public:
    explicit WindowsInputBackend(std::optional<clicknova::platform::WindowTarget> target = {})
        : target_(std::move(target)) {}
    void setTarget(std::optional<clicknova::platform::WindowTarget> target) {
        target_ = std::move(target);
    }
    bool dispatch(const Event& event) override;
    bool boundToWindow() const override { return target_.has_value(); }
    bool releaseOnCleanup(const Event& event) override;
    std::optional<PointerPosition> cursorPosition() override;
    std::optional<PointerPosition> clientToScreen(int x, int y) override;
private:
    bool sendRaw(const Event& event);
    std::optional<clicknova::platform::WindowTarget> target_;
};
} // namespace clicknova::input
