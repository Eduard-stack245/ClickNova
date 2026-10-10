// SPDX-License-Identifier: Apache-2.0
#include <memory>
#include <QApplication>
#include <QCoreApplication>
#include <QMainWindow>
#include <QString>
#include "app_style.hpp"
#include "main_window.hpp"
#include "smoke_tests.hpp"
#include "wheel_scroll_filter.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ClickNova"));
    QCoreApplication::setApplicationName(QStringLiteral("ClickNova"));
    if (app.arguments().contains(QStringLiteral("--smoke-test-shortcuts")))
        return clicknovaShortcutSmoke() ? 0 : 1;

    applyClickNovaStyle(app);
    installWheelScrollFilter(app);
    if (app.arguments().contains(QStringLiteral("--smoke-test-window-picker")))
        return runWindowPickerSmoke(app);

    std::unique_ptr<QMainWindow> window(createClickNovaMainWindow());
    window->show();
    scheduleMainWindowSmokeTests(app, *window);
    return app.exec();
}
