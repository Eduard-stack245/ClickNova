// SPDX-License-Identifier: MIT
#include "element_candidate_msaa_win32.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <oleacc.h>
#include <objbase.h>
#include <chrono>
#include <deque>
#include <utility>

namespace clicknova::gui::qml {
namespace {
using namespace accessibility::candidates;
template<typename T> class OwnedCom {
public:
    OwnedCom() = default;
    explicit OwnedCom(T* p): ptr_(p) {}
    OwnedCom(OwnedCom&& o) noexcept : ptr_(std::exchange(o.ptr_, nullptr)) {}
    OwnedCom& operator=(OwnedCom&& o) noexcept {
        if(this != &o) { if(ptr_)ptr_->Release();ptr_=std::exchange(o.ptr_,nullptr); }
        return *this;
    }
    OwnedCom(const OwnedCom&) = delete;
    OwnedCom& operator=(const OwnedCom&) = delete;
    ~OwnedCom() { if(ptr_)ptr_->Release(); }
    T* get() const noexcept {return ptr_;}
    T** out() noexcept {return &ptr_;}
    explicit operator bool() const noexcept {return ptr_!=nullptr;}
private:
    T* ptr_{};
};

bool validWindow(Target t) noexcept {
    const HWND hwnd=reinterpret_cast<HWND>(t.nativeHandle);
    if(!hwnd || !t.processId || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd) ||
       GetAncestor(hwnd, GA_ROOT)!=hwnd) return false;
    DWORD actualPid{};
    GetWindowThreadProcessId(hwnd,&actualPid);
    return actualPid==t.processId;
}

int genericType(long role) noexcept {
    // Use UIA-like numeric categories ONLY for display, not for identity.
    switch(role) {
    case ROLE_SYSTEM_PUSHBUTTON: return 50000; // button
    case ROLE_SYSTEM_LINK: return 50005;       // hyperlink
    case ROLE_SYSTEM_MENUITEM: return 50011;   // menu item
    case ROLE_SYSTEM_CHECKBUTTON: return 50002;
    case ROLE_SYSTEM_COMBOBOX: return 50003;
    case ROLE_SYSTEM_TEXT: return 50020;
    case ROLE_SYSTEM_LISTITEM: return 50007;
    case ROLE_SYSTEM_LIST: return 50008;
    case ROLE_SYSTEM_MENUPOPUP: return 50009;
    case ROLE_SYSTEM_RADIOBUTTON: return 50013;
    case ROLE_SYSTEM_WINDOW: return 50032;
    default: return 50025; // other/custom; no implicit authorization
    }
}

bool readRole(IAccessible* acc, long childId, Candidate& candidate) {
    VARIANT self{};self.vt=VT_I4;self.lVal=childId;
    VARIANT role{};VariantInit(&role);
    const HRESULT roleResult=acc->get_accRole(self,&role);
    if(FAILED(roleResult) || role.vt!=VT_I4) {VariantClear(&role);return false;}
    const long value=role.lVal;
    VariantClear(&role);
    VARIANT state{};VariantInit(&state);
    const HRESULT stateResult=acc->get_accState(self,&state);
    if(FAILED(stateResult) || state.vt!=VT_I4) {VariantClear(&state);return false;}
    const long flags=state.lVal;
    VariantClear(&state);
    candidate.controlType=genericType(value);
    candidate.enabled=(flags & (STATE_SYSTEM_UNAVAILABLE | STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_PROTECTED))==0;
    // A MSAA role/child ID is never a stable selector. Do NOT query accName,
    // accValue, accDescription, or expose childId, HWND or IDispatch in IPC.
    return true;
}
struct Node { OwnedCom<IAccessible> item; unsigned depth{}; };

class MsaaProvider final: public IProvider {
public:
    Result enumerate(Target target,Limits limits,std::stop_token stop) override {
        Result r{Status::Unavailable};
        if (!validLimits(limits)) return {Status::Failed,{},0,false};
        if (!validWindow(target)) return {Status::UnsafeTarget,{},0,false};
        const HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        if(FAILED(init)) return r;
        struct Coinit {~Coinit(){CoUninitialize();}} cleanup;
        OwnedCom<IAccessible> root;
        if(FAILED(AccessibleObjectFromWindow(reinterpret_cast<HWND>(target.nativeHandle),
                    static_cast<LONG>(OBJID_CLIENT),IID_IAccessible,reinterpret_cast<void**>(root.out()))) || !root)
            return r;
        r.status=Status::Complete;
        const auto begun=std::chrono::steady_clock::now();
        const auto expired=[&] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now()-begun).count()>=limits.maxElapsedMs;
        };
        std::deque<Node> todo;
        todo.push_back({std::move(root),0});
        r.visited=1; // Count root once; child roles count when discovered.
        while(!todo.empty()) {
            if(stop.stop_requested()) {r.status=Status::Cancelled;break;}
            if(!validWindow(target)) {r.status=Status::UnsafeTarget;break;}
            if(expired() || r.visited>=limits.maxVisited ||
               r.items.size()>=limits.maxCandidates) {r.truncated=true;break;}
            Node node=std::move(todo.front());todo.pop_front();
            // The root is a container. We do not need its name, value or text.
            LONG total{};
            if(FAILED(node.item.get()->get_accChildCount(&total)) || total<0) {
                r.status=Status::Failed;break;
            }
            if(total>0 && node.depth>=limits.maxDepth) {r.truncated=true;continue;}
            // A very wide subtree cannot be claimed complete.
            if(total>static_cast<LONG>(limits.maxVisited)) r.truncated=true;
            const LONG available=static_cast<LONG>(limits.maxVisited);
            for(LONG id=1;id<=total && id<=available;++id) {
                if(stop.stop_requested()){r.status=Status::Cancelled;break;}
                if(!validWindow(target)){r.status=Status::UnsafeTarget;break;}
                if(expired() || r.visited>=limits.maxVisited) {r.truncated=true;break;}
                ++r.visited;
                VARIANT selector{};selector.vt=VT_I4;selector.lVal=id;
                OwnedCom<IDispatch> dispatch;
                const HRESULT hr=node.item.get()->get_accChild(selector,dispatch.out());
                if(FAILED(hr)) {r.status=Status::Failed;break;}
                if(dispatch) {
                    OwnedCom<IAccessible> sub;
                    if(FAILED(dispatch.get()->QueryInterface(IID_IAccessible,
                        reinterpret_cast<void**>(sub.out()))) || !sub) {
                        r.status=Status::Failed;break;
                    }
                    Candidate role{};
                    if(!readRole(sub.get(),CHILDID_SELF,role)) {r.status=Status::Failed;break;}
                    if(r.items.size()<limits.maxCandidates) r.items.push_back(role);
                    else r.truncated=true;
                    todo.push_back({std::move(sub),node.depth+1});
                } else {
                    // Simple MSAA child. Parent supplies the role/state; it has
                    // no separately navigable IAccessible subtree.
                    Candidate role{};
                    if(!readRole(node.item.get(),id,role)){r.status=Status::Failed;break;}
                    if(r.items.size()<limits.maxCandidates) r.items.push_back(role);
                    else r.truncated=true;
                }
                if(r.items.size()>=limits.maxCandidates && (id<total || !todo.empty())) {
                    r.truncated=true;break;
                }
            }
            if(r.status!=Status::Complete || r.truncated) break;
        }
        if(stop.stop_requested())r.status=Status::Cancelled;
        if(!validWindow(target))r.status=Status::UnsafeTarget;
        // Failure/partial results are never shown after an error. In all
        // cases, identities remain empty and binding is not possible.
        return normalize(std::move(r),limits);
    }
    VerifyResult verify(Target,const Candidate&,std::stop_token) override {
        return {VerifyStatus::Unbindable,{}}; // NEVER verify a MSAA ordinal.
    }
};
} // namespace
std::shared_ptr<accessibility::candidates::IProvider> createNativeMsaaCandidateProvider() {
    return std::make_shared<MsaaProvider>();
}
} // namespace clicknova::gui::qml
#else
namespace clicknova::gui::qml {
std::shared_ptr<accessibility::candidates::IProvider> createNativeMsaaCandidateProvider(){return {};}
}
#endif
