// SPDX-License-Identifier: MIT
#include "clicknova/accessibility_discovery_policy.hpp"
#include <cstdlib>
#include <iostream>
#include <cstdint>
#include <cstdlib>

#define CHECK(expr) do { if (!(expr)) { std::cerr << "Check failed at " << __LINE__ << ": " #expr << "\n"; std::abort(); } } while (false)

using namespace clicknova::accessibility;
using namespace clicknova::accessibility::discovery;

int main() {
    std::uint64_t checks = 0;
    for (int op = 0; op < 2; ++op)
    for (int step = 0; step < 5; ++step)
    for (int provider = -1; provider <= 5; ++provider)
    for (int selector = 0; selector < 2; ++selector)
    for (int editable = 0; editable < 2; ++editable)
    for (int native = 0; native < 2; ++native)
    for (int busy = 0; busy < 2; ++busy) {
        Request r;
        r.operation = op == 0 ? Operation::Find : Operation::Clear;
        r.expectedRow = r.selectedRow = 2;
        r.actionCount = 3;
        r.workflowEditable = r.actionEditable = editable != 0;
        r.backendAvailable = native != 0;
        r.busy = busy != 0;
        r.facts.step = static_cast<StepKind>(step);
        r.facts.provider = provider;
        r.facts.hasName = selector != 0;
        auto assessment = inspect(r.facts);
        const bool shouldPermit = !busy && editable && native &&
            (r.operation == Operation::Find ? assessment.canRequestSearch : assessment.canRequestClear);
        CHECK((decide(r) == Refusal::None) == shouldPermit);
        ++checks;
        r.expectedRow = 1;
        if (!busy) CHECK(decide(r) == Refusal::StaleSelection);
        else CHECK(decide(r) == Refusal::Busy);
        ++checks;
        r.expectedRow = -1;
        if (!busy) CHECK(decide(r) == Refusal::NoSelection);
        ++checks;
        r.expectedRow = r.selectedRow = 2;
        r.actionCount = 2;
        if (!busy) CHECK(decide(r) == Refusal::NoSelection);
        ++checks;
    }
    Request valid;
    valid.operation=Operation::Find;
    valid.expectedRow=valid.selectedRow=0;
    valid.actionCount=1;
    valid.actionEditable=valid.workflowEditable=valid.backendAvailable=true;
    valid.facts.step=StepKind::LeftClick;
    CHECK(decide(valid)==Refusal::None);
    valid.facts.hasName=true;
    valid.facts.provider=99;
    CHECK(decide(valid)==Refusal::InvalidProvider);
    valid.operation=Operation::Clear;
    CHECK(decide(valid)==Refusal::None); // Remediation of invalid provider
    valid.facts.hasName=false;
    CHECK(decide(valid)==Refusal::NoSelector);
    std::cout << "PASS " << checks + 4 << " discovery policy checks\n";
}
