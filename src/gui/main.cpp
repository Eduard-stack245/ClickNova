// SPDX-License-Identifier: Apache-2.0
#include <memory>
#include <QApplication>
#include <QCoreApplication>
#include <QMainWindow>
#include <QString>
#include <QTimer>
#include "app_style.hpp"
#include "main_window.hpp"
#include "smoke_tests.hpp"
#include "wheel_scroll_filter.hpp"
#include "ui_icons.hpp"
#include "portable_storage.hpp"
#include "optional_components.hpp"
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ClickNova"));
    QCoreApplication::setApplicationName(QStringLiteral("ClickNova"));
    QString portableWarning;
    clicknova::portable::initialize(&portableWarning);
#ifdef _WIN32
    // The visual C++ backend has no Qt dependency. Resolve portable template
    // tokens against the *current* data directory after any folder move.
    const QString templateRoot = clicknova::portable::filePath(QStringLiteral("templates"));
    ::SetEnvironmentVariableW(L"CLICKNOVA_ASSET_ROOT",
        reinterpret_cast<const wchar_t*>(templateRoot.utf16()));
#endif
    // Packaged OCR language models are found next to the portable executable.
    const QString portableTessdata = clicknova::portable::filePath(QStringLiteral("tessdata"));
    if (QFileInfo(QDir(portableTessdata).filePath(QStringLiteral("eng.traineddata"))).exists() ||
        QFileInfo(QDir(portableTessdata).filePath(QStringLiteral("rus.traineddata"))).exists()) {
        qputenv("TESSDATA_PREFIX", QDir::toNativeSeparators(portableTessdata).toUtf8());
    }
    if (app.arguments().contains(QStringLiteral("--smoke-test-shortcuts")))
        return clicknovaShortcutSmoke() ? 0 : 1;

    applyClickNovaStyle(app);
    app.setWindowIcon(clicknova::gui::icon(clicknova::gui::Icon::Brand));
    installWheelScrollFilter(app);
    if (app.arguments().contains(QStringLiteral("--smoke-test-window-picker")))
        return runWindowPickerSmoke(app);

    std::unique_ptr<QMainWindow> window(createClickNovaMainWindow());
    window->show();
    // A console launcher can keep the foreground window on Windows even
    // after ClickNova has been shown. Request foreground activation ONCE at
    // event-loop startup; never steal focus later during normal operation.
    bool smokeTest = false;
    for (const QString& argument : app.arguments()) {
        if (argument.startsWith(QStringLiteral("--smoke-test-"))) {
            smokeTest = true;
            break;
        }
    }
    if (!smokeTest) {
        QTimer::singleShot(0, window.get(), [main = window.get()] {
            if (!main->isVisible()) return;
            main->raise();
            main->activateWindow();
#ifdef _WIN32
            // Qt's activation request does not always transfer foreground
            // focus away from the CMD/PowerShell launcher. Windows may deny
            // this request by its foreground-lock policy; it is best effort.
            ::SetForegroundWindow(reinterpret_cast<HWND>(main->winId()));
#endif
        });
    }
    if (!smokeTest) {
        QTimer::singleShot(0, window.get(), [main = window.get(), portableWarning] {
            if (!portableWarning.isEmpty()) QMessageBox::warning(main, QStringLiteral("ClickNova"), portableWarning);
            clicknova::gui::components::showFirstRun(main);
        });
    }
    scheduleMainWindowSmokeTests(app, *window);
    return app.exec();
}
