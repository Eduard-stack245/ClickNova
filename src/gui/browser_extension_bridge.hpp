// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "browser_discovery.hpp"

#include <QObject>
#include <QHash>
#include <QByteArray>
#include <QString>
#include <QTcpServer>
#include <functional>

class QTcpSocket;

namespace clicknova::gui {
// Opt-in, read-only tab inventory bridge for compatible WebExtensions.
// Restricted to loopback, a persistent random secret, and extension origins.
// Never sends commands to a browser. The bridge only exists while its UI is open.
class BrowserExtensionBridge final : public QObject {
public:
    explicit BrowserExtensionBridge(QObject* parent = nullptr, quint16 requestedPort = 48231);
    bool start();
    bool listening() const { return server_.isListening(); }
    QString errorString() const { return server_.errorString(); }
    QString pairingKey() const { return key_; }
    quint16 port() const { return server_.isListening() ? server_.serverPort() : requestedPort_; }
    QVector<BrowserPage> pages() const;
    void onPagesChanged(std::function<void()> callback) { onChanged_ = std::move(callback); }

private:
    struct Snapshot {
        QVector<BrowserPage> pages;
        qint64 lastSeen{0};
    };
    void acceptConnection();
    void processSocket(QTcpSocket* client);
    QTcpServer server_;
    QString key_;
    quint16 requestedPort_{48231};
    QHash<QString, Snapshot> snapshots_;
    std::function<void()> onChanged_;
};
} // namespace clicknova::gui
