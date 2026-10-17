// SPDX-License-Identifier: Apache-2.0
#include "browser_discovery.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace clicknova::gui {
QUrl devToolsListUrl(quint16 port) {
    if (!port) return {};
    QUrl result;
    result.setScheme(QStringLiteral("http"));
    result.setHost(QStringLiteral("127.0.0.1"));
    result.setPort(port);
    result.setPath(QStringLiteral("/json/list"));
    return result;
}

QVector<BrowserPage> parseDevToolsPages(const QByteArray& json) {
    QVector<BrowserPage> result;
    if (json.size() > 512 * 1024) return result;
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray()) return result;
    QSet<QString> seenIds;
    for (const QJsonValue& entry : doc.array()) {
        if (!entry.isObject()) continue;
        const auto object = entry.toObject();
        if (object.value(QStringLiteral("type")).toString() != QStringLiteral("page")) continue;
        BrowserPage page;
        page.title = object.value(QStringLiteral("title")).toString().trimmed();
        page.id = object.value(QStringLiteral("id")).toString();
        page.url = QUrl(object.value(QStringLiteral("url")).toString());
        const QString scheme = page.url.scheme().toLower();
        // Do not expose DevTools endpoints, browser internals, extension pages,
        // or file:// resources as normal website targets.
        if (page.id.isEmpty() || page.title.isEmpty() || page.title.size() > 512 ||
            (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) ||
            page.url.host().isEmpty() || seenIds.contains(page.id)) continue;
        seenIds.insert(page.id);
        page.faviconUrl = QUrl(object.value(QStringLiteral("faviconUrl")).toString());
        result.push_back(std::move(page));
        if (result.size() >= 150) break;
    }
    return result;
}

QVector<BrowserPage> parseExtensionPages(const QByteArray& json) {
    QVector<BrowserPage> result;
    if (json.size() > 192 * 1024) return result;
    QJsonParseError error{};
    const auto doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return result;
    const auto object = doc.object();
    if (object.value(QStringLiteral("version")).toInt() != 1 ||
        !object.value(QStringLiteral("tabs")).isArray()) return result;
    const QString instance = object.value(QStringLiteral("instance")).toString();
    const QString browser = object.value(QStringLiteral("browser")).toString().trimmed();
    if (instance.size() < 12 || instance.size() > 64 || browser.isEmpty() ||
        browser.size() > 64) return result;
    for (const QChar c : instance)
        if (!c.isLetterOrNumber() && c != QChar('-')) return result;
    QSet<QString> ids;
    const QJsonArray tabs = object.value(QStringLiteral("tabs")).toArray();
    for (const auto& value : tabs) {
        if (!value.isObject()) continue;
        const auto entry = value.toObject();
        BrowserPage page;
        const QString tabId = entry.value(QStringLiteral("id")).toString();
        page.title = entry.value(QStringLiteral("title")).toString().trimmed();
        page.url = QUrl(entry.value(QStringLiteral("url")).toString());
        const auto scheme = page.url.scheme().toLower();
        if (tabId.isEmpty() || tabId.size() > 32 || ids.contains(tabId) ||
            page.title.isEmpty() || page.title.size() > 512 ||
            (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) ||
            page.url.host().isEmpty() || page.url.userInfo().size() > 0 ||
            page.url.toString().size() > 4096) continue;
        bool simpleId = true;
        for (const QChar c : tabId)
            if (!c.isLetterOrNumber() && c != QChar('-')) simpleId = false;
        if (!simpleId) continue;
        ids.insert(tabId);
        page.id = QStringLiteral("extension:%1:%2").arg(instance, tabId);
        page.browser = browser;
        page.faviconUrl = QUrl(entry.value(QStringLiteral("faviconUrl")).toString());
        result.push_back(std::move(page));
        if (result.size() >= 250) break;
    }
    return result;
}

QUrl browserIconUrl(const BrowserPage& page) {
    if (!page.url.isValid() || page.url.host().isEmpty() ||
        (page.url.scheme() != QStringLiteral("https") &&
         page.url.scheme() != QStringLiteral("http"))) return {};
    // Allow only first-party icons: no third-party icon proxy, referrer, or
    // favicon aggregator receives the browsing history from this feature.
    if (page.faviconUrl.isValid() &&
        page.faviconUrl.scheme().compare(page.url.scheme(), Qt::CaseInsensitive) == 0 &&
        page.faviconUrl.host().compare(page.url.host(), Qt::CaseInsensitive) == 0 &&
        page.faviconUrl.port(page.url.scheme() == QStringLiteral("https") ? 443 : 80) ==
            page.url.port(page.url.scheme() == QStringLiteral("https") ? 443 : 80))
    {
        QUrl icon = page.faviconUrl;
        icon.setFragment(QString());
        icon.setUserInfo(QString());
        return icon;
    }
    QUrl favicon = page.url;
    favicon.setPath(QStringLiteral("/favicon.ico"));
    favicon.setQuery(QString());
    favicon.setFragment(QString());
    favicon.setUserInfo(QString());
    return favicon;
}

int uniqueExtensionPageForTab(const QVector<BrowserPage>& pages,
                              const QString& title, const QString& executable) {
    const QString process = executable.toLower().section(QLatin1Char('\\'), -1)
        .section(QLatin1Char('/'), -1);
    int index = -1;
    for (int i = 0; i < pages.size(); ++i) {
        const auto& page = pages[i];
        if (page.title.compare(title, Qt::CaseInsensitive) != 0) continue;
        const QString label = page.browser.toLower();
        QString expected;
        if (label == QStringLiteral("firefox")) expected = QStringLiteral("firefox.exe");
        else if (label == QStringLiteral("chrome")) expected = QStringLiteral("chrome.exe");
        else if (label == QStringLiteral("edge")) expected = QStringLiteral("msedge.exe");
        else if (label == QStringLiteral("brave")) expected = QStringLiteral("brave.exe");
        else if (label == QStringLiteral("opera")) expected = QStringLiteral("opera.exe");
        else if (label == QStringLiteral("vivaldi")) expected = QStringLiteral("vivaldi.exe");
        else if (label == QStringLiteral("chromium")) expected = QStringLiteral("chromium.exe");
        // Arbitrary names cannot be mapped safely based on a user-editable
        // extension label. The UIA list still works for other browsers.
        if (expected.isEmpty() || expected != process) continue;
        if (index != -1) return -1;
        index = i;
    }
    return index;
}

int uniquePageForTitle(const QVector<BrowserPage>& pages, const QString& title) {
    int match = -1;
    for (int i = 0; i < pages.size(); ++i) {
        if (pages[i].title.compare(title, Qt::CaseInsensitive) != 0) continue;
        if (match >= 0) return -1;
        match = i;
    }
    return match;
}
} // namespace clicknova::gui
