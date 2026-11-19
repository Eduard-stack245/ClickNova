// SPDX-License-Identifier: MIT
#pragma once

#include "clicknova/accessibility_discovery_policy.hpp"
#include <QObject>
#include <QVariantMap>

namespace clicknova::gui::qml {

// A provider-neutral command port. Native dialogs, headless mocks, and future
// UIA/MSAA/Java implementations can use the same contract. `invoke` means the
// request was delivered; it MUST NOT claim an element was found or bound.
class IAccessibilityDiscoveryBackend {
public:
    virtual ~IAccessibilityDiscoveryBackend() = default;
    virtual bool available(accessibility::discovery::Operation operation) const = 0;
    virtual bool invoke(accessibility::discovery::Operation operation, int expectedRow) = 0;
};

class AccessibilityDiscoveryService final : public QObject {
    Q_OBJECT
public:
    using Operation = accessibility::discovery::Operation;
    using Request = accessibility::discovery::Request;
    using Refusal = accessibility::discovery::Refusal;

    explicit AccessibilityDiscoveryService(QObject* parent = nullptr);
    bool request(Request context, IAccessibilityDiscoveryBackend& backend);
    bool canRequest(Request context, const IAccessibilityDiscoveryBackend& backend) const;
    bool busy() const noexcept { return busy_; }
    // View contains only fixed status codes/translated messages and row number;
    // never selectors, titles, values, native handles or candidate names.
    QVariantMap view(int currentRow, bool ukrainian) const;

signals:
    void changed();

private:
    enum class State { Idle, Invoking, Dispatched, Rejected, Failed };
    State state_{State::Idle};
    Operation lastOperation_{Operation::Find};
    Refusal lastRefusal_{Refusal::None};
    int lastRow_{-1};
    bool busy_{false};
};

} // namespace clicknova::gui::qml
