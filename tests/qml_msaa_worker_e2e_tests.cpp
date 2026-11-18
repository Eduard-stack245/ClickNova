// SPDX-License-Identifier: MIT
// Optional real Windows desktop test of read-only MSAA via ClickNova.exe --uia-worker.
// No input injection, Invoke, Java bridge loading or external application access.
#ifndef _WIN32
#error This test requires an interactive Windows desktop.
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "clicknova/uia_worker_protocol.hpp"
#include "../src/gui/qml/uia_isolated_transport.hpp"
#include <QCoreApplication>
#include <QFileInfo>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <stop_token>
#include <thread>

namespace ca=clicknova::accessibility::candidates;
namespace ipc=clicknova::accessibility::ipc;
namespace transport=clicknova::gui::qml::uia_transport;
#define REQUIRE(x) do {if(!(x)){std::cerr<<"Phase21 failed line "<<__LINE__<<": "<<#x<<'\n';std::abort();}}while(false)
namespace {
LRESULT CALLBACK wndProc(HWND w,UINT m,WPARAM a,LPARAM b) {
    if(m==WM_CLOSE){DestroyWindow(w);return 0;}
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(w,m,a,b);
}
void fixture(std::promise<ca::Target> ready) {
    HINSTANCE app=GetModuleHandleW(nullptr);
    WNDCLASSW wc{};wc.lpfnWndProc=wndProc;wc.hInstance=app;
    wc.lpszClassName=L"ClickNovaPhase21MsaaTest";
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){
        ready.set_exception(std::make_exception_ptr(std::runtime_error("register failed")));return;
    }
    HWND w=CreateWindowExW(0,wc.lpszClassName,L"Local fixture",
        WS_OVERLAPPEDWINDOW,50,50,350,170,nullptr,nullptr,app,nullptr);
    if(!w){ready.set_exception(std::make_exception_ptr(std::runtime_error("window failed")));return;}
    if(!CreateWindowExW(0,L"BUTTON",L"Disposable button",WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON,
       20,20,200,30,w,reinterpret_cast<HMENU>(static_cast<INT_PTR>(4123)),app,nullptr)){
        DestroyWindow(w);ready.set_exception(std::make_exception_ptr(std::runtime_error("button failed")));return;
    }
    ShowWindow(w,SW_SHOWNOACTIVATE);UpdateWindow(w);
    ready.set_value({reinterpret_cast<std::uintptr_t>(w),GetCurrentProcessId()});
    MSG msg{};
    while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
}
ipc::Request request(ca::Target target,ipc::Provider provider,std::uint64_t nonce) {
    ipc::Request r;r.operation=ipc::Operation::Scan;
    r.provider=provider;r.target=target;r.nonce=nonce;
    r.limits={320,24,6,1200};return r;
}
transport::Transaction ask(const QString& binary,const ipc::Request& r) {
    return transport::transact(binary,{QStringLiteral("--uia-worker")},r,4000,{});
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    REQUIRE(argc==2);
    HDESK desktop=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
    if(!desktop){std::cout<<"SKIP no interactive desktop\n";return 77;}
    CloseDesktop(desktop);
    const QString binary=QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    REQUIRE(QFileInfo(binary).isFile());
    std::promise<ca::Target> signal;
    auto ready=signal.get_future();
    std::jthread ui([p=std::move(signal)]()mutable {fixture(std::move(p));});
    REQUIRE(ready.wait_for(std::chrono::seconds(5))==std::future_status::ready);
    const ca::Target target=ready.get();
    const auto msaa=request(target,ipc::Provider::Msaa,491);
    const auto found=ask(binary,msaa);
    REQUIRE(found.outcome==transport::Outcome::Success);
    REQUIRE(found.reply.provider==ipc::Provider::Msaa);
    REQUIRE(found.reply.scan.status==ca::Status::Complete);
    REQUIRE(found.reply.scan.visited>=1);
    // A real Win32 button should be present in the disposable MSAA tree.
    REQUIRE(std::any_of(found.reply.scan.items.begin(),found.reply.scan.items.end(),
        [](const ca::Candidate& c){return c.controlType==50000;}));
    for(const auto& c:found.reply.scan.items){
        REQUIRE(c.automationId.empty()&&c.className.empty());
        REQUIRE(!ca::eligible(c));
    }
    auto wrong=msaa;wrong.nonce=492;wrong.target.processId++;
    const auto refused=ask(binary,wrong);
    REQUIRE(refused.outcome==transport::Outcome::Success);
    REQUIRE(refused.reply.scan.status==ca::Status::UnsafeTarget);
    auto java=request(target,ipc::Provider::Java,493);
    const auto unsupported=ask(binary,java);
    REQUIRE(unsupported.outcome==transport::Outcome::Success);
    REQUIRE(unsupported.reply.provider==ipc::Provider::Java);
    REQUIRE(unsupported.reply.scan.status==ca::Status::Unavailable);
    std::stop_source cancel;cancel.request_stop();
    REQUIRE(transport::transact(binary,{QStringLiteral("--uia-worker")},msaa,4000,
        cancel.get_token()).outcome==transport::Outcome::Cancelled);
    REQUIRE(PostMessageW(reinterpret_cast<HWND>(target.nativeHandle),WM_CLOSE,0,0));
    ui.join();
    const auto gone=ask(binary,msaa);
    REQUIRE(gone.outcome==transport::Outcome::Success);
    REQUIRE(gone.reply.scan.status==ca::Status::UnsafeTarget);
    std::cout<<"PASS real MSAA child read-only, process validation, Java unavailable, cancel and target exit\n";
}
