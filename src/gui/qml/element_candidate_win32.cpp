// SPDX-License-Identifier: MIT
#include "element_candidate_win32.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <UIAutomation.h>
#include <objbase.h>
#include <oleauto.h>
#include <chrono>
#include <string>
#include <cstdint>
#include <deque>
#include <utility>

namespace clicknova::gui::qml {
namespace {
using namespace accessibility::candidates;
template<class T> struct ComPtr {
    T* ptr{};
    ComPtr() = default;
    explicit ComPtr(T* p) : ptr(p) {}
    ~ComPtr() { if (ptr) ptr->Release(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& o) noexcept : ptr(std::exchange(o.ptr,nullptr)) {}
    ComPtr& operator=(ComPtr&& o) noexcept { if(this != &o) { if(ptr)ptr->Release();ptr=std::exchange(o.ptr,nullptr);} return *this; }
    T** put() { return &ptr; }
    T* get() const { return ptr; }
    explicit operator bool() const { return ptr != nullptr; }
};
// Reject malformed or overly large identifiers. Never query Name or Value.
std::string privateId(BSTR value) {
    if (!value) return {};
    const auto length = SysStringLen(value);
    if (length == 0 || length > 128) return {};
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value,
                                     static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (n <= 0 || n > 384) return {};
    std::string utf8(static_cast<std::size_t>(n), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value,
                            static_cast<int>(length), utf8.data(), n, nullptr, nullptr)) return {};
    return safeIdentity(utf8) ? utf8 : std::string{};
}
bool targetLive(Target target) {
    auto hwnd = reinterpret_cast<HWND>(target.nativeHandle);
    if (!target.processId || !hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return false;
    DWORD pid{};
    GetWindowThreadProcessId(hwnd,&pid);
    return pid == target.processId && GetAncestor(hwnd,GA_ROOT) == hwnd;
}
class UiaProvider final : public IProvider {
public:
    Result enumerate(Target target, Limits limits, std::stop_token cancel) override {
        Result r{Status::Unavailable};
        if (!validLimits(limits)) { r.status=Status::Failed; return r; }
        if (!targetLive(target)) { r.status=Status::UnsafeTarget; return r; }
        const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(init)) { r.status=Status::Unavailable; return r; }
        struct CoCleanup { ~CoCleanup(){ CoUninitialize(); } } cleanup;
        ComPtr<IUIAutomation> uia;
        if (FAILED(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(uia.put()))) || !uia) { r.status=Status::Unavailable; return r; }
        ComPtr<IUIAutomationTreeWalker> walker;
        if (FAILED(uia.get()->get_ControlViewWalker(walker.put())) || !walker) {
            r.status=Status::Failed; return r;
        }
        ComPtr<IUIAutomationElement> root;
        if (FAILED(uia.get()->ElementFromHandle(reinterpret_cast<HWND>(target.nativeHandle),root.put())) || !root) {
            r.status=Status::UnsafeTarget; return r;
        }
        int rootPid{};
        if (FAILED(root.get()->get_CurrentProcessId(&rootPid)) ||
            static_cast<std::uint32_t>(rootPid) != target.processId) {
            r.status=Status::UnsafeTarget; return r;
        }
        const auto started = std::chrono::steady_clock::now();
        const auto exhausted = [&] {
            return cancel.stop_requested() || !targetLive(target) ||
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now()-started).count() >= limits.maxElapsedMs;
        };
        struct Node { ComPtr<IUIAutomationElement> element; unsigned depth; };
        std::deque<Node> pending;
        TraversalCompleteness coverage{};
        // Root itself is a target container, not an actionable candidate.
        pending.push_back({std::move(root), 0});
        r.status=Status::Complete;
        while (!pending.empty() && r.visited < limits.maxVisited && r.items.size() < limits.maxCandidates) {
            if (cancel.stop_requested()) { r.status=Status::Cancelled; break; }
            if (!targetLive(target)) { r.status=Status::UnsafeTarget; break; }
            if (exhausted()) { coverage.budgetExhausted=true; break; }
            Node node=std::move(pending.front());pending.pop_front();
            ++r.visited;
            int pid{};
            if (FAILED(node.element.get()->get_CurrentProcessId(&pid))) {
                r.status=Status::Failed; break; // Cannot claim uniqueness with an unreadable node.
            }
            if (static_cast<std::uint32_t>(pid) != target.processId) {
                // Cross-process UIA subtrees are not examined. This is a partial
                // traversal and may never establish selector uniqueness.
                coverage.foreignSubtree = true;
                continue;
            }
            if (node.depth != 0) {
                CONTROLTYPEID controlType{};
                BOOL isOffscreen=TRUE, enabled=FALSE, isPassword=TRUE;
                if (FAILED(node.element.get()->get_CurrentControlType(&controlType)) ||
                    FAILED(node.element.get()->get_CurrentIsOffscreen(&isOffscreen)) ||
                    FAILED(node.element.get()->get_CurrentIsEnabled(&enabled)) ||
                    FAILED(node.element.get()->get_CurrentIsPassword(&isPassword))) {
                    r.status=Status::Failed; break;
                }
                if (!isOffscreen && !isPassword) {
                    Candidate candidate{static_cast<int>(controlType), enabled != FALSE};
                    if (candidate.enabled && supportedRole(candidate.controlType)) {
                        BSTR automationId=nullptr, className=nullptr;
                        const auto first=node.element.get()->get_CurrentAutomationId(&automationId);
                        const auto second=node.element.get()->get_CurrentClassName(&className);
                        if (SUCCEEDED(first)) candidate.automationId=privateId(automationId);
                        if (SUCCEEDED(second)) candidate.className=privateId(className);
                        SysFreeString(automationId);
                        SysFreeString(className);
                        if (FAILED(first) || FAILED(second)) { r.status=Status::Failed; break; }
                    }
                    r.items.push_back(std::move(candidate));
                }
            }
            if (node.depth >= limits.maxDepth) {
                // A maximum-depth node may have descendants containing another
                // element with the same AutomationId/ClassName/ControlType.
                // Do not mistake a depth-pruned tree for an exhaustive search.
                ComPtr<IUIAutomationElement> unseenChild;
                if (FAILED(walker.get()->GetFirstChildElement(
                        node.element.get(), unseenChild.put()))) {
                    r.status = Status::Failed;
                    break;
                }
                if (unseenChild) coverage.depthPruned = true;
                continue;
            }
            ComPtr<IUIAutomationElement> sibling;
            if (FAILED(walker.get()->GetFirstChildElement(node.element.get(),sibling.put()))) {
                r.status=Status::Failed; break;
            }
            while (sibling && pending.size() < limits.maxVisited) {
                if (exhausted()) { coverage.budgetExhausted=true;break; }
                ComPtr<IUIAutomationElement> next;
                if (FAILED(walker.get()->GetNextSiblingElement(sibling.get(),next.put()))) {
                    r.status=Status::Failed; break;
                }
                pending.push_back({std::move(sibling),node.depth+1});
                sibling=std::move(next);
            }
            if (r.status == Status::Failed) break;
        }
        if (r.visited >= limits.maxVisited || !pending.empty() || r.items.size() >= limits.maxCandidates)
            coverage.budgetExhausted = true;
        // For verification, every child of every traversable node must have
        // been accounted for, not just the first N visible candidates.
        r.truncated = !coverage.complete();
        if (cancel.stop_requested()) r.status=Status::Cancelled;
        if (!targetLive(target)) r.status=Status::UnsafeTarget;
        return normalize(std::move(r),limits);
    }
    VerifyResult verify(Target target, const Candidate& candidate,
                        std::stop_token cancel) override {
        if (cancel.stop_requested()) return {VerifyStatus::Cancelled, {}};
        if (!eligible(candidate)) return {VerifyStatus::Unbindable, {}};
        if (!targetLive(target)) return {VerifyStatus::UnsafeTarget, {}};
        // Full bounded re-enumeration, NEVER reuse positional index or COM pointer.
        // If the tree is too large or changes while scanning, refuse binding.
        const Limits verifyLimits{512, 32, 10, 3000};
        auto live = enumerate(target, verifyLimits, cancel);
        if (cancel.stop_requested()) return {VerifyStatus::Cancelled, {}};
        if (live.status == Status::UnsafeTarget) return {VerifyStatus::UnsafeTarget, {}};
        const auto outcome = verifySnapshot(candidate, live);
        if (!targetLive(target)) return {VerifyStatus::UnsafeTarget, {}};
        return outcome;
    }
};
} // namespace
std::shared_ptr<accessibility::candidates::IProvider> createNativeUiaCandidateProvider() {
    return std::make_shared<UiaProvider>();
}
} // namespace clicknova::gui::qml
#else
namespace clicknova::gui::qml {
std::shared_ptr<accessibility::candidates::IProvider> createNativeUiaCandidateProvider() {
    return {};
}
}
#endif
