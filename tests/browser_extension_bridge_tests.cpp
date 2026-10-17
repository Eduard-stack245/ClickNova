// SPDX-License-Identifier: Apache-2.0
#include "../src/gui/browser_extension_bridge.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QTcpSocket>
#include <QTimer>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

namespace {
QByteArray sendRequest(quint16 port, const QByteArray& method,
                       const QByteArray& origin, const QByteArray& secret,
                       const QByteArray& body) {
    QTcpSocket socket;
    QEventLoop loop;
    QByteArray output;
    QObject::connect(&socket, &QTcpSocket::connected, &loop, [&] {
        QByteArray data = method + " /v1/tabs HTTP/1.1\r\nHost: 127.0.0.1\r\nOrigin: " + origin +
            "\r\nContent-Type: application/json\r\nX-ClickNova-Key: " + secret +
            "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
        socket.write(data);
    });
    QObject::connect(&socket, &QTcpSocket::readyRead, &loop, [&] { output += socket.readAll(); });
    QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    socket.connectToHost(QHostAddress::LocalHost, port);
    loop.exec();
    return output;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    clicknova::gui::BrowserExtensionBridge bridge(nullptr, 0); // Ephemeral test port.
    assert(bridge.start());
    assert(bridge.port() != 0);
    assert(bridge.pairingKey().size() == 32);
    constexpr auto extensionOrigin = "chrome-extension://abcdefghijklmnopabcdefghijklmnop";
    const QByteArray payload = R"json({"version":1,"instance":"fedcba0123456789fedcba0123456789","browser":"Chrome","tabs":[{"id":"7","title":"Website","url":"https://example.org/","faviconUrl":"https://example.org/favicon.png"}]})json";
    int changes = 0;
    bridge.onPagesChanged([&] { ++changes; });
    assert(sendRequest(bridge.port(), "POST", "https://evil.example", bridge.pairingKey().toLatin1(), payload)
        .startsWith("HTTP/1.1 403"));
    assert(sendRequest(bridge.port(), "POST", extensionOrigin, QByteArray(32, '0'), payload)
        .startsWith("HTTP/1.1 403"));
    assert(bridge.pages().isEmpty());
    assert(sendRequest(bridge.port(), "POST", extensionOrigin, bridge.pairingKey().toLatin1(), payload)
        .startsWith("HTTP/1.1 200"));
    assert(bridge.pages().size() == 1);
    assert(bridge.pages().front().url.toString() == QStringLiteral("https://example.org/"));
    assert(changes == 1);
    // Retransmitting an unchanged snapshot does not reset the user selection.
    assert(sendRequest(bridge.port(), "POST", extensionOrigin, bridge.pairingKey().toLatin1(), payload)
        .startsWith("HTTP/1.1 200"));
    assert(changes == 1);
    assert(sendRequest(bridge.port(), "OPTIONS", extensionOrigin, {}, {}).startsWith("HTTP/1.1 204"));
}
