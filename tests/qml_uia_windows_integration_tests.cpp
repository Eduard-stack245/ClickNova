// SPDX-License-Identifier: MIT
// Interactive Win32 fixture. No SendInput, Invoke, SetValue or external windows.
// Opt in with CLICKNOVA_UIA_INTERACTIVE_TESTS=ON. CTest runs in its own process.
#ifndef _WIN32
#error This UI Automation integration test is Windows-only.
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../src/gui/qml/element_candidate_win32.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <vector>

using namespace clicknova::accessibility::candidates;
namespace {
constexpr UINT makeDuplicate = WM_APP + 18;
constexpr int buttonId = 1857;
struct UiState {
    std::atomic<bool> duplicated{false};
};
[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL Windows UIA integration: " << message << '\n';
    std::abort();
}
#define CHECK(x) do { if (!(x)) fail(#x); } while (false)
LRESULT CALLBACK fixtureWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    if (message == makeDuplicate) {
        const auto* first = GetDlgItem(hwnd, buttonId);
        if (first) {
            // Deliberately duplicate the ID: UIA AutomationId for Win32 buttons
            // is normally derived from the control ID. The verifier must refuse.
            auto* second = CreateWindowExW(0, L"BUTTON", L"Duplicate fixture button",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 12, 65, 175, 30,
                hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(buttonId)),
                GetModuleHandleW(nullptr), nullptr);
            auto* state = reinterpret_cast<UiState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (state && second) state->duplicated.store(true);
        }
        return 0;
    }
    if (message == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
Target makeFixture(std::promise<Target> ready, UiState* state) {
    // The window and native button live on a dedicated UI/message-pump thread,
    // avoiding UIA calls against controls on our scanning thread.
    auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = fixtureWindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"ClickNovaPhase18UiAutomationFixture";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        ready.set_exception(std::make_exception_ptr(std::runtime_error("RegisterClassW failed")));
        return {};
    }
    auto* hwnd = CreateWindowExW(0, wc.lpszClassName, L"ClickNova UIA local test fixture",
        WS_OVERLAPPEDWINDOW, 20, 20, 260, 160, nullptr, nullptr, instance, state);
    if (!hwnd) {
        ready.set_exception(std::make_exception_ptr(std::runtime_error("CreateWindowExW failed")));
        return {};
    }
    auto* button = CreateWindowExW(0, L"BUTTON", L"Fixture button",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 12, 15, 175, 30,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(buttonId)), instance, nullptr);
    if (!button) {
        DestroyWindow(hwnd);
        ready.set_exception(std::make_exception_ptr(std::runtime_error("CreateWindowExW Button failed")));
        return {};
    }
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);
    Target target{reinterpret_cast<std::uintptr_t>(hwnd), GetCurrentProcessId()};
    ready.set_value(target);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return target;
}
}
int main() {
    // An interactive desktop is needed; skipped rather than claiming success
    // in Windows services / locked CI sessions.
    HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!desktop) {
        std::cout << "SKIP: UI Automation requires an interactive desktop\n";
        return 77;
    }
    CloseDesktop(desktop);
    UiState state;
    std::promise<Target> promise;
    auto future = promise.get_future();
    std::jthread ui([p = std::move(promise), &state]() mutable {
        makeFixture(std::move(p), &state);
    });
    if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready) fail("fixture startup timed out");
    const Target target = future.get();
    CHECK(target.nativeHandle != 0 && target.processId == GetCurrentProcessId());
    auto provider = clicknova::gui::qml::createNativeUiaCandidateProvider();
    CHECK(provider != nullptr);
    const Limits generous{512, 32, 10, 3000};
    const Result found = provider->enumerate(target, generous, {});
    CHECK(found.status == Status::Complete && !found.truncated);
    auto match = std::find_if(found.items.begin(), found.items.end(),
        [](const Candidate& c) { return c.controlType == 50000 && eligible(c); });
    CHECK(match != found.items.end());
    const Candidate original = *match;
    CHECK(provider->verify(target, original, {}).status == VerifyStatus::Verified);
    std::stop_source stopped;
    stopped.request_stop();
    CHECK(provider->verify(target, original, stopped.get_token()).status == VerifyStatus::Cancelled);
    CHECK(PostMessageW(reinterpret_cast<HWND>(target.nativeHandle), makeDuplicate, 0, 0));
    for (int i = 0; i < 100 && !state.duplicated.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(state.duplicated.load());
    CHECK(provider->verify(target, original, {}).status == VerifyStatus::Ambiguous);
    CHECK(PostMessageW(reinterpret_cast<HWND>(target.nativeHandle), WM_CLOSE, 0, 0));
    ui.join();
    CHECK(provider->verify(target, original, {}).status == VerifyStatus::UnsafeTarget);
    std::cout << "PASS: live Win32 UIA unique/ambiguous/cancelled/stale target\n";
    return 0;
}
