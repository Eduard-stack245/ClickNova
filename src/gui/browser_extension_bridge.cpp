// SPDX-License-Identifier: Apache-2.0
#include "browser_extension_bridge.hpp"

#include <QDateTime>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSettings>
#include <QTcpSocket>
#include <QTimer>
#include <QRegularExpression>

namespace clicknova::gui {
namespace {
constexpr qsizetype kMaxRequest = 192 * 1024 + 8192;

// A browser extension origin, not a website or an arbitrary "null" origin.
bool approvedOrigin(const QByteArray& value) {
    static const QRegularExpression originPattern(
        QStringLiteral("^(chrome-extension://[a-p]{32}|moz-extension://[a-fA-F0-9-]{32,40})$"));
    return originPattern.match(QString::fromLatin1(value)).hasMatch();
}

QByteArray response(int code, const QByteArray& origin, const QByteArray& body) {
    QByteArray header = code == 200 ? "HTTP/1.1 200 OK\r\n" :
                        code == 204 ? "HTTP/1.1 204 No Content\r\n" :
                        code == 403 ? "HTTP/1.1 403 Forbidden\r\n" :
                                      "HTTP/1.1 400 Bad Request\r\n";
    header += "Content-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\n";
    if (approvedOrigin(origin)) {
        header += "Access-Control-Allow-Origin: " + origin + "\r\n";
        header += "Access-Control-Allow-Headers: content-type, x-clicknova-key\r\n";
        header += "Access-Control-Allow-Methods: OPTIONS, POST\r\n";
        header += "Access-Control-Max-Age: 300\r\n";
        header += "Access-Control-Allow-Private-Network: true\r\n";
        header += "Vary: Origin\r\n";
    }
    header += "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n";
    return header + body;
}

QByteArray headerValue(const QList<QByteArray>& lines, const QByteArray& key) {
    QByteArray answer;
    for (int index = 1; index < lines.size(); ++index) {
        const QByteArray line = lines[index];
        const qsizetype colon = line.indexOf(':');
        if (colon < 0 || line.left(colon).trimmed().toLower() != key) continue;
        if (!answer.isEmpty()) return {}; // Duplicate headers are ambiguous.
        answer = line.mid(colon + 1).trimmed();
    }
    return answer;
}
}

BrowserExtensionBridge::BrowserExtensionBridge(QObject* parent, quint16 requestedPort)
    : QObject(parent), server_(this), requestedPort_(requestedPort) {
    QSettings settings(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
    key_ = settings.value(QStringLiteral("browser-extension/pairing-key")).toString();
    static const QRegularExpression valid(QStringLiteral("^[0-9a-f]{32}$"));
    if (!valid.match(key_).hasMatch()) {
        QByteArray secret;
        for (int i = 0; i < 4; ++i) {
            const quint32 word = QRandomGenerator::system()->generate();
            for (int offset = 24; offset >= 0; offset -= 8)
                secret.append(static_cast<char>((word >> offset) & 0xff));
        }
        key_ = QString::fromLatin1(secret.toHex());
        settings.setValue(QStringLiteral("browser-extension/pairing-key"), key_);
    }
    connect(&server_, &QTcpServer::newConnection, this, [this] { acceptConnection(); });
}

bool BrowserExtensionBridge::start() {
    if (server_.isListening()) return true;
    // Never bind QHostAddress::Any or expose the pairing API to the network.
    return server_.listen(QHostAddress::LocalHost, requestedPort_);
}

QVector<BrowserPage> BrowserExtensionBridge::pages() const {
    QVector<BrowserPage> out;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = snapshots_.cbegin(); it != snapshots_.cend(); ++it) {
        if (now - it->lastSeen > 90000) continue;
        for (const auto& item : it->pages) {
            out.push_back(item);
            if (out.size() >= 300) return out;
        }
    }
    return out;
}

void BrowserExtensionBridge::acceptConnection() {
    while (auto* client = server_.nextPendingConnection()) {
        if (client->peerAddress() != QHostAddress::LocalHost) {
            client->disconnectFromHost();
            client->deleteLater();
            continue;
        }
        auto* request = new QByteArray();
        auto* completed = new bool(false);
        connect(client, &QTcpSocket::readyRead, client, [this, client, request, completed] {
            if (*completed) return;
            request->append(client->readAll());
            if (request->size() > kMaxRequest) {
                *completed = true;
                client->write(response(400, {}, "{}"));
                client->disconnectFromHost();
                return;
            }
            const qsizetype boundary = request->indexOf("\r\n\r\n");
            if (boundary < 0) return;
            const auto lines = request->left(boundary).split('\n');
            const QByteArray method = lines.front().trimmed();
            const QByteArray lengthText = headerValue(lines, "content-length");
            bool ok = false;
            const qlonglong length = lengthText.toLongLong(&ok);
            if (method.startsWith("POST ") && ok && length >= 0 &&
                boundary + 4 + length <= kMaxRequest && request->size() < boundary + 4 + length)
                return;
            *completed = true;
            client->setProperty("clicknovaRequest", *request);
            processSocket(client);
            client->disconnectFromHost();
        });
        connect(client, &QTcpSocket::disconnected, client, [client, request, completed] {
            delete request;
            delete completed;
            client->deleteLater();
        });
        QTimer::singleShot(3000, client, [client] { client->disconnectFromHost(); });
    }
}

void BrowserExtensionBridge::processSocket(QTcpSocket* client) {
    // The complete request is already buffered in this socket's readyRead handler.
    // QTcpSocket has been drained into a per-connection request buffer, so the
    // data is passed using the socket dynamic property below.
    const QByteArray request = client->property("clicknovaRequest").toByteArray();
    const qsizetype boundary = request.indexOf("\r\n\r\n");
    if (boundary < 0) { client->write(response(400, {}, "{}")); return; }
    const auto lines = request.left(boundary).split('\n');
    const QByteArray start = lines.front().trimmed();
    const QByteArray origin = headerValue(lines, "origin");
    if (!approvedOrigin(origin)) { client->write(response(403, {}, "{}")); return; }
    if (start.startsWith("OPTIONS /v1/tabs HTTP/1.")) {
        client->write(response(204, origin, {}));
        return;
    }
    if (!start.startsWith("POST /v1/tabs HTTP/1.") ||
        headerValue(lines, "content-type").toLower() != "application/json") {
        client->write(response(400, origin, "{}"));
        return;
    }
    const QByteArray incoming = headerValue(lines, "x-clicknova-key");
    const QByteArray expected = key_.toLatin1();
    unsigned int different = static_cast<unsigned int>(incoming.size() != expected.size());
    for (qsizetype i = 0; i < expected.size(); ++i)
        different |= static_cast<unsigned int>(static_cast<unsigned char>(expected[i]) ^
            static_cast<unsigned char>(i < incoming.size() ? incoming[i] : '\0'));
    if (different) { client->write(response(403, origin, "{}")); return; }
    const QByteArray lengthText = headerValue(lines, "content-length");
    bool ok = false;
    const auto length = lengthText.toLongLong(&ok);
    if (!ok || length < 0 || length > 192 * 1024 ||
        boundary + 4 + length != request.size()) {
        client->write(response(400, origin, "{}")); return;
    }
    const QByteArray body = request.mid(boundary + 4);
    QJsonParseError parseError{};
    const auto doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        client->write(response(400, origin, "{}")); return;
    }
    const auto json = doc.object();
    const QString instance = json.value(QStringLiteral("instance")).toString();
    const QString browser = json.value(QStringLiteral("browser")).toString();
    const auto parsed = parseExtensionPages(body);
    if (instance.size() < 12 || instance.size() > 64 || browser.isEmpty() ||
        !json.value(QStringLiteral("tabs")).isArray() ||
        json.value(QStringLiteral("version")).toInt() != 1) {
        client->write(response(400, origin, "{}")); return;
    }
    bool changed = true;
    if (snapshots_.contains(instance)) {
        const auto& old = snapshots_[instance].pages;
        changed = old.size() != parsed.size();
        if (!changed) for (int i = 0; i < old.size(); ++i) {
            if (old[i].id != parsed[i].id || old[i].url != parsed[i].url ||
                old[i].title != parsed[i].title || old[i].faviconUrl != parsed[i].faviconUrl ||
                old[i].browser != parsed[i].browser) { changed = true; break; }
        }
    }
    if (snapshots_.size() >= 12 && !snapshots_.contains(instance)) {
        auto oldest = snapshots_.begin();
        for (auto it = snapshots_.begin(); it != snapshots_.end(); ++it)
            if (it->lastSeen < oldest->lastSeen) oldest = it;
        snapshots_.erase(oldest);
        changed = true;
    }
    snapshots_[instance] = {parsed, QDateTime::currentMSecsSinceEpoch()};
    client->write(response(200, origin, "{\"ok\":true}"));
    if (changed && onChanged_) onChanged_();
}
} // namespace clicknova::gui
