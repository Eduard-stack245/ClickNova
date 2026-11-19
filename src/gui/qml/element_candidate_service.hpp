// SPDX-License-Identifier: MIT
#pragma once
#include "clicknova/element_candidate_search.hpp"
#include <QObject>
#include <QVariantMap>
#include <QString>
#include <cstdint>
#include <memory>
#include <thread>

namespace clicknova::gui::qml {
// Executes a read-only provider on a worker thread; never performs input or
// passes window titles, AutomationIds or candidate names to QML.
class ElementCandidateService final : public QObject {
    Q_OBJECT
public:
    using Context = accessibility::candidates::Context;
    using Target = accessibility::candidates::Target;
    using IProvider = accessibility::candidates::IProvider;
    explicit ElementCandidateService(QObject* parent = nullptr);
    ~ElementCandidateService() override;
    bool start(Context context, Target target, std::shared_ptr<IProvider> provider);
    void cancel();
    bool requestBind(int row, int oneBasedNumber);
    void bindingCommitted(bool accepted);
    bool busy() const noexcept { return busy_; }
    QVariantMap view(int currentRow, bool ukrainian) const;
    // No bindings. Results are an anonymous, transient read-only list.
signals:
    void changed();
    // Internal C++ signal; identity is deliberately never exposed as Q_PROPERTY.
    void verified(int row, const accessibility::candidates::Candidate& match,
                  accessibility::candidates::Target target);
private:
    void complete(std::uint64_t generation, accessibility::candidates::Result result);
    void completeBind(std::uint64_t generation,
                      accessibility::candidates::VerifyResult result);
    std::jthread worker_;
    accessibility::candidates::Result last_{};
    accessibility::candidates::Refusal refusal_{accessibility::candidates::Refusal::None};
    bool busy_{};
    bool cancellationRequested_{};
    bool verifying_{};
    bool awaitingCommit_{};
    QString bindState_{QStringLiteral("idle")};
    std::shared_ptr<IProvider> provider_;
    Target target_{};
    int providerKind_{-1};
    int row_{-1};
    std::uint64_t generation_{};
};
} // namespace clicknova::gui::qml
