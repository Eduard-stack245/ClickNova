// SPDX-License-Identifier: MIT
// Qt-free regression suite for selector re-verification decisions.
#include "clicknova/element_candidate_search.hpp"
#include <cstdlib>
#include <iostream>
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x << '\n'; std::abort(); } } while(false)
using namespace clicknova::accessibility::candidates;
int main() {
    const Candidate id{50000, true, "okay", "Button"};
    Result live{Status::Complete, {id}, 1, false};
    CHECK(verifySnapshot(id, live).status == VerifyStatus::Verified);
    CHECK(sameIdentity(verifySnapshot(id, live).match, id));
    live.items.clear();
    CHECK(verifySnapshot(id, live).status == VerifyStatus::Missing);
    live.items.push_back(id); live.items.push_back(id);
    CHECK(verifySnapshot(id, live).status == VerifyStatus::Ambiguous);
    live.items.pop_back();
    live.items[0].enabled = false;
    CHECK(verifySnapshot(id, live).status == VerifyStatus::Unbindable);
    live.items[0] = id;
    live.truncated = true;
    CHECK(verifySnapshot(id, live).status == VerifyStatus::Failed);
    live.truncated = false;
    for (const auto status : {Status::Unavailable, Status::UnsafeTarget,
                              Status::Cancelled, Status::Failed, Status::Searching}) {
        live.status = status;
        CHECK(verifySnapshot(id, live).status == VerifyStatus::Failed);
    }
    live.status = Status::Complete;
    CHECK(verifySnapshot({50004,true,"okay","Edit"}, live).status == VerifyStatus::Unbindable);
    CHECK(verifySnapshot(id, Result{Status::Complete, {{50000,true,"different","Button"}},1,false}).status == VerifyStatus::Missing);
    CHECK(verifySnapshot(id, Result{Status::Complete, {{50011,true,"okay","Button"}},1,false}).status == VerifyStatus::Missing);
    CHECK(verifySnapshot(id, Result{Status::Complete, {{50000,true,"okay","different"}},1,false}).status == VerifyStatus::Missing);
    CHECK(TraversalCompleteness{}.complete());
    TraversalCompleteness full;
    full.depthPruned = true; CHECK(!full.complete());
    full = {}; full.foreignSubtree = true; CHECK(!full.complete());
    full = {}; full.budgetExhausted = true; CHECK(!full.complete());
    full = {}; full.unreadableNode = true; CHECK(!full.complete());
    std::cout << "PASS: unique, duplicate, missing, unreadable, disabled and truncated UIA snapshots\n";
}
