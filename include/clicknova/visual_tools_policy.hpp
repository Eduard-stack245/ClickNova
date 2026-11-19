// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>

// Pure, read-only permission policy. This does NOT perform OCR, change a
// selector, capture pixels or authorize the actual automation executor.
namespace clicknova::visual::tools {
enum class SavedProvider { None, Image, Ocr };
enum class Refusal {
    None, NoSelection, StaleSelection, UnsupportedAction, UnboundWindow,
    EditorLocked, DiscoveryBusy, NativeUnavailable
};
struct Facts {
    int expectedRow{-1};
    int selectedRow{-1};
    std::size_t actionCount{0};
    bool supportedAction{false};
    bool enforcedWindow{false};
    bool workflowEditable{false};
    bool actionEditable{false};
    bool discoveryBusy{false};
    bool nativeAvailable{false};
    int savedProvider{0}; // 0 UIA, 1 MSAA, 2 Java, 3 Image, 4 OCR
    bool hasSelector{false};
};
struct Assessment {
    Refusal refusal{Refusal::NoSelection};
    SavedProvider saved{SavedProvider::None};
    bool selectorPresent{false};
    bool canOpen{false};
};
constexpr Assessment assess(const Facts& f) noexcept {
    Assessment result;
    result.saved = f.hasSelector && f.savedProvider == 3 ? SavedProvider::Image :
                   f.hasSelector && f.savedProvider == 4 ? SavedProvider::Ocr :
                   SavedProvider::None;
    result.selectorPresent = result.saved != SavedProvider::None;
    if (f.expectedRow < 0 || f.selectedRow < 0 ||
        static_cast<std::size_t>(f.expectedRow) >= f.actionCount ||
        static_cast<std::size_t>(f.selectedRow) >= f.actionCount)
        result.refusal = Refusal::NoSelection;
    else if (f.expectedRow != f.selectedRow) result.refusal = Refusal::StaleSelection;
    else if (!f.supportedAction) result.refusal = Refusal::UnsupportedAction;
    else if (!f.enforcedWindow) result.refusal = Refusal::UnboundWindow;
    else if (!f.workflowEditable || !f.actionEditable) result.refusal = Refusal::EditorLocked;
    else if (f.discoveryBusy) result.refusal = Refusal::DiscoveryBusy;
    else if (!f.nativeAvailable) result.refusal = Refusal::NativeUnavailable;
    else result.refusal = Refusal::None;
    result.canOpen = result.refusal == Refusal::None;
    return result;
}
} // namespace clicknova::visual::tools
