// SPDX-License-Identifier: MIT
#include "accessibility_discovery_service.hpp"
#include <QString>

namespace clicknova::gui::qml {
namespace {
using Refusal = accessibility::discovery::Refusal;
using Operation = accessibility::discovery::Operation;
QString translate(bool uk, const char* en, const char* ua) {
    return QString::fromUtf8(uk ? ua : en);
}
QString reasonCode(Refusal r) {
    switch (r) {
    case Refusal::None: return QStringLiteral("none");
    case Refusal::Busy: return QStringLiteral("busy");
    case Refusal::NoSelection: return QStringLiteral("no-selection");
    case Refusal::StaleSelection: return QStringLiteral("stale-selection");
    case Refusal::EditorLocked: return QStringLiteral("editor-locked");
    case Refusal::UnsupportedAction: return QStringLiteral("unsupported-action");
    case Refusal::NoSelector: return QStringLiteral("no-selector");
    case Refusal::InvalidProvider: return QStringLiteral("invalid-provider");
    case Refusal::NativeUnavailable: return QStringLiteral("native-unavailable");
    }
    return QStringLiteral("unknown");
}
QString reasonLabel(Refusal r, bool uk) {
    switch (r) {
    case Refusal::None: return {};
    case Refusal::Busy: return translate(uk, "Element picker is already open.", "Вікно вибору елемента вже відкрите.");
    case Refusal::NoSelection: return translate(uk, "Select an action first.", "Спочатку оберіть дію.");
    case Refusal::StaleSelection: return translate(uk, "Selected action changed. Repeat the request.", "Вибрана дія змінилася. Повторіть запит.");
    case Refusal::EditorLocked: return translate(uk, "Stop automation or recording before editing elements.", "Зупиніть автоматизацію або запис перед редагуванням елементів.");
    case Refusal::UnsupportedAction: return translate(uk, "This action does not support element search.", "Ця дія не підтримує пошук елементів.");
    case Refusal::NoSelector: return translate(uk, "No stored selector to clear.", "Немає збереженого селектора для очищення.");
    case Refusal::InvalidProvider: return translate(uk, "Unknown provider. Clear the old selector first.", "Невідомий провайдер. Спочатку очистьте старий селектор.");
    case Refusal::NativeUnavailable: return translate(uk, "Native element tool is unavailable or did not accept the request.", "Нативний інструмент елементів недоступний або відхилив запит.");
    }
    return {};
}
} // namespace

AccessibilityDiscoveryService::AccessibilityDiscoveryService(QObject* parent)
    : QObject(parent) {}

bool AccessibilityDiscoveryService::canRequest(
    Request context, const IAccessibilityDiscoveryBackend& backend) const {
    context.busy = busy_;
    context.backendAvailable = !busy_ && backend.available(context.operation);
    return accessibility::discovery::decide(context) == Refusal::None;
}

bool AccessibilityDiscoveryService::request(
    Request context, IAccessibilityDiscoveryBackend& backend) {
    // An inner event loop in a native picker can re-enter this function. Never
    // overwrite the outer request's state or dispatch a second operation.
    if (busy_) return false;
    context.busy = false;
    context.backendAvailable = backend.available(context.operation);
    const Refusal refusal = accessibility::discovery::decide(context);
    lastRow_ = context.expectedRow;
    lastOperation_ = context.operation;
    lastRefusal_ = refusal;
    if (refusal != Refusal::None) {
        state_ = State::Rejected;
        emit changed();
        return false;
    }
    busy_ = true;
    state_ = State::Invoking;
    emit changed();
    bool delivered = false;
    try {
        // The adapter must check selection identity and enabled state again
        // immediately before the legacy/native tool is invoked.
        delivered = backend.invoke(context.operation, context.expectedRow);
    } catch (...) {
        // Do not forward exception text: it may include private selector data.
        delivered = false;
    }
    busy_ = false;
    state_ = delivered ? State::Dispatched : State::Failed;
    lastRefusal_ = delivered ? Refusal::None : Refusal::NativeUnavailable;
    emit changed();
    return delivered;
}

QVariantMap AccessibilityDiscoveryService::view(int currentRow, bool uk) const {
    QVariantMap result;
    const bool sameRow = currentRow >= 0 && currentRow == lastRow_;
    const auto state = sameRow ? state_ : State::Idle;
    QString code;
    QString message;
    switch (state) {
    case State::Idle:
        code = QStringLiteral("idle");
        break;
    case State::Invoking:
        code = QStringLiteral("native-tool-open");
        message = translate(uk, "Native element tool is open.", "Нативний інструмент вибору елемента відкритий.");
        break;
    case State::Dispatched:
        code = QStringLiteral("native-tool-closed");
        message = translate(uk,
            "Native tool closed. Check the saved binding; no live match is guaranteed.",
            "Нативний інструмент закрито. Перевірте прив'язку; збіг із живим елементом не гарантовано.");
        break;
    case State::Rejected:
    case State::Failed:
        code = state == State::Rejected ? QStringLiteral("rejected") : QStringLiteral("not-delivered");
        message = reasonLabel(lastRefusal_, uk);
        break;
    }
    result.insert(QStringLiteral("stateKey"), code);
    result.insert(QStringLiteral("message"), message);
    result.insert(QStringLiteral("reasonKey"), state == State::Idle
        ? QStringLiteral("none") : reasonCode(lastRefusal_));
    result.insert(QStringLiteral("operation"), lastOperation_ == Operation::Find
        ? QStringLiteral("find") : QStringLiteral("clear"));
    result.insert(QStringLiteral("busy"), busy_);
    result.insert(QStringLiteral("verified"), false);
    result.insert(QStringLiteral("row"), sameRow ? lastRow_ : -1);
    return result;
}
} // namespace clicknova::gui::qml
