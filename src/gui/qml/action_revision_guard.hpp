// SPDX-License-Identifier: MIT
#pragma once

#include <string_view>

namespace clicknova::gui::qml::revision_guard {

// Tickets are opaque snapshots of the full native document, not UI row labels.
// Enforce exactly the format produced by action_revision::snapshot before
// comparing. Neither Qt nor operating-system input is needed by this policy.
inline bool wellFormed(std::string_view ticket) noexcept {
    if (ticket.size() != 32) return false;
    for (const unsigned char c : ticket)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

inline bool matches(std::string_view expected, std::string_view current) noexcept {
    return wellFormed(expected) && wellFormed(current) && expected == current;
}

// Quick configuration is independent of the sequence document. Binding a
// sequence or step window, however, requires a fresh native-document ticket.
inline bool permitsWindowBinding(std::string_view scope, int expectedRow,
                                 int selectedRow, std::string_view expectedRevision,
                                 std::string_view currentRevision) noexcept {
    if (scope == "quick") return expectedRow == -1 && expectedRevision.empty();
    if (scope == "sequence") return expectedRow == -1 && matches(expectedRevision, currentRevision);
    if (scope == "step") return expectedRow >= 0 && expectedRow == selectedRow &&
                                 matches(expectedRevision, currentRevision);
    return false;
}

// The QML picker deliberately displays a second confirmation before changing
// sequence/step bindings. Enforce it *again* at the native QObject boundary:
// QML callers can invoke exposed methods without going through the dialog.
// Quick target selection does not change the scenario and needs no confirmation.
inline bool hasRequiredWindowConfirmation(std::string_view scope, bool confirmed) noexcept {
    return scope == "quick" || ((scope == "sequence" || scope == "step") && confirmed);
}

} // namespace clicknova::gui::qml::revision_guard
