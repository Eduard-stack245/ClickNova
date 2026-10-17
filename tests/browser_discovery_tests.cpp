// SPDX-License-Identifier: Apache-2.0
#include "../src/gui/browser_discovery.hpp"
#include <QCoreApplication>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    using namespace clicknova::gui;
    const QByteArray payload = R"json([
      {"id":"1","type":"page","title":"Example","url":"https://example.org/hello"},
      {"id":"2","type":"page","title":"Other","url":"http://docs.example.net/path", "faviconUrl":"http://docs.example.net/static/icon.png"},
      {"id":"3","type":"background_page","title":"Hidden","url":"https://example.com"},
      {"id":"4","type":"page","title":"Files","url":"file:///secret"},
      {"id":"5","type":"page","title":"Internals","url":"chrome://settings"},
      {"id":"1","type":"page","title":"Duplicate id","url":"https://duplicate.com"}
    ])json";
    const auto pages = parseDevToolsPages(payload);
    assert(pages.size() == 2);
    assert(pages[0].url.host() == QStringLiteral("example.org"));
    assert(browserIconUrl(pages[0]).toString() == QStringLiteral("https://example.org/favicon.ico"));
    assert(browserIconUrl(pages[1]).toString() == QStringLiteral("http://docs.example.net/static/icon.png"));
    auto foreignIcon = pages.front();
    foreignIcon.faviconUrl = QUrl(QStringLiteral("https://some-icon-aggregator.example/capture?url=https://example.org"));
    assert(browserIconUrl(foreignIcon).toString() == QStringLiteral("https://example.org/favicon.ico"));
    auto unsafePage = pages.front();
    unsafePage.url = QUrl(QStringLiteral("file:///sensitive"));
    assert(!browserIconUrl(unsafePage).isValid() || browserIconUrl(unsafePage).isEmpty());
    assert(uniquePageForTitle(pages, QStringLiteral("EXAMPLE")) == 0);
    assert(uniquePageForTitle(pages, QStringLiteral("Not found")) == -1);
    auto duplicate = pages;
    duplicate.push_back(pages.front());
    assert(uniquePageForTitle(duplicate, QStringLiteral("Example")) == -1);
    assert(parseDevToolsPages(QByteArrayLiteral("invalid")).isEmpty());
    assert(parseDevToolsPages(QByteArray(512 * 1024 + 1, 'x')).isEmpty());
    assert(devToolsListUrl(9222).toString() == QStringLiteral("http://127.0.0.1:9222/json/list"));
    assert(devToolsListUrl(0).isEmpty());
    const QByteArray extension = R"json({
        "version":1,"instance":"1234567890abcdef1234567890abcdef","browser":"Firefox",
        "tabs":[
            {"id":"101","title":"First","url":"https://example.org/a","faviconUrl":"https://example.org/icon.png"},
            {"id":"102","title":"Second","url":"https://example.net/b"},
            {"id":"103","title":"Private file","url":"file:///secret"},
            {"id":"104","title":"Internal","url":"about:config"},
            {"id":"101","title":"Repeat","url":"https://example.org/dup"}
        ]
    })json";
    const auto fromExtension = parseExtensionPages(extension);
    assert(fromExtension.size() == 2);
    assert(fromExtension[0].browser == QStringLiteral("Firefox"));
    assert(fromExtension[0].id == QStringLiteral("extension:1234567890abcdef1234567890abcdef:101"));
    assert(browserIconUrl(fromExtension[0]) == QUrl(QStringLiteral("https://example.org/icon.png")));
    assert(browserIconUrl(fromExtension[1]) == QUrl(QStringLiteral("https://example.net/favicon.ico")));
    assert(uniqueExtensionPageForTab(fromExtension, QStringLiteral("First"),
        QStringLiteral("C:\\Apps\\firefox.exe")) == 0);
    assert(uniqueExtensionPageForTab(fromExtension, QStringLiteral("First"),
        QStringLiteral("chrome.exe")) == -1);
    auto competing = fromExtension;
    competing.push_back(fromExtension[0]);
    assert(uniqueExtensionPageForTab(competing, QStringLiteral("First"),
        QStringLiteral("firefox.exe")) == -1);
    assert(parseExtensionPages(QByteArrayLiteral("{}" )).isEmpty());
    assert(parseExtensionPages(QByteArray(192 * 1024 + 1, 'x')).isEmpty());
    assert(parseExtensionPages(QByteArrayLiteral("{\"version\":1,\"instance\":\"../danger\",\"browser\":\"Chrome\",\"tabs\":[]}")).isEmpty());
}
// Browser-extension tab snapshots share no COM or DevTools assumptions.
// A stable extension instance/id is essential for duplicate tab names/URLs.
