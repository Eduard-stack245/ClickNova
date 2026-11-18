// SPDX-License-Identifier: MIT
#pragma once
#include "clicknova/visual_worker_protocol.hpp"
#include <QObject>
#include <QTimer>
#include <QString>
#include <string>
#include <QVariantMap>
#include <cstdint>
#include <stop_token>
#include <thread>

namespace clicknova::gui::qml {
// Asynchronous, read-only preview. No screenshots, OCR text or template paths
// leave the native worker. No scenario editing or input injection occurs here.
class VisualPreviewService final : public QObject {
    Q_OBJECT
public:
    explicit VisualPreviewService(QObject* parent=nullptr);
    ~VisualPreviewService() override;
    bool start(int row, std::uint64_t hwnd, std::uint32_t pid,
               clicknova::visual::worker::Provider provider,
               std::string query);
    void cancel();
    bool busy() const noexcept {return busy_;}
    QVariantMap view(int selectedRow) const;
signals:
    void changed();
private:
    void launch();
    void finish(std::uint64_t generation, int row,
                bool cancelled, int outcome, int resultStatus);
    QTimer armTimer_;
    std::jthread thread_;
    clicknova::visual::worker::Request request_{};
    std::uint64_t generation_{};
    int row_{-1};
    bool busy_{};
    bool cancelRequested_{};
    QString state_{QStringLiteral("idle")};
};
}
