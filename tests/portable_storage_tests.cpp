// SPDX-License-Identifier: Apache-2.0
#include "../src/gui/portable_storage.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <cassert>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ClickNova"));
    QCoreApplication::setApplicationName(QStringLiteral("ClickNova"));
    QString warning;
    const bool portable = clicknova::portable::initialize(&warning);
    assert(!clicknova::portable::dataRoot().isEmpty());
    assert(clicknova::portable::filePath(QStringLiteral("../escape")).isEmpty());
    assert(clicknova::portable::filePath(QStringLiteral("/absolute")).isEmpty());
    assert(QFileInfo(clicknova::portable::filePath(QStringLiteral("scenarios"))).isAbsolute());
    if (portable) {
        assert(warning.isEmpty());
        assert(QFileInfo(clicknova::portable::dataRoot()).fileName() == QStringLiteral("data") ||
               QFileInfo(clicknova::portable::dataRoot()).fileName() == QStringLiteral("ClickNova-data"));
    } else assert(!warning.isEmpty());
    return 0;
}
