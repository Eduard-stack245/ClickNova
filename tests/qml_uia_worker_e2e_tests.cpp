// SPDX-License-Identifier: MIT
// Opt-in Windows integration test. Calls the REAL ClickNova.exe --uia-worker
// with a disposable Win32 test window; no SendInput or Invoke is ever issued.
#ifndef _WIN32
#error This integration fixture requires Windows and a real interactive desktop.
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../src/gui/qml/uia_isolated_transport.hpp"
#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QString>
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
#include <utility>

namespace ca = clicknova::accessibility::candidates;
namespace ipc = clicknova::accessibility::ipc;
namespace transport = clicknova::gui::qml::uia_transport;
namespace {
constexpr UINT duplicateMessage = WM_APP + 71;
constexpr int fixtureButtonId = 4520;
constexpr wchar_t fixtureClass[] = L"ClickNovaPhase20WorkerFixture";
std::atomic<bool> duplicateCreated{false};

[[noreturn]] void failure(const char* what) {
    std::cerr << "Phase20 real worker failure: " << what << '\n';
    std::abort();
}
#define REQUIRE(expr) do { if (!(expr)) failure(#expr); } while (false)

LRESULT CALLBACK fixtureProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == duplicateMessage) {
        // Same Win32 control ID makes UIA's AutomationId ambiguous.
        auto* duplicate = CreateWindowExW(0, L"BUTTON", L"Second local test button",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 18, 65, 190, 32,
            hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(fixtureButtonId)),
            GetModuleHandleW(nullptr), nullptr);
        duplicateCreated.store(duplicate != nullptr);
        return 0;
    }
    if (message == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void fixtureThread(std::promise<ca::Target> ready) {
    auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW cls{};
    cls.lpfnWndProc = fixtureProc;
    cls.hInstance = instance;
    cls.lpszClassName = fixtureClass;
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        ready.set_exception(std::make_exception_ptr(std::runtime_error("RegisterClassW failed")));
        return;
    }
    HWND hwnd = CreateWindowExW(0, fixtureClass, L"ClickNova Phase20 isolated UIA test",
        WS_OVERLAPPEDWINDOW, 30, 30, 265, 155, nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        ready.set_exception(std::make_exception_ptr(std::runtime_error("CreateWindowExW failed")));
        return;
    }
    if (!CreateWindowExW(0, L"BUTTON", L"Local UIA test button",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 18, 15, 190, 32,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(fixtureButtonId)), instance, nullptr)) {
        DestroyWindow(hwnd);
        ready.set_exception(std::make_exception_ptr(std::runtime_error("Button creation failed")));
        return;
    }
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);
    ready.set_value(ca::Target{reinterpret_cast<std::uintptr_t>(hwnd), GetCurrentProcessId()});
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

ipc::Request createRequest(ipc::Operation operation, ca::Target target,
                           std::uint64_t nonce) {
    ipc::Request request;
    request.operation = operation;
    request.nonce = nonce;
    request.target = target;
    request.limits = operation == ipc::Operation::Scan
        ? ca::Limits{320,24,6,1200} : ca::Limits{512,32,10,3000};
    return request;
}

transport::Transaction invokeWorker(const QString& executable, const ipc::Request& request) {
    return transport::transact(executable, {QStringLiteral("--uia-worker")}, request,
        request.operation == ipc::Operation::Scan ? 4000 : 6500, {});
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    REQUIRE(argc == 2);
    const QString executable = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    REQUIRE(QFileInfo(executable).isFile());
    // Avoid false PASS on headless CI/services. CTest must mark code 77 SKIPPED.
    HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!desktop) {
        std::cout << "SKIP: a Windows interactive desktop is required\n";
        return 77;
    }
    CloseDesktop(desktop);
    std::promise<ca::Target> promise;
    auto ready = promise.get_future();
    std::jthread ui([p = std::move(promise)]() mutable { fixtureThread(std::move(p)); });
    REQUIRE(ready.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    const ca::Target target = ready.get();
    REQUIRE(target.nativeHandle != 0 && target.processId == GetCurrentProcessId());

    auto scan = createRequest(ipc::Operation::Scan,target,11);
    const auto enumerated = invokeWorker(executable,scan);
    REQUIRE(enumerated.outcome == transport::Outcome::Success);
    REQUIRE(enumerated.reply.scan.status == ca::Status::Complete);
    REQUIRE(!enumerated.reply.scan.truncated);
    auto found = std::find_if(enumerated.reply.scan.items.begin(), enumerated.reply.scan.items.end(),
        [](const ca::Candidate& item) { return item.controlType == 50000 && ca::eligible(item); });
    REQUIRE(found != enumerated.reply.scan.items.end());

    auto verify = createRequest(ipc::Operation::Verify,target,12);
    verify.candidate = *found;
    const auto verified = invokeWorker(executable,verify);
    REQUIRE(verified.outcome == transport::Outcome::Success);
    REQUIRE(verified.reply.verification.status == ca::VerifyStatus::Verified);
    REQUIRE(ca::sameIdentity(verify.candidate,verified.reply.verification.match));

    // A process id mismatch must be rejected by the CHILD itself.
    auto wrongProcess = createRequest(ipc::Operation::Scan,target,13);
    wrongProcess.target.processId = target.processId + 1;
    const auto refused = invokeWorker(executable,wrongProcess);
    REQUIRE(refused.outcome == transport::Outcome::Success);
    REQUIRE(refused.reply.scan.status == ca::Status::UnsafeTarget);

    REQUIRE(PostMessageW(reinterpret_cast<HWND>(target.nativeHandle),duplicateMessage,0,0));
    for (int i=0; i<100 && !duplicateCreated.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(duplicateCreated.load());
    const auto ambiguous = invokeWorker(executable,verify);
    REQUIRE(ambiguous.outcome == transport::Outcome::Success);
    REQUIRE(ambiguous.reply.verification.status == ca::VerifyStatus::Ambiguous);

    REQUIRE(PostMessageW(reinterpret_cast<HWND>(target.nativeHandle),WM_CLOSE,0,0));
    ui.join();
    const auto gone = invokeWorker(executable,scan);
    REQUIRE(gone.outcome == transport::Outcome::Success);
    REQUIRE(gone.reply.scan.status == ca::Status::UnsafeTarget);

    // An already cancelled request must never start a child process.
    std::stop_source stop;
    stop.request_stop();
    REQUIRE(transport::transact(executable,{QStringLiteral("--uia-worker")},scan,
        4000,stop.get_token()).outcome == transport::Outcome::Cancelled);
    // The executable must refuse an extra byte after the framed request.
    // This also checks that the reserved worker entrypoint really runs before
    // any GUI startup, using the same binary as the parent app.
    std::vector<std::uint8_t> encoded, framed;
    REQUIRE(ipc::encode(scan, encoded) && ipc::frame(encoded, framed));
    framed.push_back(0x42);
    QProcess trailing;
    trailing.setProgram(executable);
    trailing.setArguments({QStringLiteral("--uia-worker")});
    trailing.setStandardErrorFile(QProcess::nullDevice());
    trailing.start(QIODevice::ReadWrite);
    REQUIRE(trailing.waitForStarted(2000));
    REQUIRE(trailing.write(reinterpret_cast<const char*>(framed.data()),
                           static_cast<qint64>(framed.size())) == static_cast<qint64>(framed.size()));
    trailing.closeWriteChannel();
    REQUIRE(trailing.waitForFinished(3000));
    REQUIRE(trailing.exitStatus() == QProcess::NormalExit && trailing.exitCode() != 0);
    REQUIRE(trailing.readAllStandardOutput().isEmpty());
    std::cout << "PASS: real ClickNova worker, UIA search/verify, duplicate, PID, stale target, cancel, trailing input\n";
    return 0;
}
