// SPDX-License-Identifier: Apache-2.0
#include "clicknova/window_target.hpp"

namespace clicknova::platform {

TargetCheck evaluateWindowTarget(const TargetSnapshot& state) {
    if (!state.exists) return TargetCheck::Missing;
    if (!state.visible) return TargetCheck::Hidden;
    if (state.minimized) return TargetCheck::Minimized;
    if (!state.foreground) return TargetCheck::NotForeground;
    if (!state.insideClient) return TargetCheck::OutOfBounds;
    if (!state.unobscured) return TargetCheck::Obscured;
    return TargetCheck::Ready;
}

const char* targetCheckMessage(TargetCheck status) {
    switch (status) {
    case TargetCheck::Ready: return "Target window is ready";
    case TargetCheck::Unsupported: return "Window targeting is not implemented on this platform";
    case TargetCheck::Missing: return "Target window closed or its handle changed";
    case TargetCheck::Hidden: return "Target window is hidden";
    case TargetCheck::Minimized: return "Target window is minimized";
    case TargetCheck::NotForeground: return "Target window lost focus; automation stopped";
    case TargetCheck::OutOfBounds: return "Click location is outside the target client area";
    case TargetCheck::Obscured: return "Target click location is covered by another window";
    }
    return "Unknown window targeting error";
}

} // namespace clicknova::platform
