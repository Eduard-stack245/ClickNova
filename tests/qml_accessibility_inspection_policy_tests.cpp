// SPDX-License-Identifier: MIT
#include "clicknova/accessibility_inspection_policy.hpp"
#include <cstdint>
#include <iostream>
using namespace clicknova::accessibility;

static_assert(inspect({StepKind::None}).state == State::NoSelection);
static_assert(!inspect({StepKind::None}).canRequestSearch);
static_assert(inspect({StepKind::LeftClick}).canRequestSearch);
static_assert(inspect({StepKind::OtherClick}).state == State::NotApplicable);
static_assert(!inspect({StepKind::ElementTask, -1, true}).canRequestSearch);
static_assert(inspect({StepKind::ElementTask, -1, true}).canRequestClear);
static_assert(inspect({StepKind::ElementTask, 4, true}).state == State::Configured);

int main() {
    std::uint64_t cases=0;
    for (int kind=0; kind <=4; ++kind)
    for (int provider=-2; provider<=6; ++provider)
    for (unsigned bits=0; bits<64; ++bits) {
        Facts f;
        f.step=static_cast<StepKind>(kind);
        f.provider=provider;
        f.hasName=(bits & 1)!=0;
        f.hasAutomationId=(bits & 2)!=0;
        f.hasClass=(bits & 4)!=0;
        f.hasBoundWindow=(bits & 8)!=0;
        f.hasRecordedWindow=(bits & 16)!=0;
        f.hasBrowserScope=(bits & 32)!=0;
        const auto r=inspect(f);
        auto check=[&](bool v){++cases; if(!v) {std::cerr<<"Case kind="<<kind<<" provider="<<provider<<" flags="<<bits<<" failed at #"<<cases<<'\n';return false;}return true;};
        const bool chosen=kind!=0;
        const bool applicable=kind==2 || kind==4;
        const bool configured=(bits & 7)!=0;
        const bool known=provider>=0 && provider<=4;
        if (!check(r.applicable==applicable) ||
            !check(r.configured==configured) ||
            !check(r.providerKnown==known) ||
            !check(r.requiresLiveVerification) ||
            !check(r.canRequestClear == (chosen && configured)) ||
            !check(r.canRequestSearch == (applicable && (!configured || known))) ||
            !check(r.hasBoundWindow == f.hasBoundWindow) ||
            !check(r.hasRecordedWindow == f.hasRecordedWindow) ||
            !check(r.hasBrowserScope == f.hasBrowserScope)) return 1;
        const State expected = !chosen ? State::NoSelection : !applicable ? State::NotApplicable :
            !configured ? State::NoSelector : !known ? State::InvalidProvider : State::Configured;
        if (!check(r.state==expected)) return 2;
    }
    std::cout << "PASS " << cases << " accessibility policy checks\n";
    return 0;
}
