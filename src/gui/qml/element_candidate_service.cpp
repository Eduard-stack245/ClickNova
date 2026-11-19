// SPDX-License-Identifier: MIT
#include "element_candidate_service.hpp"
#include <QMetaObject>
#include <QString>
#include <QVariantList>
#include <utility>
#include <algorithm>
using clicknova::accessibility::candidates::VerifyResult;
using clicknova::accessibility::candidates::VerifyStatus;

namespace clicknova::gui::qml {
using accessibility::candidates::Result;
using accessibility::candidates::Status;
using accessibility::candidates::Refusal;
namespace {
using namespace accessibility::candidates;
QString reasonName(Refusal r) {
    switch (r) {
    case Refusal::None: return QStringLiteral("none");
    case Refusal::Busy: return QStringLiteral("busy");
    case Refusal::NoSelection: return QStringLiteral("no-selection");
    case Refusal::StaleSelection: return QStringLiteral("stale-selection");
    case Refusal::EditorLocked: return QStringLiteral("editor-locked");
    case Refusal::UnsupportedAction: return QStringLiteral("unsupported-action");
    case Refusal::UnsupportedProvider: return QStringLiteral("unsupported-provider");
    case Refusal::NoBoundWindow: return QStringLiteral("no-bound-window");
    case Refusal::UnsafeTarget: return QStringLiteral("unsafe-target");
    }
    return QStringLiteral("unknown");
}
QString stateName(Status state) {
    switch (state) {
    case Status::Idle: return QStringLiteral("idle");
    case Status::Searching: return QStringLiteral("searching");
    case Status::Complete: return QStringLiteral("complete");
    case Status::Cancelled: return QStringLiteral("cancelled");
    case Status::Unavailable: return QStringLiteral("unavailable");
    case Status::UnsafeTarget: return QStringLiteral("unsafe-target");
    case Status::Failed: return QStringLiteral("failed");
    }
    return QStringLiteral("failed");
}
QString genericRole(int nativeType) {
    // UIA control type identifiers from the Windows SDK. Expose broad categories
    // only: no element name, text, AutomationId, class or coordinates.
    switch (nativeType) {
    case 50000: return QStringLiteral("button");
    case 50001: return QStringLiteral("calendar");
    case 50002: return QStringLiteral("checkbox");
    case 50003: return QStringLiteral("combobox");
    case 50004: return QStringLiteral("edit");
    case 50005: return QStringLiteral("hyperlink");
    case 50006: return QStringLiteral("image");
    case 50007: return QStringLiteral("listitem");
    case 50008: return QStringLiteral("list");
    case 50009: return QStringLiteral("menu");
    case 50010: return QStringLiteral("menubar");
    case 50011: return QStringLiteral("menuitem");
    case 50012: return QStringLiteral("progressbar");
    case 50013: return QStringLiteral("radio");
    case 50014: return QStringLiteral("scrollbar");
    case 50015: return QStringLiteral("slider");
    case 50016: return QStringLiteral("spinner");
    case 50017: return QStringLiteral("statusbar");
    case 50018: return QStringLiteral("tab");
    case 50019: return QStringLiteral("tabitem");
    case 50020: return QStringLiteral("text");
    case 50021: return QStringLiteral("toolbar");
    case 50022: return QStringLiteral("tooltip");
    case 50023: return QStringLiteral("tree");
    case 50024: return QStringLiteral("treeitem");
    case 50025: return QStringLiteral("custom");
    case 50026: return QStringLiteral("group");
    case 50027: return QStringLiteral("thumb");
    case 50028: return QStringLiteral("datagrid");
    case 50029: return QStringLiteral("dataitem");
    case 50030: return QStringLiteral("document");
    case 50031: return QStringLiteral("splitbutton");
    case 50032: return QStringLiteral("window");
    case 50033: return QStringLiteral("pane");
    case 50034: return QStringLiteral("header");
    case 50035: return QStringLiteral("headeritem");
    case 50036: return QStringLiteral("table");
    case 50037: return QStringLiteral("titlebar");
    case 50038: return QStringLiteral("separator");
    default: return QStringLiteral("other");
    }
}
} // namespace
ElementCandidateService::ElementCandidateService(QObject* parent) : QObject(parent) {}
ElementCandidateService::~ElementCandidateService() {
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
}
bool ElementCandidateService::start(Context context, Target target,
                                     std::shared_ptr<IProvider> provider) {
    if (busy_) return false; // nested requests must not overwrite outer state
    // Respect other active discovery operations supplied by the bridge.
    // Clearing this flag would allow concurrent scans through different adapters.
    context.targetVerified = context.targetVerified && target.nativeHandle != 0 && target.processId != 0;
    refusal_ = authorize(context);
    if (refusal_ != Refusal::None || !provider) {
        if (!busy_) {
            row_ = context.expectedRow;
            last_ = Result{Status::Unavailable, {}, 0, false};
            emit changed();
        }
        return false;
    }
    if (worker_.joinable()) worker_.join(); // previous completed worker only
    provider_ = provider;
    target_ = target;
    providerKind_ = context.provider;
    bindState_ = QStringLiteral("idle");
    row_ = context.expectedRow;
    cancellationRequested_ = false;
    last_ = Result{Status::Searching, {}, 0, false};
    busy_ = true;
    const auto generation = ++generation_;
    emit changed();
    worker_ = std::jthread([this, generation, target, provider = std::move(provider)](std::stop_token cancel) {
        accessibility::candidates::Result result;
        try {
            result = accessibility::candidates::normalize(
                provider->enumerate(target, accessibility::candidates::Limits{}, cancel),
                accessibility::candidates::Limits{});
        } catch (...) {
            result = accessibility::candidates::Result{accessibility::candidates::Status::Failed, {}, 0, false};
        }
        if (cancel.stop_requested()) result = accessibility::candidates::Result{accessibility::candidates::Status::Cancelled, {}, 0, false};
        QMetaObject::invokeMethod(this, [this, generation, result = std::move(result)]() mutable {
            complete(generation, std::move(result));
        }, Qt::QueuedConnection);
    });
    return true;
}
void ElementCandidateService::cancel() {
    if (!busy_) return;
    cancellationRequested_ = true;
    worker_.request_stop();
    emit changed();
}
void ElementCandidateService::complete(std::uint64_t generation,
                                       accessibility::candidates::Result result) {
    if (generation != generation_ || !busy_) return;
    busy_ = false;
    last_ = cancellationRequested_
        ? accessibility::candidates::Result{accessibility::candidates::Status::Cancelled, {}, 0, false}
        : std::move(result);
    cancellationRequested_ = false;
    refusal_ = accessibility::candidates::Refusal::None;
    emit changed();
}
bool ElementCandidateService::requestBind(int row, int oneBasedNumber) {
    // MSAA and Java candidate positions are display-only and never selectors.
    if (providerKind_ != 0) return false;
    if (busy_ || row != row_ || last_.status != Status::Complete || last_.truncated ||
        oneBasedNumber <= 0 || static_cast<std::size_t>(oneBasedNumber) > last_.items.size() ||
        !provider_) return false;
    const auto candidate = last_.items[static_cast<std::size_t>(oneBasedNumber - 1)];
    if (!eligible(candidate)) return false;
    if (worker_.joinable()) worker_.join();
    busy_ = true;
    verifying_ = true;
    cancellationRequested_ = false;
    bindState_ = QStringLiteral("verifying");
    const auto generation = ++generation_;
    const auto provider = provider_;
    const auto target = target_;
    emit changed();
    worker_ = std::jthread([this, generation, provider, target, candidate](std::stop_token token) {
        VerifyResult result;
        try { result = provider->verify(target, candidate, token); }
        catch (...) { result = {VerifyStatus::Failed, {}}; }
        if (token.stop_requested()) result = {VerifyStatus::Cancelled, {}};
        // Never accept a provider's verified answer for a DIFFERENT candidate.
        if (result.status == VerifyStatus::Verified &&
            !accessibility::candidates::sameIdentity(candidate, result.match))
            result = {VerifyStatus::Failed, {}};
        QMetaObject::invokeMethod(this, [this, generation, result = std::move(result)]() mutable {
            completeBind(generation, std::move(result));
        }, Qt::QueuedConnection);
    });
    return true;
}
void ElementCandidateService::completeBind(std::uint64_t generation, VerifyResult result) {
    if (generation != generation_ || !busy_ || !verifying_) return;
    verifying_ = false;
    if (cancellationRequested_) result = {VerifyStatus::Cancelled, {}};
    if (result.status == VerifyStatus::Verified && eligible(result.match) && !cancellationRequested_) {
        awaitingCommit_ = true;
        // Only the bridge may decide whether the native action is still safe.
        emit verified(row_, result.match, target_);
        if (awaitingCommit_) bindingCommitted(false); // fail closed if nobody handled it
        return;
    }
    busy_ = false;
    cancellationRequested_ = false;
    switch (result.status) {
    case VerifyStatus::Missing: bindState_ = QStringLiteral("missing"); break;
    case VerifyStatus::Ambiguous: bindState_ = QStringLiteral("ambiguous"); break;
    case VerifyStatus::Cancelled: bindState_ = QStringLiteral("cancelled"); break;
    case VerifyStatus::UnsafeTarget: bindState_ = QStringLiteral("unsafe-target"); break;
    case VerifyStatus::Unbindable: bindState_ = QStringLiteral("unavailable"); break;
    default: bindState_ = QStringLiteral("failed"); break;
    }
    emit changed();
}
void ElementCandidateService::bindingCommitted(bool accepted) {
    if (!awaitingCommit_) return;
    awaitingCommit_ = false;
    busy_ = false;
    cancellationRequested_ = false;
    bindState_ = accepted ? QStringLiteral("saved") : QStringLiteral("rejected");
    emit changed();
}
QVariantMap ElementCandidateService::view(int currentRow, bool uk) const {
    QVariantMap result;
    const bool same = currentRow >= 0 && currentRow == row_;
    const auto state = same ? last_.status : accessibility::candidates::Status::Idle;
    const auto key = stateName(state);
    QString message;
    if (state == accessibility::candidates::Status::Searching)
        message = uk ? QString::fromUtf8("Пошук лише для читання. Натисніть «Скасувати», щоб зупинити.")
                     : QStringLiteral("Read-only search. Use Cancel to stop.");
    else if (state == accessibility::candidates::Status::Complete)
        message = uk ? QString::fromUtf8("Кандидати не перевірені. Використайте «Перевірити й прив’язати» для повторної перевірки або нативний інструмент.")
                     : QStringLiteral("Candidates are unverified. Choose Verify and bind for a fresh unique match, or use the native picker.");
    else if (state == accessibility::candidates::Status::Cancelled)
        message = uk ? QString::fromUtf8("Пошук скасовано.") : QStringLiteral("Search cancelled.");
    else if (same && (state == accessibility::candidates::Status::Failed || state == accessibility::candidates::Status::UnsafeTarget))
        message = uk ? QString::fromUtf8("Пошук не вдався або цільове вікно втрачено.") : QStringLiteral("Search failed or the target window changed.");
    else if (same && state == accessibility::candidates::Status::Unavailable)
        message = uk ? QString::fromUtf8("Пошук недоступний для цього кроку або провайдера.") : QStringLiteral("Search unavailable for this step or provider.");
    QVariantList items;
    if (same && state == accessibility::candidates::Status::Complete) {
        int ordinal = 1;
        for (const auto& item : last_.items) {
            QVariantMap view;
            view.insert(QStringLiteral("number"), ordinal++);
            view.insert(QStringLiteral("roleKey"), genericRole(item.controlType));
            view.insert(QStringLiteral("enabled"), item.enabled);
            view.insert(QStringLiteral("bindable"), providerKind_ == 0 && !busy_ && !last_.truncated && eligible(item));
            items.append(view);
        }
    }
    result.insert(QStringLiteral("providerKey"), same ? providerKind_ : -1);
    result.insert(QStringLiteral("readOnly"), same && providerKind_ != 0);
    if (same && state == accessibility::candidates::Status::Complete && providerKind_ == 2)
        message = uk ? QString::fromUtf8("Java: лише перегляд ролей. Прив’язування недоступне.")
                     : QStringLiteral("Java: role preview only. Binding is not supported.");
    if (same && state == accessibility::candidates::Status::Complete && providerKind_ == 1)
        message = uk ? QString::fromUtf8("MSAA: лише перегляд ролей. Прив’язування недоступне.")
                     : QStringLiteral("MSAA: role preview only. Binding is not supported.");
    if (same && state == accessibility::candidates::Status::Unavailable && providerKind_ == 2)
        message = uk ? QString::fromUtf8("Java Access Bridge недоступний для цього вікна або не налаштований.")
                     : QStringLiteral("Java Access Bridge is unavailable for this window or not installed.");
    result.insert(QStringLiteral("stateKey"), key);
    result.insert(QStringLiteral("reasonKey"), same ? reasonName(refusal_) : QStringLiteral("none"));
    result.insert(QStringLiteral("message"), message);
    result.insert(QStringLiteral("row"), same ? row_ : -1);
    result.insert(QStringLiteral("busy"), busy_);
    result.insert(QStringLiteral("cancelRequested"), cancellationRequested_);
    result.insert(QStringLiteral("items"), items);
    result.insert(QStringLiteral("visited"), same ? static_cast<qulonglong>(last_.visited) : 0);
    result.insert(QStringLiteral("truncated"), same && last_.truncated);
    result.insert(QStringLiteral("verified"), false); // preview itself is never verified
    result.insert(QStringLiteral("canBind"), same && providerKind_ == 0 && !busy_ &&
        state == Status::Complete && !last_.truncated &&
        std::any_of(last_.items.begin(), last_.items.end(),
                    [](const auto& c) { return eligible(c); }));
    result.insert(QStringLiteral("bindStateKey"), same ? bindState_ : QStringLiteral("idle"));
    return result;
}
} // namespace clicknova::gui::qml
