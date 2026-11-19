// SPDX-License-Identifier: MIT
#pragma once

#include "accessibility_discovery_service.hpp"
#include <QPointer>

class QMainWindow;

// All Qt Widgets coupling is confined to this transitional adapter. It does
// not find elements, store selectors, or report an optimistic success result.
class LegacyAccessibilityAdapter final
    : public clicknova::gui::qml::IAccessibilityDiscoveryBackend {
public:
    explicit LegacyAccessibilityAdapter(QMainWindow* legacy);
    bool available(clicknova::accessibility::discovery::Operation operation) const override;
    bool invoke(clicknova::accessibility::discovery::Operation operation,
                int expectedRow) override;
private:
    QPointer<QMainWindow> legacy_;
};
