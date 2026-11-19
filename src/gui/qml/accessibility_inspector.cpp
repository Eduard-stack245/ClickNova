// SPDX-License-Identifier: MIT
#include "accessibility_inspector.hpp"
#include "clicknova/accessibility_inspection_policy.hpp"
#include <QString>
#include <variant>

namespace clicknova::gui::qml {
namespace {
using namespace clicknova::accessibility;
QString label(bool uk, const char* en, const char* ua) {
    return QString::fromUtf8(uk ? ua : en);
}
QString providerName(int provider, bool uk) {
    switch (provider) {
    case 0: return QStringLiteral("UI Automation (UIA)");
    case 1: return QStringLiteral("MSAA");
    case 2: return QStringLiteral("Java Access Bridge");
    case 3: return label(uk, "Image recognition", "Розпізнавання зображень");
    case 4: return QStringLiteral("OCR");
    default: return label(uk, "Unrecognized provider", "Невідомий провайдер");
    }
}
QString stateName(State state, bool uk) {
    switch (state) {
    case State::NoSelection: return label(uk, "Select a step first", "Спочатку оберіть крок");
    case State::NotApplicable: return label(uk, "Element binding is not supported for this action", "Ця дія не підтримує прив'язку до елемента");
    case State::NoSelector: return label(uk, "No element selector configured", "Селектор елемента не налаштовано");
    case State::Configured: return label(uk, "Selector saved — not verified against the live window", "Селектор збережено — ще не перевірено у вікні");
    case State::InvalidProvider: return label(uk, "Provider is invalid — clear the selector before reselecting", "Некоректний провайдер — спочатку очистьте селектор");
    }
    return {};
}
QString stateKey(State state) {
    switch (state) {
    case State::NoSelection: return QStringLiteral("none");
    case State::NotApplicable: return QStringLiteral("not-applicable");
    case State::NoSelector: return QStringLiteral("not-configured");
    case State::Configured: return QStringLiteral("configured-unverified");
    case State::InvalidProvider: return QStringLiteral("invalid-provider");
    }
    return QStringLiteral("invalid-provider");
}
} // namespace

clicknova::accessibility::Facts accessibilityFacts(const input::Action* action,
                                                    const input::StepWindow* binding) {
    using clicknova::accessibility::StepKind;
    clicknova::accessibility::Facts facts;
    if (action) {
        facts.step = StepKind::Other;
        if (const auto* click = std::get_if<input::Click>(action)) {
            facts.step = click->button == input::Button::Left
                ? StepKind::LeftClick : StepKind::OtherClick;
        } else if (std::holds_alternative<input::ElementTask>(*action)) {
            facts.step = StepKind::ElementTask;
        }
    }
    if (binding) {
        facts.provider = binding->elementProvider;
        facts.hasName = !binding->elementName.empty();
        facts.hasAutomationId = !binding->elementAutomationId.empty();
        facts.hasClass = !binding->elementClass.empty();
        facts.hasBoundWindow = binding->enforce && !binding->title.empty();
        facts.hasRecordedWindow = !binding->enforce && !binding->title.empty();
        facts.hasBrowserScope = binding->elementBrowser;
    }
    return facts;
}

QVariantMap describeAccessibility(const input::Action* action,
                                  const input::StepWindow* binding,
                                  bool ukrainian) {
    const auto facts = accessibilityFacts(action, binding);
    const auto check = clicknova::accessibility::inspect(facts);
    QVariantMap view;
    view.insert(QStringLiteral("applicable"), check.applicable);
    view.insert(QStringLiteral("configured"), check.configured);
    view.insert(QStringLiteral("providerKnown"), check.providerKnown);
    view.insert(QStringLiteral("canSearch"), check.canRequestSearch);
    view.insert(QStringLiteral("canClear"), check.canRequestClear);
    view.insert(QStringLiteral("hasRequiredWindow"), check.hasBoundWindow);
    view.insert(QStringLiteral("hasRecordedWindow"), check.hasRecordedWindow);
    view.insert(QStringLiteral("hasBrowserScope"), check.hasBrowserScope);
    view.insert(QStringLiteral("verified"), false); // Never claim live provider resolution.
    view.insert(QStringLiteral("stateKey"), stateKey(check.state));
    view.insert(QStringLiteral("stateLabel"), stateName(check.state, ukrainian));
    view.insert(QStringLiteral("providerLabel"),
        check.configured ? providerName(facts.provider, ukrainian)
                         : label(ukrainian, "Not selected", "Не вибрано"));
    view.insert(QStringLiteral("scopeLabel"), check.hasBoundWindow
        ? label(ukrainian, "Explicit target window", "Конкретне цільове вікно")
        : check.hasRecordedWindow
        ? label(ukrainian, "Recorded window (not enforced)", "Записане вікно (не обмежує)")
        : label(ukrainian, "Default target or foreground", "Типове або активне вікно"));
    view.insert(QStringLiteral("warning"), label(ukrainian,
        "Stored selectors are not proof of a live match. Search and execution retain native safety checks.",
        "Збережений селектор не гарантує збіг у поточному вікні. Пошук і виконання використовують нативні перевірки."));
    return view;
}
} // namespace clicknova::gui::qml
