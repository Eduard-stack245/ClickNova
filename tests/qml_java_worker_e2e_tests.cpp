// SPDX-License-Identifier: MIT
// Opt-in only: interactive JVM and WindowsAccessBridge-64.dll are required.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "clicknova/uia_worker_protocol.hpp"
#include "../src/gui/qml/uia_isolated_transport.hpp"
#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QElapsedTimer>
#include <QThread>
#include <cstdlib>
#include <algorithm>
#include <iostream>
namespace ipc=clicknova::accessibility::ipc;
namespace ca=clicknova::accessibility::candidates;
namespace transport=clicknova::gui::qml::uia_transport;
#define CHECK(x) do {if (!(x)){std::cerr << "Java worker E2E FAIL " << __LINE__ << ": " << #x << '\n';std::abort();}} while(false)
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    CHECK(argc == 3);
    HDESK desktop=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
    if (!desktop) { std::cout << "SKIP no interactive desktop\n"; return 77; }
    CloseDesktop(desktop);
    const QString home=qEnvironmentVariable("JAVA_HOME");
    const QString java=home+QStringLiteral("/bin/java.exe");
    const QString source=QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
    const QString executable=QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    if (home.isEmpty() || !QFileInfo(java).isFile() || !QFileInfo(source).isFile()) {
        std::cout << "SKIP Java JDK or fixture missing\n";return 77;
    }
    QProcess fixture;
    fixture.setProgram(java);
    fixture.setArguments({source}); // Java 11+ source-file launcher
    fixture.setStandardOutputFile(QProcess::nullDevice());
    fixture.setStandardErrorFile(QProcess::nullDevice());
    fixture.start();
    if (!fixture.waitForStarted(4000)) {std::cout<<"SKIP Java fixture could not start\n";return 77;}
    auto cleanup=[&] {fixture.kill(); (void)fixture.waitForFinished(2000);};
    HWND target{};
    QElapsedTimer timer;timer.start();
    while(timer.elapsed()<13000 && fixture.state()!=QProcess::NotRunning) {
        target=FindWindowW(nullptr,L"ClickNovaPhase22JavaFixture");
        if(target) break;
        QThread::msleep(80);
    }
    if(!target) {cleanup();std::cout<<"SKIP Swing fixture unavailable\n";return 77;}
    DWORD pid{};GetWindowThreadProcessId(target,&pid);
    ipc::Request request;
    request.provider=ipc::Provider::Java;
    request.operation=ipc::Operation::Scan;
    request.target={reinterpret_cast<std::uintptr_t>(target),pid};
    request.nonce=0x220f5u;
    request.limits={320,24,6,1200};
    auto found=transport::transact(executable,{QStringLiteral("--uia-worker")},request,4000,{});
    if (found.outcome != transport::Outcome::Success ||
        found.reply.scan.status == ca::Status::Unavailable) {
        cleanup();std::cout<<"SKIP JAB not installed or disabled\n";return 77;
    }
    CHECK(found.reply.provider==ipc::Provider::Java);
    CHECK(found.reply.scan.status==ca::Status::Complete);
    CHECK(found.reply.scan.visited>=1);
    CHECK(std::any_of(found.reply.scan.items.begin(), found.reply.scan.items.end(),
        [](const ca::Candidate& c){return c.controlType==50000;}));
    for(const auto& c:found.reply.scan.items) {
        CHECK(c.automationId.empty()&&c.className.empty());
        CHECK(!ca::eligible(c));
    }
    auto wrong=request;wrong.target.processId++;wrong.nonce++;
    auto refused=transport::transact(executable,{QStringLiteral("--uia-worker")},wrong,4000,{});
    CHECK(refused.outcome==transport::Outcome::Success);
    CHECK(refused.reply.scan.status==ca::Status::UnsafeTarget);
    std::stop_source cancel;cancel.request_stop();
    CHECK(transport::transact(executable,{QStringLiteral("--uia-worker")},request,4000,
        cancel.get_token()).outcome==transport::Outcome::Cancelled);
    CHECK(!ipc::permitted(ipc::Provider::Java,ipc::Operation::Verify));
    cleanup();
    std::cout<<"PASS Java Access Bridge isolated read-only worker, target validation and cancellation\n";
}
#else
int main(){return 77;}
#endif
