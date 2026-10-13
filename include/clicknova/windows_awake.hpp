// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace clicknova::platform {
// Windows tracks execution-state requests per thread. Construct and destroy
// this guard on the SAME thread; it must never outlive a playback session.
// Does not override manual sleep, lid closure, forced hibernation or shutdown.
class WindowsAwakeGuard final {
public:
    explicit WindowsAwakeGuard(bool enabled) noexcept { setEnabled(enabled); }
    WindowsAwakeGuard(const WindowsAwakeGuard&) = delete;
    WindowsAwakeGuard& operator=(const WindowsAwakeGuard&) = delete;
    ~WindowsAwakeGuard() { setEnabled(false); }
    void setEnabled(bool enabled) noexcept {
#ifdef _WIN32
        if (enabled == active_) return;
        if (enabled) {
            active_ = ::SetThreadExecutionState(
                ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED) != 0;
        } else {
            ::SetThreadExecutionState(ES_CONTINUOUS);
            active_ = false;
        }
#else
        (void)enabled;
#endif
    }
    [[nodiscard]] bool active() const noexcept { return active_; }
private:
    bool active_{false};
};
} // namespace clicknova::platform
