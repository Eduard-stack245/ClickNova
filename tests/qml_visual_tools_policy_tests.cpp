// SPDX-License-Identifier: MIT
#include "clicknova/visual_tools_policy.hpp"
#include <cstdlib>
#include <iostream>
using namespace clicknova::visual::tools;
#define EXPECT(expression) do { if (!(expression)) { \
    std::cerr << "failed: " #expression << " (line " << __LINE__ << ")\n"; \
    return EXIT_FAILURE; \
} } while(false)
int main() {
    Facts f;
    f.expectedRow=0; f.selectedRow=0; f.actionCount=1;
    f.supportedAction=true; f.enforcedWindow=true;
    f.workflowEditable=true; f.actionEditable=true; f.nativeAvailable=true;
    EXPECT(assess(f).canOpen);
    f.savedProvider=4; f.hasSelector=true;
    EXPECT(assess(f).saved==SavedProvider::Ocr);
    f.savedProvider=3;
    EXPECT(assess(f).saved==SavedProvider::Image);
    f.discoveryBusy=true;
    EXPECT(assess(f).refusal==Refusal::DiscoveryBusy);
    f.discoveryBusy=false; f.expectedRow=1;
    EXPECT(assess(f).refusal==Refusal::NoSelection);
    f.expectedRow=0; f.enforcedWindow=false;
    EXPECT(assess(f).refusal==Refusal::UnboundWindow);
    f.enforcedWindow=true; f.workflowEditable=false;
    EXPECT(assess(f).refusal==Refusal::EditorLocked);
    f.workflowEditable=true; f.nativeAvailable=false;
    EXPECT(assess(f).refusal==Refusal::NativeUnavailable);
    return EXIT_SUCCESS;
}
