// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QVector>

namespace clicknova::gui {
// Browser DevTools is an OPTIONAL localhost-only discovery source. It never
// automatically enables browser debugging or launches/controls a browser.
struct BrowserPage {
    QString id;
    QString title;
    QUrl url;
    QUrl faviconUrl;
    QString browser; // Informational source label; never a native-window identity.
};
QVector<BrowserPage> parseDevToolsPages(const QByteArray& json);
// Snapshot supplied by a paired browser extension. Parsed strictly and kept in memory.
QVector<BrowserPage> parseExtensionPages(const QByteArray& json);
QUrl browserIconUrl(const BrowserPage& page);
QUrl devToolsListUrl(quint16 port);
// Exact matching: a page discovered from a debug endpoint must not be
// assigned to a different UIA tab on the strength of a substring match.
int uniquePageForTitle(const QVector<BrowserPage>& pages, const QString& title);
// Decorate a native UIA tab only if an extension page has both an exact,
// unique title and a matching *specific* browser executable. Informational
// decoration does not grant a native window identity to an extension page.
int uniqueExtensionPageForTab(const QVector<BrowserPage>& pages,
                              const QString& title, const QString& executable);
} // namespace clicknova::gui
