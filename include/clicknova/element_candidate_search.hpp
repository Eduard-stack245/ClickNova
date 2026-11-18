// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <stop_token>
#include <vector>

namespace clicknova::accessibility::candidates {
// This read-only discovery layer deliberately cannot invoke or bind an element.
// In particular, a candidate number is NOT a persistent UIA selector.
enum class Status { Idle, Searching, Complete, Cancelled, Unavailable, UnsafeTarget, Failed };
enum class VerifyStatus { Verified, Unbindable, Missing, Ambiguous, Cancelled, UnsafeTarget, Failed };
enum class Refusal { None, Busy, NoSelection, StaleSelection, EditorLocked,
                     UnsupportedAction, UnsupportedProvider, NoBoundWindow,
                     UnsafeTarget };
struct Target {
    std::uintptr_t nativeHandle{};
    std::uint32_t processId{};
};
struct Context {
    int expectedRow{-1};
    int selectedRow{-1};
    std::size_t actionCount{};
    bool workflowEditable{};
    bool actionEditable{};
    bool pickerOpen{};
    bool alreadyScanning{};
    bool supportsElementSearch{};
    int provider{-1}; // 0: UIA; 1: MSAA; 2: Java (read-only)
    bool boundWindow{};
    bool targetVerified{};
};
constexpr Refusal authorize(const Context& c) noexcept {
    if (c.pickerOpen || c.alreadyScanning) return Refusal::Busy;
    if (c.expectedRow < 0 || c.selectedRow < 0 ||
        static_cast<std::size_t>(c.expectedRow) >= c.actionCount ||
        static_cast<std::size_t>(c.selectedRow) >= c.actionCount) return Refusal::NoSelection;
    if (c.expectedRow != c.selectedRow) return Refusal::StaleSelection;
    if (!c.workflowEditable || !c.actionEditable) return Refusal::EditorLocked;
    if (!c.supportsElementSearch) return Refusal::UnsupportedAction;
    // Read-only discovery is available for UIA, MSAA and Java. Only UIA
    // is currently eligible for fresh-tree binding verification.
    if (c.provider != 0 && c.provider != 1 && c.provider != 2)
        return Refusal::UnsupportedProvider;
    if (!c.boundWindow) return Refusal::NoBoundWindow;
    if (!c.targetVerified) return Refusal::UnsafeTarget;
    return Refusal::None;
}
struct Limits {
    std::size_t maxVisited{320};
    std::size_t maxCandidates{24};
    unsigned maxDepth{6};
    int maxElapsedMs{1200}; // cooperative budget; never claimed as hard COM timeout
};
constexpr bool validLimits(const Limits& l) noexcept {
    return l.maxVisited >= 1 && l.maxVisited <= 512 &&
           l.maxCandidates >= 1 && l.maxCandidates <= 32 &&
           l.maxDepth >= 1 && l.maxDepth <= 10 &&
           l.maxElapsedMs >= 50 && l.maxElapsedMs <= 3000;
}
// Only an exhaustive scan can establish uniqueness. A skipped descendant,
// cross-process subtree or reached resource limit invalidates that proof.
struct TraversalCompleteness {
    bool depthPruned{};
    bool foreignSubtree{};
    bool budgetExhausted{};
    bool unreadableNode{};
    constexpr bool complete() const noexcept {
        return !depthPruned && !foreignSubtree && !budgetExhausted && !unreadableNode;
    }
};
struct Candidate {
    int controlType{};   // numeric UIA type shown to QML as generic role
    bool enabled{};
    // Native-only volatile identity, NEVER copied into a QVariant or log.
    // An AutomationId is not globally unique; verify the whole bounded tree
    // again before turning it into a persisted selector.
    std::string automationId{};
    std::string className{};
};
constexpr int supportedRole(int type) noexcept {
    return type == 50000 ? 1 : type == 50005 ? 4 : type == 50011 ? 7 : 0;
}
inline bool safeIdentity(const std::string& s) noexcept {
    if (s.empty() || s.size() > 384) return false;
    for (unsigned char c : s) if (c < 0x20 || c == 0x7f) return false;
    return true;
}
inline bool eligible(const Candidate& c) noexcept {
    return c.enabled && supportedRole(c.controlType) != 0 &&
           safeIdentity(c.automationId) && safeIdentity(c.className);
}
inline bool sameIdentity(const Candidate& a, const Candidate& b) noexcept {
    return a.controlType == b.controlType &&
        a.automationId == b.automationId && a.className == b.className;
}
struct VerifyResult {
    VerifyStatus status{VerifyStatus::Failed};
    Candidate match{};
};
struct Result {
    Status status{Status::Unavailable};
    std::vector<Candidate> items;
    std::size_t visited{};
    bool truncated{};
};
// Pure, deterministic uniqueness decision shared by Win32 UIA and portable
// regression tests. Never accept a partly traversed or failed live tree.
inline VerifyResult verifySnapshot(const Candidate& requested, const Result& live) {
    if (!eligible(requested)) return {VerifyStatus::Unbindable, {}};
    if (live.status != Status::Complete || live.truncated) return {VerifyStatus::Failed, {}};
    std::size_t hits = 0;
    Candidate matched;
    for (const auto& current : live.items) {
        if (sameIdentity(requested, current)) {
            ++hits;
            if (hits == 1) matched = current;
            if (hits > 1) return {VerifyStatus::Ambiguous, {}};
        }
    }
    if (!hits) return {VerifyStatus::Missing, {}};
    if (!eligible(matched)) return {VerifyStatus::Unbindable, {}};
    return {VerifyStatus::Verified, std::move(matched)};
}
class IProvider {
public:
    virtual ~IProvider() = default;
    virtual Result enumerate(Target target, Limits limits, std::stop_token cancel) = 0;
    // Must re-read a *fresh* tree, prove exactly one matching live element,
    // and fail closed on any truncation. Never invoke the element.
    virtual VerifyResult verify(Target, const Candidate&, std::stop_token) {
        return {VerifyStatus::Unbindable, {}};
    }
};
// Defensive normalization even when a future provider returns excessive output.
inline Result normalize(Result r, const Limits& l) {
    if (!validLimits(l)) return Result{Status::Failed, {}, 0, false};
    if (r.status == Status::Idle || r.status == Status::Searching)
        r.status = Status::Failed;
    if (r.status != Status::Complete) {
        r.items.clear(); // never show partial results after failure/cancellation
        r.truncated = false;
    }
    if (r.items.size() > l.maxCandidates) {
        r.items.resize(l.maxCandidates);
        r.truncated = true;
    }
    if (r.visited > l.maxVisited) r.visited = l.maxVisited;
    return r;
}
} // namespace clicknova::accessibility::candidates
