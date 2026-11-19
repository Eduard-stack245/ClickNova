// SPDX-License-Identifier: MIT
#include "visual_tools_gateway.hpp"

namespace clicknova::gui::qml {
QVariantMap describeVisualTools(const clicknova::visual::tools::Assessment& state,
                                bool ukrainian, const QString& lastRequestKey) {
    using namespace clicknova::visual::tools;
    const auto localized = [ukrainian](const char* en, const char* uk) {
        return QString::fromUtf8(ukrainian ? uk : en);
    };
    QString provider = QStringLiteral("none");
    if (state.saved == SavedProvider::Image) provider = QStringLiteral("image");
    if (state.saved == SavedProvider::Ocr) provider = QStringLiteral("ocr");
    QString reason;
    switch (state.refusal) {
    case Refusal::None: reason = QStringLiteral("ready"); break;
    case Refusal::NoSelection: reason = QStringLiteral("no-selection"); break;
    case Refusal::StaleSelection: reason = QStringLiteral("stale-selection"); break;
    case Refusal::UnsupportedAction: reason = QStringLiteral("unsupported-action"); break;
    case Refusal::UnboundWindow: reason = QStringLiteral("window-required"); break;
    case Refusal::EditorLocked: reason = QStringLiteral("locked"); break;
    case Refusal::DiscoveryBusy: reason = QStringLiteral("busy"); break;
    case Refusal::NativeUnavailable: reason = QStringLiteral("native-unavailable"); break;
    }
    QVariantMap out;
    out.insert(QStringLiteral("canOpen"), state.canOpen);
    out.insert(QStringLiteral("selectorSaved"), state.selectorPresent);
    out.insert(QStringLiteral("providerKey"), provider);
    out.insert(QStringLiteral("reasonKey"), reason);
    out.insert(QStringLiteral("lastRequestKey"), lastRequestKey);
    out.insert(QStringLiteral("savedSummary"),
        state.saved == SavedProvider::Ocr ? localized("Saved OCR configuration (not verified)",
                                      "Збережене налаштування OCR (не перевірено)") :
        state.saved == SavedProvider::Image ? localized("Saved image selector (not verified)",
                                          "Збережений селектор зображення (не перевірено)") :
        localized("No visual selector saved on this step", "Для цього кроку немає візуального селектора"));
    return out;
}
} // namespace clicknova::gui::qml
