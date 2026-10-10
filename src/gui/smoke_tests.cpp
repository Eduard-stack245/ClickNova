// SPDX-License-Identifier: Apache-2.0
#include <QApplication>
#include <QMainWindow>
#include <vector>
#include <QTimer>
#include <QDebug>
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QScrollArea>
#include <QLabel>
#include <QKeySequenceEdit>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QScrollBar>
#include <QToolButton>
#include <QWheelEvent>
#include "window_chooser.hpp"
#include "clicknova/window_target.hpp"
#include "main_window.hpp"
#include "smoke_tests.hpp"

int runWindowPickerSmoke(QApplication& app) {
    if (app.arguments().contains(QStringLiteral("--smoke-test-window-picker"))) {
        // An isolated list-picker test: no real window selection, focus change or clicks.
        QTimer::singleShot(0, &app, [&app] {
            const std::vector<clicknova::platform::WindowTarget> sample = {
                {1, 1234, L"Text Editor"}, {2, 5678, L"Sample Browser"}};
            QTimer::singleShot(50, &app, [&app] {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                auto* search = dialog ? dialog->findChild<QLineEdit*>(QStringLiteral("windowSearch")) : nullptr;
                auto* list = dialog ? dialog->findChild<QListWidget*>(QStringLiteral("windowSearchResults")) : nullptr;
                auto* use = dialog ? dialog->findChild<QPushButton*>(QStringLiteral("confirmWindowSelection")) : nullptr;
                if (!search || !list || !use) { if (dialog) dialog->reject(); return; }
                search->setText(QStringLiteral("browser"));
                if (list->count() != 1 || !list->item(0)->text().contains(QStringLiteral("5678"))) {
                    dialog->reject(); return;
                }
                list->setCurrentRow(0);
                if (!use->isEnabled()) { dialog->reject(); return; }
                use->click();
            });
            const auto chosen = chooseTargetWindow(nullptr, QStringLiteral("en"),
                                                   [sample] { return sample; });
            const bool success = chosen && chosen->handle == 2 && chosen->processId == 5678;
            if (!success) qCritical() << "Window selection smoke test failed";
            app.exit(success ? 0 : 1);
        });
        return app.exec();
    }
    return 1;
}

void scheduleMainWindowSmokeTests(QApplication& app, QMainWindow& window) {
    if (app.arguments().contains(QStringLiteral("--smoke-test-workflows"))) {
        QTimer::singleShot(80, &app, [&app, &window] {
            auto* nav = window.findChild<QListWidget*>(QStringLiteral("navigation"));
            auto* pages = window.findChild<QStackedWidget*>();
            auto* clickTabs = window.findChild<QTabWidget*>(QStringLiteral("clickConfigurationTabs"));
            auto* personal = window.findChild<QTabWidget*>(QStringLiteral("personalSettingsTabs"));
            auto* position = window.findChild<QCheckBox*>(QStringLiteral("optionUseFixedPosition"));
            auto* windowLimit = window.findChild<QCheckBox*>(QStringLiteral("optionLimitToWindow"));
            bool ok = nav && pages && clickTabs && personal && position && windowLimit &&
                      nav->count() == 3 && clickTabs->count() == 2 && personal->count() == 3;
            if (ok) {
                auto* useSequence = window.findChild<QCheckBox*>(QStringLiteral("optionUseSequence"));
                auto* quickPicker = window.findChild<QPushButton*>(QStringLiteral("quickPickPosition"));
                ok &= useSequence && quickPicker && quickPicker->isEnabled();
                nav->setCurrentRow(1);
                ok &= pages->currentIndex() == 3 && useSequence->isChecked();
                nav->setCurrentRow(2);
                ok &= pages->currentIndex() == 6;
                nav->setCurrentRow(0);
                ok &= pages->currentIndex() == 0 && !useSequence->isChecked();
                position->setChecked(true);
                windowLimit->setChecked(true);
                ok &= position->isChecked() && windowLimit->isChecked();
            }
            if (!ok) qCritical() << "Workflow navigation smoke test failed";
            app.exit(ok ? 0 : 1);
        });
    }
    if (app.arguments().contains(QStringLiteral("--smoke-test-language"))) {
        QTimer::singleShot(80, &app, [&app, &window] {
            auto* language = window.findChild<QComboBox*>(QStringLiteral("uiLanguage"));
            auto* navigation = window.findChild<QListWidget*>(QStringLiteral("navigation"));
            bool pass = language && navigation && navigation->count() == 3;
            if (pass) {
                language->setCurrentIndex(1);
                pass &= navigation->item(0)->text() == QString::fromUtf8("Автокликер");
                pass &= navigation->item(2)->text() == QString::fromUtf8("Настройки");
                auto* add = window.findChild<QPushButton*>(QStringLiteral("sequenceAddPoint"));
                pass &= add && add->toolTip().contains(QString::fromUtf8("Создаёт действие"));
                language->setCurrentIndex(0);
                pass &= navigation->item(0)->text() == QStringLiteral("Auto Clicker");
            }
            if (!pass) qCritical() << "Language switching test failed";
            app.exit(pass ? 0 : 1);
        });
    }
    if (app.arguments().contains(QStringLiteral("--smoke-test-sequence"))) {
        QTimer::singleShot(80, &app, [&app, &window] {
            auto* sequence = window.findChild<QCheckBox*>(QStringLiteral("optionUseSequence"));
            auto* table = window.findChild<QTableWidget*>(QStringLiteral("sequenceSteps"));
            auto* nav = window.findChild<QListWidget*>(QStringLiteral("navigation"));
            bool pass = sequence && table && nav && nav->count() == 3;
            if (pass) {
                auto* add = window.findChild<QPushButton*>(QStringLiteral("sequenceAddPoint"));
                pass &= add != nullptr;
                pass &= table->columnCount() == 5 &&
                        table->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff;
                if (add) { add->click(); pass &= table->rowCount() == 1; }
                auto* x = window.findChild<QSpinBox*>(QStringLiteral("sequenceCoordinateX"));
                auto* save = window.findChild<QPushButton*>(QStringLiteral("sequenceUpdateSelected"));
                pass &= x != nullptr && save != nullptr && save->isEnabled();
                if (x && save) {
                    auto* editStatus = window.findChild<QLabel*>(QStringLiteral("actionEditStatus"));
                    auto* discard = window.findChild<QPushButton*>(QStringLiteral("sequenceDiscardChanges"));
                    auto* editToggle = window.findChild<QToolButton*>(QStringLiteral("sequenceEditToggle"));
                    pass &= editToggle && editToggle->isChecked(); // New actions open the editor.
                    x->setValue(217);
                    pass &= table->item(0, 1) && table->item(0, 1)->text().contains(QStringLiteral("217"));
                    pass &= editStatus && editStatus->text().contains(QStringLiteral("automatically"));
                    pass &= discard && discard->isEnabled();
                    if (discard) discard->click();
                    pass &= x->value() == 0 && discard && !discard->isEnabled();
                    x->setValue(217);
                    save->click(); // Done collapses the editor; no separate save is necessary.
                    pass &= editToggle && !editToggle->isChecked();
                    pass &= table->item(0, 1) && table->item(0, 1)->text().contains(QStringLiteral("217"));
                    if (editToggle) editToggle->click();
                }
                auto* windowSettings = window.findChild<QToolButton*>(QStringLiteral("sequenceWindowSettingsToggle"));
                auto* windowPanel = window.findChild<QWidget*>(QStringLiteral("sequenceWindowSettingsPanel"));
                pass &= windowSettings && windowPanel && windowPanel->isHidden();
                if (windowSettings && windowPanel) {
                    windowSettings->click();
                    pass &= !windowPanel->isHidden();
                    windowSettings->click();
                    pass &= windowPanel->isHidden();
                }
                auto* perStep = window.findChild<QSpinBox*>(QStringLiteral("sequenceStepClickCount"));
                auto* interval = window.findChild<QSpinBox*>(QStringLiteral("sequenceClickInterval"));
                auto* passDelay = window.findChild<QSpinBox*>(QStringLiteral("sequencePassDelay"));
                auto* captureSecs = window.findChild<QSpinBox*>(QStringLiteral("sequenceCaptureSeconds"));
                auto* picker = window.findChild<QPushButton*>(QStringLiteral("sequencePickWithShortcut"));
                auto* windowPicker = window.findChild<QPushButton*>(QStringLiteral("sequencePickWindowUnderCursor"));
                pass &= perStep && interval && passDelay && captureSecs && picker &&
                        windowPicker && windowPicker->isEnabled();
                if (interval && save) {
                    interval->setValue(125);
                    save->click();
                    pass &= table->item(0, 3) && table->item(0, 3)->text().contains(QStringLiteral("125"));
                }
                if (passDelay) { passDelay->setValue(550); pass &= passDelay->value() == 550; }
                if (captureSecs) { captureSecs->setValue(8); pass &= captureSecs->value() == 8; }
                if (perStep && save) {
                    perStep->setValue(3);
                    save->click();
                    pass &= table->item(0, 2) && table->item(0, 2)->text().contains(QStringLiteral("3"));
                }
                auto* preview = window.findChild<QLabel*>(QStringLiteral("sequencePreview"));
                pass &= preview && preview->text().contains(QStringLiteral("One pass: 3 clicks"));
                auto* sequencePageSwitch = window.findChild<QCheckBox*>(QStringLiteral("sequenceEnableOnPage"));
                auto* sequenceUnlimited = window.findChild<QCheckBox*>(QStringLiteral("sequenceRepeatUnlimited"));
                auto* sequencePassCount = window.findChild<QSpinBox*>(QStringLiteral("sequenceRepeatCount"));
                // This compatibility control is hidden, but it must still belong to
                // the main window so its state and lifetime are testable.
                pass &= sequencePageSwitch && sequencePageSwitch->parentWidget() &&
                        sequenceUnlimited && sequencePassCount;
                if (!sequencePageSwitch)
                    qCritical() << "Sequence smoke: hidden mode switch is missing from the widget tree";
                nav->setCurrentRow(1);
                pass &= sequence->isChecked() &&
                        sequencePageSwitch && sequencePageSwitch->isChecked() &&
                        !sequencePageSwitch->isVisible();
                if (sequenceUnlimited && sequencePassCount) {
                    sequenceUnlimited->setChecked(false);
                    sequencePassCount->setValue(3);
                    pass &= sequencePassCount->isEnabled();
                    pass &= preview && preview->text().contains(QStringLiteral("All passes: 9 clicks"));
                    auto* quickCount = window.findChild<QSpinBox*>(QStringLiteral("quickClickCount"));
                    pass &= quickCount && quickCount->value() != 3; // Independent click and sequence counts.
                }
                nav->setCurrentRow(0);
                pass &= !sequence->isChecked() && sequencePageSwitch &&
                        !sequencePageSwitch->isChecked();
            }
            if (!pass) qCritical() << "Sequence UI smoke test failed";
            app.exit(pass ? 0 : 1);
        });
    }
    if (app.arguments().contains(QStringLiteral("--smoke-test-scenarios"))) {
        QTimer::singleShot(50, &app, [&app, &window] {
            const bool ok = window.findChild<QComboBox*>(QStringLiteral("autoScenarioSelector")) &&
                window.findChild<QComboBox*>(QStringLiteral("sequenceScenarioSelector")) &&
                window.findChild<QPushButton*>(QStringLiteral("autoScenarioSave")) &&
                window.findChild<QPushButton*>(QStringLiteral("sequenceScenarioSave"));
            if (!ok) qCritical() << "Scenario library smoke test failed";
            app.exit(ok ? 0 : 1);
        });
    }
    if (app.arguments().contains(QStringLiteral("--smoke-test-adaptive"))) {
        // Only inspect layouts and tooltips. Never start any real automation.
        QTimer::singleShot(70, &app, [&app, &window] {
            window.resize(820, 640);
            auto* nav = window.findChild<QListWidget*>(QStringLiteral("navigation"));
            if (nav) nav->setCurrentRow(1);
            app.processEvents();
            QTimer::singleShot(90, &app, [&app, &window, nav] {
                const auto* add = window.findChild<QPushButton*>(QStringLiteral("sequenceAddPoint"));
                const auto* pick = window.findChild<QPushButton*>(QStringLiteral("sequencePickWithClick"));
                const auto* hover = window.findChild<QPushButton*>(QStringLiteral("sequencePickWithShortcut"));
                const auto* table = window.findChild<QTableWidget*>(QStringLiteral("sequenceSteps"));
                const auto* start = window.findChild<QPushButton*>(QStringLiteral("primaryButton"));
                bool pass = nav && nav->width() <= 150 && add && pick && hover && table && start;
                if (pass) {
                    pass &= !add->toolTip().isEmpty() && !pick->toolTip().isEmpty() &&
                            !hover->toolTip().isEmpty() && !start->toolTip().isEmpty();
                    // A wrapping toolbar must not push its buttons outside the editor.
                    const auto* editor = window.findChild<QWidget*>(QStringLiteral("sequenceEditor"));
                    pass &= editor != nullptr;
                    if (editor) for (const auto* button : {add, pick, hover}) {
                        const QPoint right = button->mapTo(editor, QPoint(button->width(), 0));
                        pass &= right.x() <= editor->width();
                    }
                    pass &= table->isColumnHidden(2) && table->isColumnHidden(3);
                }
                if (!pass) qCritical() << "Adaptive layout / tooltip smoke test failed";
                app.exit(pass ? 0 : 1);
            });
        });
    }
    if (app.arguments().contains(QStringLiteral("--smoke-test-ui"))) {
        // Headless-ish layout check: does not send clicks or register a macro.
        QTimer::singleShot(50, &app, [&app, &window] {
            window.resize(500, 360);
            app.processEvents();
            bool fieldsHaveMinimumHeight = true;
            for (const auto* spin : window.findChildren<QAbstractSpinBox*>()) {
                fieldsHaveMinimumHeight &= spin->height() >= 44;
            }
            for (const auto* combo : window.findChildren<QComboBox*>()) {
                fieldsHaveMinimumHeight &= combo->height() >= 44;
            }
            fieldsHaveMinimumHeight &= window.findChild<QComboBox*>(QStringLiteral("positionMode")) != nullptr;
            fieldsHaveMinimumHeight &= window.findChild<QComboBox*>(QStringLiteral("targetMode")) != nullptr;
            fieldsHaveMinimumHeight &= window.findChild<QComboBox*>(QStringLiteral("targetWindows")) != nullptr;
            fieldsHaveMinimumHeight &= window.findChild<QSpinBox*>(QStringLiteral("windowCaptureSeconds")) != nullptr;
            fieldsHaveMinimumHeight &= window.findChild<QPushButton*>(QStringLiteral("chooseTargetWindow")) != nullptr;
            fieldsHaveMinimumHeight &= window.findChild<QPushButton*>(QStringLiteral("sequenceChooseWindow")) != nullptr;
            fieldsHaveMinimumHeight &= window.findChild<QSpinBox*>(QStringLiteral("coordinateX")) != nullptr;
            fieldsHaveMinimumHeight &= window.findChild<QSpinBox*>(QStringLiteral("coordinateY")) != nullptr;
            const auto* nav = window.findChild<QListWidget*>(QStringLiteral("navigation"));
            fieldsHaveMinimumHeight &= nav != nullptr && nav->count() == 3 &&
                nav->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff;
            auto* clickTabs = window.findChild<QTabWidget*>(QStringLiteral("clickConfigurationTabs"));
            auto* settingsTabs = window.findChild<QTabWidget*>(QStringLiteral("personalSettingsTabs"));
            auto* optionalDetails = window.findChild<QCheckBox*>(QStringLiteral("showOptionalDetails"));
            fieldsHaveMinimumHeight &= clickTabs && clickTabs->count() == 2 &&
                settingsTabs && settingsTabs->count() == 3 && optionalDetails;
            if (optionalDetails && clickTabs) {
                optionalDetails->setChecked(true);
                fieldsHaveMinimumHeight &= clickTabs->isVisible();
                optionalDetails->setChecked(false);
            }
            auto* fixed = window.findChild<QCheckBox*>(QStringLiteral("optionUseFixedPosition"));
            auto* bound = window.findChild<QCheckBox*>(QStringLiteral("optionLimitToWindow"));
            auto* positionMode = window.findChild<QComboBox*>(QStringLiteral("positionMode"));
            auto* targetMode = window.findChild<QComboBox*>(QStringLiteral("targetMode"));
            auto* setup = window.findChild<QLabel*>(QStringLiteral("activeSetup"));
            fieldsHaveMinimumHeight &= fixed && bound && positionMode && targetMode && setup;
            if (fixed && bound && positionMode && targetMode && setup) {
                for (int p = 0; p < 2; ++p) {
                    for (int w = 0; w < 2; ++w) {
                        fixed->setChecked(p == 1);
                        bound->setChecked(w == 1);
                        fieldsHaveMinimumHeight &= positionMode->currentIndex() == p;
                        fieldsHaveMinimumHeight &= targetMode->currentIndex() == w;
                        fieldsHaveMinimumHeight &= setup->text().contains(
                            w ? QStringLiteral("Selected window") : QStringLiteral("Any foreground window"));
                    }
                }
                // Screen and client positions must not overwrite one another.
                auto* x = window.findChild<QSpinBox*>(QStringLiteral("coordinateX"));
                auto* y = window.findChild<QSpinBox*>(QStringLiteral("coordinateY"));
                if (x && y) {
                    bound->setChecked(false);
                    x->setValue(120);
                    y->setValue(130);
                    bound->setChecked(true);
                    x->setValue(16);
                    y->setValue(18);
                    bound->setChecked(false);
                    fieldsHaveMinimumHeight &= x->value() == 120 && y->value() == 130;
                    bound->setChecked(true);
                    fieldsHaveMinimumHeight &= x->value() == 16 && y->value() == 18;
                }
                // Restore the safe defaults after the combination regression test.
                fixed->setChecked(false);
                bound->setChecked(false);
            }
            fieldsHaveMinimumHeight &= window.findChild<QKeySequenceEdit*>(QStringLiteral("shortcutStart")) != nullptr;
            fieldsHaveMinimumHeight &= window.findChild<QKeySequenceEdit*>(QStringLiteral("shortcutStop")) != nullptr;
            fieldsHaveMinimumHeight &= window.findChild<QKeySequenceEdit*>(QStringLiteral("shortcutPick")) != nullptr;
            const auto scrolls = window.findChildren<QScrollArea*>();
            fieldsHaveMinimumHeight &= scrolls.size() == 7;
            // Wheel-over-input must scroll the containing page, not change settings.
            auto* interval = window.findChild<QSpinBox*>(QStringLiteral("quickClickInterval"));
            auto* mouseButton = window.findChild<QComboBox*>(QStringLiteral("quickMouseButton"));
            auto* pages = window.findChild<QStackedWidget*>();
            auto* clickScroll = pages ? qobject_cast<QScrollArea*>(pages->widget(0)) : nullptr;
            fieldsHaveMinimumHeight &= interval && mouseButton && clickScroll;
            if (interval && mouseButton && clickScroll) {
                auto* bar = clickScroll->verticalScrollBar();
                fieldsHaveMinimumHeight &= bar && bar->maximum() > 0;
                const auto wheelDown = [](QWidget* target) {
                    QWheelEvent wheel(QPointF(10, 10),
                        QPointF(target->mapToGlobal(QPoint(10, 10))), QPoint(),
                        QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                        Qt::NoScrollPhase, false);
                    QCoreApplication::sendEvent(target, &wheel);
                };
                if (bar) bar->setValue(0);
                const int originalInterval = interval->value();
                interval->setFocus();
                wheelDown(interval);
                fieldsHaveMinimumHeight &= interval->value() == originalInterval;
                if (bar) fieldsHaveMinimumHeight &= bar->value() > 0;
                const int originalButton = mouseButton->currentIndex();
                wheelDown(mouseButton);
                fieldsHaveMinimumHeight &= mouseButton->currentIndex() == originalButton;
            }
            if (!fieldsHaveMinimumHeight) {
                qCritical() << "ClickNova UI test failed: fields are compressed or scrolling is unavailable";
            }
            app.exit(fieldsHaveMinimumHeight ? 0 : 1);
        });
    }
}
