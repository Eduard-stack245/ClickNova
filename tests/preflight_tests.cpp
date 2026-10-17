// SPDX-License-Identifier: Apache-2.0
#include "clicknova/input_actions.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
using namespace clicknova::input;
namespace {
bool has(const PreflightReport& result, PreflightRisk code, std::size_t row) {
    for (auto issue : result.issues) if (issue.risk == code && issue.stepIndex == row) return true;
    return false;
}
struct FailBackend final : IBackend {
    int calls{0};
    bool dispatch(const Event&) override { return ++calls < 3; }
};
struct Waiter final : IWaiter {
    bool wait(std::int64_t, std::stop_token) override { return true; }
};
}
int main() {
    assert(!preflight({}).validationError.empty());
    const Plan simple{{Click{Button::Left, 1, 100}, Wait{100}}};
    auto report = preflight(simple);
    assert(report.validationError.empty());
    assert(report.estimatedClickCount == 1 && report.minimumDurationMs == 100);
    assert(has(report, PreflightRisk::UnboundForegroundInput, 0));
    assert(!report.confirmationRecommended);

    const Plan many{{Wait{200}, Click{Button::Left, 501, 10}}, 3, 500};
    report = preflight(many);
    assert(report.confirmationRecommended);
    assert(has(report, PreflightRisk::ManyClicks, 1));
    assert(report.estimatedClickCount == 1503);
    assert(report.minimumDurationMs == 3 * (200 + 500 * 10) + 2 * 500);

    const Plan script{{RunScript{u"Write-Host hi"}}};
    report = preflight(script);
    assert(has(report, PreflightRisk::LocalScript, 0));
    assert(report.confirmationRecommended);

    const Plan hold{{KeyDown{0x11}, Wait{200}, KeyUp{0x11}}};
    report = preflight(hold);
    assert(has(report, PreflightRisk::ManualHeldInput, 0));
    assert(report.confirmationRecommended);

    const Plan slow{{Wait{30LL * 60 * 1000}}, 1};
    report = preflight(slow);
    assert(has(report, PreflightRisk::LongMinimumDuration, 0));
    assert(report.confirmationRecommended);

    const Plan saturated{{Click{Button::Left, 5000000000LL, 60000}},
                         std::numeric_limits<int>::max(), std::numeric_limits<std::int64_t>::max()};
    report = preflight(saturated);
    assert(report.validationError.empty());
    assert(report.minimumDurationMs == std::numeric_limits<std::int64_t>::max());
    assert(report.estimatedClickCount == 5000000000ULL * static_cast<unsigned long long>(std::numeric_limits<int>::max()));

    const Plan bound{{Click{Button::Left, 1, 0}}, 1, 0, true};
    report = preflight(bound);
    assert(!has(report, PreflightRisk::UnboundForegroundInput, 0));

    // Source action diagnostics cannot be confused with a visual tree/group row.
    FailBackend backend;
    Waiter waiter;
    const auto execution = play({{Wait{0}, KeyTap{0x41}, KeyTap{0x42}}}, backend, waiter);
    assert(execution.status == Status::DispatchFailed);
    assert(execution.failedStepIndex && *execution.failedStepIndex == 2);
    assert(execution.failedPassNumber == 1);
}
