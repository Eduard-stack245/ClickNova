// SPDX-License-Identifier: MIT
#include "clicknova/element_candidate_search.hpp"
#include <cassert>
#include <iostream>
#include <vector>
#include <cstdlib>
#define CHECK(expr) do { if (!(expr)) { std::cerr << "failed: " #expr " line " << __LINE__ << '\n'; std::abort(); } } while(false)
using namespace clicknova::accessibility::candidates;
int main() {
    Context ok;
    ok.expectedRow=ok.selectedRow=1; ok.actionCount=4;
    ok.workflowEditable=ok.actionEditable=ok.supportsElementSearch=true;
    ok.boundWindow=ok.targetVerified=true;ok.provider=0;
    CHECK(authorize(ok)==Refusal::None);
    CHECK(validLimits(Limits{}));
    CHECK(TraversalCompleteness{}.complete());
    TraversalCompleteness completeness;
    completeness.depthPruned = true; CHECK(!completeness.complete());
    completeness = {}; completeness.foreignSubtree = true; CHECK(!completeness.complete());
    completeness = {}; completeness.budgetExhausted = true; CHECK(!completeness.complete());
    completeness = {}; completeness.unreadableNode = true; CHECK(!completeness.complete());
    CHECK(!validLimits({0,1,1,200}));
    CHECK(!validLimits({1,0,1,200}));
    CHECK(!validLimits({1,1,0,200}));
    CHECK(!validLimits({1,1,1,0}));
    CHECK(!validLimits({513,1,1,200}));
    CHECK(!validLimits({1,33,1,200}));
    CHECK(!validLimits({1,1,11,200}));
    CHECK(!validLimits({1,1,1,3001}));
    auto c=ok;c.pickerOpen=true; CHECK(authorize(c)==Refusal::Busy);
    c=ok;c.alreadyScanning=true;CHECK(authorize(c)==Refusal::Busy);
    c=ok;c.expectedRow=-1;CHECK(authorize(c)==Refusal::NoSelection);
    c=ok;c.expectedRow=4;CHECK(authorize(c)==Refusal::NoSelection);
    c=ok;c.selectedRow=0;CHECK(authorize(c)==Refusal::StaleSelection);
    c=ok;c.workflowEditable=false;CHECK(authorize(c)==Refusal::EditorLocked);
    c=ok;c.actionEditable=false;CHECK(authorize(c)==Refusal::EditorLocked);
    c=ok;c.supportsElementSearch=false;CHECK(authorize(c)==Refusal::UnsupportedAction);
    c=ok;c.provider=1;CHECK(authorize(c)==Refusal::None);
    c=ok;c.provider=2;CHECK(authorize(c)==Refusal::None);
    c=ok;c.provider=3;CHECK(authorize(c)==Refusal::UnsupportedProvider);
    c=ok;c.provider=4;CHECK(authorize(c)==Refusal::UnsupportedProvider);
    c=ok;c.boundWindow=false;CHECK(authorize(c)==Refusal::NoBoundWindow);
    c=ok;c.targetVerified=false;CHECK(authorize(c)==Refusal::UnsafeTarget);
    std::size_t checks{};
    // Systematically test all boolean flags: each disallowed configuration
    // must be rejected, without relying on asserts removed by -DNDEBUG.
    for (unsigned mask=0;mask<128;++mask) {
        c=ok;
        c.pickerOpen=(mask&1)!=0;
        c.alreadyScanning=(mask&2)!=0;
        c.workflowEditable=(mask&4)!=0;
        c.actionEditable=(mask&8)!=0;
        c.supportsElementSearch=(mask&16)!=0;
        c.boundWindow=(mask&32)!=0;
        c.targetVerified=(mask&64)!=0;
        const bool valid=mask==124; // 0b1111100 (bit 2,3,4,5,6 only)
        CHECK((authorize(c)==Refusal::None)==valid);++checks;
    }
    Result excessive;excessive.status=Status::Complete;excessive.visited=600;
    excessive.items=std::vector<Candidate>(100,Candidate{50000,true});
    excessive=normalize(std::move(excessive),Limits{});
    CHECK(excessive.items.size()==24 && excessive.visited==320 && excessive.truncated);
    for (Status s : {Status::Cancelled,Status::UnsafeTarget,Status::Unavailable,Status::Failed,Status::Idle,Status::Searching}) {
        Result r;r.status=s;r.items={{50000,true}};r.truncated=true;
        r=normalize(std::move(r), Limits{});
        CHECK(r.items.empty() && !r.truncated);++checks;
    }
    std::cout << "PASS candidate policy, " << checks << " table cases, privacy normalization\n";
}
