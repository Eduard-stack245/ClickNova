// SPDX-License-Identifier: Apache-2.0
#include <QApplication>
#include <QStringList>
#include <QMainWindow>
#include <QToolBar>
#include <QDockWidget>
#include <vector>
#include <algorithm>
#include <QTimer>
#include <QTimeEdit>
#include <QDebug>
#include <QDialog>
#include <QGroupBox>
#include <QLineEdit>
#include <QListWidget>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QPlainTextEdit>
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
#include <QMenu>
#include <QAction>
#include <QShortcut>
#include <QPointer>
#include <QFile>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QMetaObject>
#include <QContextMenuEvent>
#include "action_table_widget.hpp"
#include "input_lab.hpp"
#include <QWheelEvent>
#include <limits>
#include "window_chooser.hpp"
#include "clicknova/window_target.hpp"
#include "main_window.hpp"
#include "smoke_tests.hpp"
#include "recording_review_dialog.hpp"
#include "recording_checkpoint.hpp"
#include <QTemporaryDir>
#include "clicknova/input_actions.hpp"

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
    if (app.arguments().contains(QStringLiteral("--smoke-test-recording-recovery"))) {
        QTimer::singleShot(0, &app, [&app] {
            QTemporaryDir temp;
            using namespace clicknova::gui::recording_checkpoint;
            const QString path = newPath(temp.path());
            const QDateTime when = QDateTime::currentDateTime().addSecs(-60);
            clicknova::recording::Sample typed;
            typed.kind = clicknova::recording::Kind::Text;
            typed.text = u"Test кириллица";
            typed.window = {u"Browser window", u"browser.exe"};
            typed.milliseconds = 123;
            clicknova::recording::Sample mouse;
            mouse.kind = clicknova::recording::Kind::MouseDown;
            mouse.button = clicknova::input::Button::Right;
            mouse.x = 42; mouse.y = 107;
            mouse.window = typed.window;
            mouse.milliseconds = 800;
            const std::vector<clicknova::recording::Sample> events{typed, mouse};
            Snapshot restored;
            bool ok = save(path, when, events, 1000) && load(path, restored) &&
                restored.samples.size() == 2 && restored.created == when &&
                restored.durationMs == 1000 && restored.samples[0].text == typed.text &&
                restored.samples[1].x == 42 && restored.samples[1].button == clicknova::input::Button::Right &&
                restored.samples[1].window.title == typed.window.title;
            const auto candidates = list(temp.path());
            ok &= candidates.size() == 1 && candidates[0].samples.size() == 2;
            ok &= save(path, when, events, 1300) && load(path, restored) && restored.durationMs == 1300;
            ok &= remove(path) && list(temp.path()).empty();
            if (!ok) qCritical() << "Recording recovery GUI smoke test failed";
            app.exit(ok ? 0 : 1);
        });
    }
    if (app.arguments().contains(QStringLiteral("--smoke-test-unified"))) {
        // The shared workspace handles manual actions, recordings, window
        // targeting and all saved formats without a visible second editor.
        QTimer::singleShot(85, &app, [&app, &window] {
            auto* nav = window.findChild<QListWidget*>(QStringLiteral("navigation"));
            auto* pages = window.findChild<QStackedWidget*>();
            auto* table = window.findChild<QTableWidget*>(QStringLiteral("inputLabActions"));
            auto* quickClick = window.findChild<QPushButton*>(QStringLiteral("inputLabQuickClick"));
            auto* quickText = window.findChild<QPushButton*>(QStringLiteral("inputLabQuickText"));
            auto* library = window.findChild<QComboBox*>(QStringLiteral("inputLabSavedPlans"));
            auto* previous = window.findChild<QPushButton*>(QStringLiteral("inputLabImportPrevious"));
            auto* savedToggle = window.findChild<QPushButton*>(QStringLiteral("inputLabSavedToggle"));
            auto* record = window.findChild<QPushButton*>(QStringLiteral("inputLabRecord"));
            auto* windowPicker = window.findChild<QPushButton*>(QStringLiteral("inputLabChooseWindow"));
            auto* windowClear = window.findChild<QPushButton*>(QStringLiteral("inputLabClearWindow"));
            auto* run = window.findChild<QPushButton*>(QStringLiteral("inputLabRun"));
            auto* old = window.findChild<QWidget*>(QStringLiteral("sequenceEditor"));
            auto* moveModes = window.findChild<QComboBox*>(QStringLiteral("inputLabMoveMode"));
            bool ok = nav && nav->count() == 3 && pages && table && quickClick && quickText &&
                      library && previous && savedToggle && record && windowPicker && windowClear && run &&
                      moveModes && moveModes->count() == 3 && !windowClear->isEnabled();
            if (ok) {
                nav->setCurrentRow(1);
                app.processEvents();
                auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("executionToolbar"));
                auto* dock = window.findChild<QDockWidget*>(QStringLiteral("activityLogDock"));
                auto* log = window.findChild<QPlainTextEdit*>(QStringLiteral("activityLogView"));
                auto* clearLog = window.findChild<QPushButton*>(QStringLiteral("clearActivityLog"));
                auto* quickStart = window.findChild<QPushButton*>(QStringLiteral("primaryButton"));
                auto* stepStop = window.findChild<QPushButton*>(QStringLiteral("inputLabStop"));
                ok &= toolbar && toolbar->isVisible() && dock && log && clearLog &&
                      quickStart && stepStop && run->isVisible() && stepStop->isVisible() &&
                      !quickStart->isVisible() && !log->toPlainText().isEmpty();
                if (clearLog) clearLog->click();
                if (log) ok &= log->toPlainText().isEmpty();
                if (dock) { dock->hide(); app.processEvents(); ok &= !dock->isVisible(); dock->show(); }
                savedToggle->click();
                app.processEvents();
                ok &= pages->currentIndex() == 3 && quickClick->isVisible() &&
                      record->isVisible() && !previous->isVisible() && (!old || !old->isVisible()) &&
                      run->text().contains(QStringLiteral("F6"));
                const int before = table->rowCount();
                quickClick->click();
                quickText->click();
                ok &= table->rowCount() == before + 2;
            }
            if (!ok) qCritical() << "Unified Sequence workspace smoke test failed";
            app.exit(ok ? 0 : 1);
        });
    }
    if (app.arguments().contains(QStringLiteral("--smoke-test-action-menu"))) {
        // No OS input injection or scenario writes. Run on a known, non-identical
        // plan, so reordering actually proves that the action data changed.
        // On Windows, Qt WIN32 targets may not expose qCritical() via stderr:
        // write a diagnostic file only if the smoke test fails.
        QTimer::singleShot(110, &app, [&app, &window] {
            bool ok = true;
            const QString diagnosticPath = QCoreApplication::applicationDirPath() +
                QStringLiteral("/gui-action-menu-smoke.log");
            QFile::remove(diagnosticPath);  // Never report a stale failure.
            const auto expect = [&ok, &diagnosticPath](bool value, const char* what) {
                if (!value) {
                    qCritical().noquote() << "gui_action_menu_smoke:" << what;
                    QFile file(diagnosticPath);
                    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                        file.write("gui_action_menu_smoke: ");
                        file.write(what);
                        file.write("\n");
                    }
                    ok = false;
                }
                return value;
            };
            auto* nav = window.findChild<QListWidget*>(QStringLiteral("navigation"));
            // Qt 6.12 findChild<T*>() requires T to declare Q_OBJECT.
            // InputLabWidget deliberately inherits QWidget without Q_OBJECT,
            // so locate it by its QWidget metaobject and then check the C++ type.
            auto* lab = dynamic_cast<InputLabWidget*>(
                window.findChild<QWidget*>(QStringLiteral("inputLab")));
            if (nav) nav->setCurrentRow(1);
            auto* table = window.findChild<QTableWidget*>(QStringLiteral("inputLabActions"));
            auto* quick = window.findChild<QPushButton*>(QStringLiteral("inputLabQuickWait"));
            if (!expect(nav && lab && table && quick, "required widgets missing")) {
                app.exit(1);
                return;
            }
            using namespace clicknova::input;
            lab->replaceActions(Plan{{Click{Button::Left, 1, 100}, Wait{300}}, 1, 0});
            const int before = table->rowCount();
            expect(before == 2, "test plan did not load");
            expect(table->columnCount() == 3, "three action columns expected");
            expect(table->viewport()->contextMenuPolicy() == Qt::CustomContextMenu,
                   "viewport right-click policy missing");
            expect(table->horizontalHeader()->contextMenuPolicy() == Qt::CustomContextMenu,
                   "header right-click policy missing");
            expect(table->horizontalHeader()->viewport()->contextMenuPolicy() == Qt::CustomContextMenu,
                   "header viewport right-click policy missing");
            expect(table->horizontalHeaderItem(2) &&
                   table->horizontalHeaderItem(2)->text() == QStringLiteral("⋯"),
                   "list-wide ellipsis missing");
            expect(!window.findChild<QPushButton*>(QStringLiteral("inputLabMoreTools")),
                   "obsolete overflow button still present");
            const auto menuKeys = table->findChildren<QShortcut*>();
            const auto hasMenuKey = [&menuKeys](const QKeySequence& seq) {
                return std::any_of(menuKeys.cbegin(), menuKeys.cend(),
                    [&seq](const QShortcut* shortcut) { return shortcut->key() == seq; });
            };
            expect(hasMenuKey(QKeySequence(Qt::SHIFT | Qt::Key_F10)),
                   "Shift+F10 context menu keyboard shortcut missing");
            expect(hasMenuKey(QKeySequence(Qt::Key_Menu)),
                   "keyboard Menu context shortcut missing");

            const auto menuNow = [table]() -> QPointer<QMenu> {
                // Find only the root context menu; do not match a nested submenu.
                const auto menus = table->findChildren<QMenu*>(
                    QStringLiteral("inputLabContextMenu"), Qt::FindDirectChildrenOnly);
                return menus.size() == 1 ? QPointer<QMenu>(menus.front()) : QPointer<QMenu>();
            };
            const auto action = [](QMenu* menu, const char* name) -> QAction* {
                return menu ? menu->findChild<QAction*>(QString::fromLatin1(name)) : nullptr;
            };
            const auto submenu = [](QMenu* menu, const char* name) -> QMenu* {
                return menu ? menu->findChild<QMenu*>(QString::fromLatin1(name)) : nullptr;
            };
            const auto closeMenu = [&app](QPointer<QMenu>& menu) {
                if (menu) menu->close();
                app.processEvents();
            };
            const auto openRowMenu = [&](int row) -> QPointer<QMenu> {
                auto* button = qobject_cast<QToolButton*>(table->cellWidget(row, 2));
                if (!expect(button && !button->icon().isNull(), "row ellipsis button missing"))
                    return {};
                button->click();
                app.processEvents();
                auto menu = menuNow();
                expect(menu && menu->isVisible(), "row menu not visible");
                return menu;
            };

            quick->click();
            expect(table->rowCount() == before + 1, "quick add did not append action");
            auto menu = openRowMenu(0);
            expect(action(menu, "stepCopy") && action(menu, "stepPaste") &&
                   action(menu, "stepDuplicate") && action(menu, "stepDelete"),
                   "copy/paste/duplicate/delete entries missing");
            expect(action(menu, "stepFirst") && action(menu, "stepLast") &&
                   action(menu, "listUndo") && action(menu, "listRedo"),
                   "move/undo/redo entries missing");
            expect(submenu(menu, "stepInsertBefore") && submenu(menu, "stepInsertAfter"),
                   "insert before/after submenus missing");
            if (auto* copy = action(menu, "stepCopy")) copy->trigger();
            closeMenu(menu);
            expect(!menuNow(), "closed row popup still has the active menu name");

            // One selected step has one Delete/Copy/Duplicate command.
            QPointer<QMenu> oneMenu = openRowMenu(0);
            expect(action(oneMenu, "stepDelete") && !action(oneMenu, "multiDelete") &&
                   action(oneMenu, "stepCopy") && !action(oneMenu, "multiCopy"),
                   "single-row menu duplicates batch actions");
            closeMenu(oneMenu);
            table->selectAll();
            app.processEvents();
            QPointer<QMenu> allMenu = openRowMenu(0);
            expect(!action(allMenu, "multiSelectAll") && !action(allMenu, "stepDelete") &&
                   action(allMenu, "multiDelete") && !action(allMenu, "stepCopy") &&
                   action(allMenu, "multiCopy") && !action(allMenu, "stepDuplicate"),
                   "batch menu contains duplicate commands or redundant Select All");
            closeMenu(allMenu);
            // Partial multi-selection must offer one batch command for each
            // operation, plus Select All for the remaining unselected rows.
            table->setCurrentCell(0, 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            table->selectionModel()->select(table->model()->index(1, 0),
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
            app.processEvents();
            QPointer<QMenu> partialMenu = openRowMenu(0);
            expect(action(partialMenu, "multiSelectAll") &&
                   action(partialMenu, "multiDelete") && !action(partialMenu, "stepDelete") &&
                   action(partialMenu, "multiDuplicate") && !action(partialMenu, "stepDuplicate") &&
                   action(partialMenu, "multiCopy") && !action(partialMenu, "stepCopy") &&
                   action(partialMenu, "multiUp") && !action(partialMenu, "stepUp") &&
                   submenu(partialMenu, "stepInsertBefore") &&
                   submenu(partialMenu, "stepInsertAfter") &&
                   action(partialMenu, "stepPaste") && action(partialMenu, "stepPasteBefore"),
                   "partial selection has missing or duplicate menu commands");
            closeMenu(partialMenu);
            table->setCurrentCell(0, 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            app.processEvents();

            // Right-clicking a real table row must select and open that row's
            // commands, not the list-wide commands used for empty space.
            const QPoint rowPoint = table->visualItemRect(table->item(0, 0)).center();
            QContextMenuEvent rowContext(QContextMenuEvent::Mouse, rowPoint,
                table->viewport()->mapToGlobal(rowPoint));
            QApplication::sendEvent(table->viewport(), &rowContext);
            app.processEvents();
            menu = menuNow();
            expect(rowContext.isAccepted(), "row viewport context event was not accepted");
            expect(menu && menu->isVisible() && action(menu, "stepDelete") &&
                   !action(menu, "listClearAll") && table->currentRow() == 0,
                   "row right-click opened wrong menu");
            closeMenu(menu);

            // Qt may also deliver context-menu events directly to its table
            // rather than the viewport. In this path pos is table-local, not
            // viewport-local; both must resolve to the same row.
            const QPoint otherRowPoint = table->visualItemRect(table->item(1, 0)).center();
            const QPoint otherGlobal = table->viewport()->mapToGlobal(otherRowPoint);
            const QPoint tableLocal = table->mapFromGlobal(otherGlobal);
            QContextMenuEvent directTableContext(QContextMenuEvent::Mouse, tableLocal, otherGlobal);
            QApplication::sendEvent(table, &directTableContext);
            app.processEvents();
            menu = menuNow();
            expect(directTableContext.isAccepted(), "direct table context event was not accepted");
            expect(menu && menu->isVisible() && action(menu, "stepDelete") &&
                   !action(menu, "listClearAll") && table->currentRow() == 1,
                   "direct table right-click selected the wrong row or menu");
            closeMenu(menu);

            // The button installed as a cell widget must respond to right-click
            // just like the surrounding table cell, not fall through to the
            // list-wide menu (particularly when it covers the whole cell).
            auto* rowButton = qobject_cast<QToolButton*>(table->cellWidget(1, 2));
            if (expect(rowButton, "second row ellipsis missing")) {
                const QPoint buttonPoint = rowButton->rect().center();
                QContextMenuEvent buttonContext(QContextMenuEvent::Mouse, buttonPoint,
                    rowButton->mapToGlobal(buttonPoint));
                QApplication::sendEvent(rowButton, &buttonContext);
                app.processEvents();
                menu = menuNow();
                expect(buttonContext.isAccepted(), "row button right-click was not accepted");
                expect(menu && menu->isVisible() && action(menu, "stepDelete") &&
                       !action(menu, "listClearAll") && table->currentRow() == 1,
                       "row ellipsis right-click opened the wrong menu");
                closeMenu(menu);
            }

            // A context menu event in empty viewport space opens the list menu.
            const QPoint empty(table->viewport()->width() - 5,
                               table->viewport()->height() - 5);
            QContextMenuEvent context(QContextMenuEvent::Mouse, empty,
                                      table->viewport()->mapToGlobal(empty));
            QApplication::sendEvent(table->viewport(), &context);
            app.processEvents();
            menu = menuNow();
            auto* add = submenu(menu, "listAddAction");
            auto* paste = action(menu, "stepPaste");
            auto* clearAll = action(menu, "listClearAll");
            expect(context.isAccepted(), "empty viewport right-click was not accepted");
            expect(menu && menu->isVisible() && !action(menu, "stepDelete"),
                   "empty viewport opened a row menu instead of the list menu");
            expect(add && add->actions().size() >= 13, "add action submenu incomplete");
            expect(paste && paste->isEnabled(), "paste at end disabled after copy");
            expect(clearAll && clearAll->isEnabled(), "clear all missing or disabled");
            if (paste && paste->isEnabled()) paste->trigger();
            closeMenu(menu);
            expect(table->rowCount() == before + 2, "paste at end did not append");

            // Both event paths are used by Qt 6 on Windows. Real mouse
            // right-clicks typically hit the header viewport; direct events
            // to QHeaderView must also work for other delivery paths.
            auto* header = table->horizontalHeader();
            auto* headerViewport = header->viewport();
            const QPoint headerPoint(headerViewport->width() / 2,
                                     headerViewport->height() / 2);
            QContextMenuEvent headerViewportContext(QContextMenuEvent::Mouse, headerPoint,
                headerViewport->mapToGlobal(headerPoint));
            QApplication::sendEvent(headerViewport, &headerViewportContext);
            app.processEvents();
            menu = menuNow();
            expect(headerViewportContext.isAccepted(), "header viewport right-click was not handled");
            expect(menu && menu->isVisible(), "header viewport list menu not visible");
            expect(action(menu, "listClearAll") && !action(menu, "stepDelete"),
                   "header viewport menu has incorrect commands");
            closeMenu(menu);

            const QPoint directPoint(header->width() / 2, header->height() / 2);
            QContextMenuEvent directHeaderContext(QContextMenuEvent::Mouse, directPoint,
                header->mapToGlobal(directPoint));
            QApplication::sendEvent(header, &directHeaderContext);
            app.processEvents();
            menu = menuNow();
            expect(directHeaderContext.isAccepted(), "direct header right-click was not handled");
            expect(menu && menu->isVisible() && action(menu, "listClearAll"),
                   "direct header list menu missing");
            closeMenu(menu);

            // The last header section is the visible list-wide ellipsis.
            // Invoking its sectionClicked signal checks the same connection
            // Qt invokes on mouse release, without synthesizing OS clicks.
            const bool clicked = QMetaObject::invokeMethod(header, "sectionClicked",
                Qt::DirectConnection, Q_ARG(int, 2));
            expect(clicked, "header ellipsis section signal missing");
            app.processEvents();
            menu = menuNow();
            expect(menu && menu->isVisible() && action(menu, "listClearAll"),
                   "header ellipsis list menu missing");
            closeMenu(menu);

            auto* movable = static_cast<ActionTableWidget*>(table);
            const QString first = table->item(0, 0)->text().mid(3);
            if (movable->requestMove) movable->requestMove(0, table->rowCount() - 1);
            expect(table->item(table->rowCount() - 1, 0)->text().mid(3) == first,
                   "move did not change the real action order");
            menu = openRowMenu(0);
            if (auto* duplicate = action(menu, "stepDuplicate")) duplicate->trigger();
            else expect(false, "duplicate action not found after reorder");
            closeMenu(menu);
            expect(table->rowCount() == before + 3, "duplicate did not add action");
            menu = openRowMenu(1);
            if (auto* erase = action(menu, "stepDelete")) erase->trigger();
            else expect(false, "delete action not found");
            closeMenu(menu);
            expect(table->rowCount() == before + 2, "delete did not remove action");

            menu = openRowMenu(0);
            auto* undo = action(menu, "listUndo");
            expect(undo && undo->isEnabled(), "undo unexpectedly disabled");
            if (undo && undo->isEnabled()) undo->trigger();
            closeMenu(menu);
            expect(table->rowCount() == before + 3, "undo did not restore action");
            menu = openRowMenu(0);
            auto* redo = action(menu, "listRedo");
            expect(redo && redo->isEnabled(), "redo unexpectedly disabled");
            if (redo && redo->isEnabled()) redo->trigger();
            closeMenu(menu);
            expect(table->rowCount() == before + 2, "redo did not remove action");

            // Insert-before is a distinct command, not merely a visible submenu.
            // It must preserve the original first action at the following row.
            const QString oldFirstAction = table->item(0, 0)->text().mid(3);
            menu = openRowMenu(0);
            auto* insert = submenu(menu, "stepInsertBefore");
            expect(insert && !insert->actions().isEmpty(), "insert before has no steps");
            if (insert && !insert->actions().isEmpty()) insert->actions().front()->trigger();
            closeMenu(menu);
            expect(table->rowCount() == before + 3, "insert before did not insert step");
            expect(table->item(1, 0) && table->item(1, 0)->text().mid(3) == oldFirstAction,
                   "insert before altered the previous first action");
            menu = openRowMenu(0);
            undo = action(menu, "listUndo");
            if (undo && undo->isEnabled()) undo->trigger();
            else expect(false, "undo after insertion unavailable");
            closeMenu(menu);
            expect(table->rowCount() == before + 2, "undo insertion did not restore list");
            expect(table->item(0, 0) && table->item(0, 0)->text().mid(3) == oldFirstAction,
                   "undo insertion did not restore the original first action");

            // Multi-row deletion must erase each corresponding window binding.
            // Otherwise the surviving actions can silently target wrong windows.
            Plan windowPlan{
                {Wait{100}, Wait{200}, Wait{300}, Wait{400}}, 1, 0, false,
                {{"Window A", "a.exe", false, 0},
                 {"Window B", "b.exe", false, 0},
                 {"Window C", "c.exe", false, 0},
                 {"Window D", "d.exe", false, 0}}};
            lab->replaceActions(windowPlan);
            expect(table->rowCount() == 4, "bound test plan did not load");
            table->setCurrentCell(1, 0,
                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            table->selectionModel()->select(table->model()->index(2, 0),
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
            expect(table->selectionModel()->selectedRows().size() == 2,
                   "multi-row selection failed");
            QShortcut* deleteShortcut = nullptr;
            for (auto* shortcut : table->findChildren<QShortcut*>()) {
                if (shortcut->key() == QKeySequence(Qt::Key_Delete)) {
                    deleteShortcut = shortcut;
                    break;
                }
            }
            expect(deleteShortcut && QMetaObject::invokeMethod(
                deleteShortcut, "activated", Qt::DirectConnection),
                "Delete shortcut missing or not invokable");
            expect(table->rowCount() == 2, "multi-row deletion did not remove two steps");
            expect(table->item(0, 1) && table->item(0, 1)->text().contains(QStringLiteral("Window A")) &&
                   table->item(1, 1) && table->item(1, 1)->text().contains(QStringLiteral("Window D")),
                   "multi-row deletion lost or shifted surviving window bindings");
            menu = openRowMenu(0);
            undo = action(menu, "listUndo");
            if (undo && undo->isEnabled()) undo->trigger();
            else expect(false, "undo of bound multi-row deletion unavailable");
            closeMenu(menu);
            expect(table->rowCount() == 4 && table->item(1, 1) &&
                   table->item(1, 1)->text().contains(QStringLiteral("Window B")) &&
                   table->item(2, 1) && table->item(2, 1)->text().contains(QStringLiteral("Window C")),
                   "undo of multi-row deletion did not restore window bindings");

            if (!ok) qCritical() << "Step context menu smoke test failed";
            app.exit(ok ? 0 : 1);
        });
    }
    if (app.arguments().contains(QStringLiteral("--smoke-test-icons"))) {
        // Pure UI regression: no system input, no files, and no automation starts.
        QTimer::singleShot(90, &app, [&app, &window] {
            const auto* nav = window.findChild<QListWidget*>(QStringLiteral("navigation"));
            const auto* run = window.findChild<QPushButton*>(QStringLiteral("primaryButton"));
            const auto* click = window.findChild<QPushButton*>(QStringLiteral("inputLabQuickClick"));
            const auto* key = window.findChild<QPushButton*>(QStringLiteral("inputLabQuickKey"));
            const auto* text = window.findChild<QPushButton*>(QStringLiteral("inputLabQuickText"));
            const auto* record = window.findChild<QPushButton*>(QStringLiteral("inputLabRecord"));
            bool ok = nav && nav->count() == 3 && run && !run->icon().isNull() &&
                      click && !click->icon().isNull() && key && !key->icon().isNull() &&
                      text && !text->icon().isNull() && record && !record->icon().isNull();
            if (nav) for (int i = 0; i < nav->count(); ++i)
                ok &= !nav->item(i)->icon().isNull();
            if (!ok) qCritical() << "ClickNova icon and navigation smoke test failed";
            app.exit(ok ? 0 : 1);
        });
    }
    if (app.arguments().contains(QStringLiteral("--smoke-test-recording-review"))) {
        // Tests the review UI without installing hooks or generating input.
        QTimer::singleShot(70, &app, [&app, &window] {
            using namespace clicknova::input;
            std::vector<Action> sample{ButtonDown{Button::Left},
                                       MovePointer{MoveMode::Absolute, 200, 150, 500},
                                       ButtonUp{Button::Left}, Wait{200}};
            std::vector<clicknova::recording::WindowInfo> origins(sample.size(),
                {u"Example browser", u"browser.exe"});
            RecordingReviewDialog review(std::move(sample), 42, 1200, true, &window,
                                         std::move(origins));
            review.show();
            app.processEvents();
            auto* table = review.findChild<QTableWidget*>(QStringLiteral("recordingReviewActions"));
            auto* summary = review.findChild<QLabel*>(QStringLiteral("reviewSummary"));
            auto* remove = review.findChild<QPushButton*>(QStringLiteral("reviewRemove"));
            auto* replace = review.findChild<QPushButton*>(QStringLiteral("reviewReplace"));
            auto* saving = review.findChild<QCheckBox*>(QStringLiteral("reviewSaveOptional"));
            bool ok = table && table->rowCount() == 4 && table->columnCount() == 3 &&
                      table->item(0, 2) &&
                      table->item(0, 2)->text().contains(QStringLiteral("Example browser")) && summary &&
                      summary->text().contains(QStringLiteral("42")) && remove && replace &&
                      saving && !saving->isChecked();
            if (ok) {
                table->selectRow(0);
                remove->click(); // Removes the matching mouse-up as well.
                app.processEvents();
                ok &= table->rowCount() == 2 && review.actions().size() == 2;
                replace->click();
                ok &= review.decision() == RecordingReviewDialog::Decision::Replace &&
                      !review.saveRequested();
            }
            if (!ok) qCritical() << "Recording review GUI smoke test failed";
            app.exit(ok ? 0 : 1);
        });
    }
    if (app.arguments().contains(QStringLiteral("--smoke-test-recorder"))) {
        // Never install native hooks in tests: inspect the floating panel and cancel.
        QTimer::singleShot(90, &app, [&app, &window] {
            auto* nav = window.findChild<QListWidget*>(QStringLiteral("navigation"));
            if (nav) nav->setCurrentRow(1);
            auto* record = window.findChild<QPushButton*>(QStringLiteral("inputLabRecord"));
            auto* recovery = window.findChild<QPushButton*>(QStringLiteral("inputLabRecoverRecording"));
            auto* actions = window.findChild<QTableWidget*>(QStringLiteral("inputLabActions"));
            bool ok = nav && record && recovery && actions && record->isEnabled();
            // Recovery stays hidden until an unfinished checkpoint exists.

            const int oldCount = actions ? actions->rowCount() : -1;
            if (ok) {
                record->click();
                app.processEvents();
                QDialog* overlay = nullptr;
                for (auto* w : QApplication::topLevelWidgets()) {
                    if (w->objectName() == QStringLiteral("inputRecorderOverlay")) {
                        overlay = qobject_cast<QDialog*>(w);
                        break;
                    }
                }
                auto* start = overlay ? overlay->findChild<QPushButton*>(QStringLiteral("recorderStart")) : nullptr;
                auto* pause = overlay ? overlay->findChild<QPushButton*>(QStringLiteral("recorderPause")) : nullptr;
                auto* stop = overlay ? overlay->findChild<QPushButton*>(QStringLiteral("recorderStop")) : nullptr;
                auto* cancel = overlay ? overlay->findChild<QPushButton*>(QStringLiteral("recorderCancel")) : nullptr;
                auto* keyboard = overlay ? overlay->findChild<QCheckBox*>(QStringLiteral("recorderKeyboard")) : nullptr;
                auto* movement = overlay ? overlay->findChild<QCheckBox*>(QStringLiteral("recorderMovement")) : nullptr;
                auto* textInput = overlay ? overlay->findChild<QCheckBox*>(QStringLiteral("recorderTypingText")) : nullptr;
                auto* windowInfo = overlay ? overlay->findChild<QLabel*>(QStringLiteral("recorderActiveWindow")) : nullptr;
                ok &= overlay && overlay->isVisible() && start && start->isEnabled() &&
                      pause && !pause->isEnabled() && stop && !stop->isEnabled() &&
                      cancel && keyboard && movement && movement->isChecked() &&
                      textInput && windowInfo && textInput->isEnabled() == keyboard->isChecked();
                // QDialog's default Escape/reject behavior used to hide the
                // recorder and discard its in-memory events without a review.
                if (overlay) {
                    overlay->reject();
                    app.processEvents();
                    ok &= overlay->isVisible() && record->isEnabled();
                    record->click(); // Restore/open the existing recorder.
                    app.processEvents();
                    ok &= overlay->isVisible();
                }
                if (cancel) cancel->click();
                app.processEvents();
                ok &= actions->rowCount() == oldCount && record->isEnabled();
            }
            if (!ok) qCritical() << "Floating input recorder smoke test failed";
            app.exit(ok ? 0 : 1);
        });
    }
    if (app.arguments().contains(QStringLiteral("--smoke-test-quick-motion"))) {
        // Only inspect UI state: never synthesize a mouse event in a smoke test.
        QTimer::singleShot(100, &app, [&app, &window] {
            auto* nav = window.findChild<QListWidget*>(QStringLiteral("navigation"));
            auto* mode = window.findChild<QComboBox*>(QStringLiteral("quickActionMode"));
            auto* button = window.findChild<QComboBox*>(QStringLiteral("quickMouseButton"));
            auto* destination = window.findChild<QGroupBox*>(QStringLiteral("quickMoveGroup"));
            auto* x = window.findChild<QSpinBox*>(QStringLiteral("quickDestinationX"));
            auto* y = window.findChild<QSpinBox*>(QStringLiteral("quickDestinationY"));
            auto* duration = window.findChild<QSpinBox*>(QStringLiteral("quickMoveDuration"));
            auto* capture = window.findChild<QPushButton*>(QStringLiteral("quickCaptureDestination"));
            auto* summary = window.findChild<QLabel*>(QStringLiteral("quickMoveReadiness"));
            bool ok = nav && mode && button && destination && x && y &&
                      duration && capture && summary && mode->count() == 4 && button->count() == 5;
            if (ok) {
                nav->setCurrentRow(0);
                mode->setCurrentIndex(2);
                app.processEvents();
                ok &= destination->isVisible() && button->isEnabled() && capture->isEnabled();
                ok &= summary->text().contains(QStringLiteral("not set"));
                x->setValue(350);
                y->setValue(420);
                duration->setValue(800);
                ok &= summary->text().contains(QStringLiteral("ready"));
                mode->setCurrentIndex(1);
                ok &= destination->isVisible() && !button->isEnabled();
                mode->setCurrentIndex(3);
                ok &= !destination->isVisible();
                mode->setCurrentIndex(0);
                ok &= !destination->isVisible() && button->isEnabled();
            }
            if (!ok) qCritical() << "Quick motion UI smoke test failed";
            app.exit(ok ? 0 : 1);
        });
    }
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
            // A Windows GUI-subsystem app may not forward qCritical() to CTest.
            // Write individual failed assertions to a dedicated diagnostic log.
            const QString diagnosticPath = QCoreApplication::applicationDirPath() +
                QStringLiteral("/gui-sequence-smoke.log");
            QFile::remove(diagnosticPath); // Do not report a failure from an earlier run.
            bool pass = true;
            const auto expect = [&pass, &diagnosticPath](bool result, const char* message) {
                if (!result) {
                    pass = false;
                    qCritical().noquote() << "gui_sequence_smoke:" << message;
                    QFile log(diagnosticPath);
                    if (log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                        log.write("gui_sequence_smoke: ");
                        log.write(message);
                        log.write("\n");
                    }
                }
                return result;
            };
            auto* nav = window.findChild<QListWidget*>(QStringLiteral("navigation"));
            auto* table = window.findChild<QTableWidget*>(QStringLiteral("inputLabActions"));
            auto* types = window.findChild<QComboBox*>(QStringLiteral("inputLabType"));
            auto* quickClick = window.findChild<QPushButton*>(QStringLiteral("inputLabQuickClick"));
            auto* quickWait = window.findChild<QPushButton*>(QStringLiteral("inputLabQuickWait"));
            auto* quickWaitUntil = window.findChild<QPushButton*>(QStringLiteral("inputLabQuickWaitUntil"));
            auto* quickCode = window.findChild<QPushButton*>(QStringLiteral("inputLabQuickCode"));
            auto* previewNext = window.findChild<QLabel*>(QStringLiteral("inputLabWaitNextAt"));
            auto* liveNext = window.findChild<QLabel*>(QStringLiteral("inputLabNextStepTime"));
            auto* waitExpression = window.findChild<QLineEdit*>(QStringLiteral("inputLabWaitExpression"));
            auto* waitSyntaxHint = window.findChild<QLabel*>(QStringLiteral("inputLabWaitSyntaxHint"));
            auto* script = window.findChild<QPlainTextEdit*>(QStringLiteral("inputLabScript"));
            auto* clock = window.findChild<QTimeEdit*>(QStringLiteral("inputLabWaitUntilClock"));
            auto* clockPreview = window.findChild<QLabel*>(QStringLiteral("inputLabWaitUntilPreview"));
            auto* record = window.findChild<QPushButton*>(QStringLiteral("inputLabRecord"));
            auto* run = window.findChild<QPushButton*>(QStringLiteral("inputLabRun"));
            auto* windowPicker = window.findChild<QPushButton*>(QStringLiteral("inputLabChooseWindow"));
            auto* stepWindowPicker = window.findChild<QPushButton*>(QStringLiteral("inputLabStepChooseWindow"));
            auto* stepWindowClear = window.findChild<QPushButton*>(QStringLiteral("inputLabStepClearWindow"));
            auto* stepWindowEnforce = window.findChild<QCheckBox*>(QStringLiteral("inputLabStepEnforceWindow"));
            auto* stepWindowActivate = window.findChild<QCheckBox*>(QStringLiteral("inputLabStepActivateWindow"));
            auto* stepWindowWait = window.findChild<QLineEdit*>(QStringLiteral("inputLabStepWindowWaitMs"));
            auto* library = window.findChild<QComboBox*>(QStringLiteral("inputLabSavedPlans"));
            if (expect(nav && table && types && quickClick && quickWait && quickWaitUntil && quickCode && script &&
                       record && run && windowPicker && library && stepWindowPicker && stepWindowClear &&
                       stepWindowEnforce && stepWindowActivate && stepWindowWait && previewNext && liveNext &&
                       waitExpression && waitSyntaxHint && clock && clockPreview,
                       "required workspace/timing controls missing")) {
                // v0.5.17 added PowerShell as action type 12 (13 types total).
                // Check type identities instead of assuming there are exactly 12.
                expect(nav->count() == 3, "navigation should contain three sections");
                expect(types->count() >= 13 && types->findData(7) == 7 &&
                       types->findData(12) == 12 && types->findData(13) == 13, "Wait, PowerShell or wait-until action type missing");
                nav->setCurrentRow(1);
                app.processEvents();
                expect(quickClick->isVisible() && quickCode->isVisible() &&
                       record->isVisible() && run->isVisible() &&
                       !run->text().isEmpty() && windowPicker->isVisible(),
                       "unified workspace buttons not visible");
                const int before = table->rowCount();
                quickClick->click();
                quickWait->click();
                expect(table->rowCount() == before + 2,
                       "click and wait quick actions were not appended");
                if (table->rowCount() > 0) {
                    table->setCurrentCell(table->rowCount() - 1, 0);
                    expect(types->currentData().toInt() == 7,
                           "Wait action did not load into the shared editor");
                    expect(previewNext->isVisible() && !liveNext->text().isEmpty(),
                           "next-step clock preview or live status missing");
                    waitExpression->setText(QStringLiteral("3д 2ч 15мин 8с 250мс"));
                    app.processEvents();
                    expect(table->item(table->rowCount()-1, 1)->text().contains(QStringLiteral("250")) &&
                           table->item(table->rowCount()-1, 1)->text().contains(QStringLiteral("3")),
                           "composite wait duration was not saved into the selected step");
                    expect(previewNext->text().contains(QRegularExpression(QStringLiteral("[0-9]{2}\\.[0-9]{2}\\.[0-9]{4}"))),
                           "next-step clock preview does not show a local date for a multi-day wait");
                    waitExpression->setText(QStringLiteral("invalid"));
                    app.processEvents();
                    expect(previewNext->text().contains(QStringLiteral("Invalid")) ||
                           previewNext->text().contains(QStringLiteral("Неверн")),
                           "invalid duration expression must be reported to the user");
                    waitExpression->setText(QStringLiteral("3д 2ч 15мин 8с 250мс"));
                }
                quickCode->click(); // Only inserts a script step; does NOT execute it.
                expect(table->rowCount() == before + 3,
                       "PowerShell quick action was not appended");
                if (table->rowCount() > 0) {
                    table->setCurrentCell(table->rowCount() - 1, 0);
                    expect(types->currentData().toInt() == 12 && script->isVisible(),
                           "PowerShell step is not editable in the shared editor");
                }
                // A clock-based delay is a NEW, editable action in the SAME table.
                const int beforeClock = table->rowCount();
                quickWaitUntil->click();
                expect(table->rowCount() == beforeClock + 1,
                       "Wait-until quick action was not appended");
                table->setCurrentCell(table->rowCount() - 1, 0);
                app.processEvents();
                expect(types->currentData().toInt() == 13 && clock->isVisible() &&
                       clockPreview->isVisible(), "Wait-until clock editor is unavailable");
                clock->setTime(QTime(18, 30, 0));
                app.processEvents();
                expect(clockPreview->text().contains(QStringLiteral("18:30:00")),
                       "Wait-until local time is missing from the preview");
                expect(table->item(table->rowCount()-1, 1)->text().contains(QStringLiteral("18:30:00")),
                       "Wait-until time was not saved in the step table");
                nav->setCurrentRow(0);
            }
            if (!pass) qCritical() << "Unified Sequence UI smoke test failed";
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
    if (app.arguments().contains(QStringLiteral("--smoke-test-input-lab"))) {
        // UI-only verification: no real input is injected.
        QTimer::singleShot(80, &app, [&app, &window] {
            auto* nav = window.findChild<QListWidget*>(QStringLiteral("navigation"));
            if (nav && nav->count() >= 3) nav->setCurrentRow(1);
            auto* lab = window.findChild<QWidget*>(QStringLiteral("inputLab"));
            auto* table = window.findChild<QTableWidget*>(QStringLiteral("inputLabActions"));
            auto* add = window.findChild<QPushButton*>(QStringLiteral("inputLabQuickKey"));
            auto* type = window.findChild<QComboBox*>(QStringLiteral("inputLabType"));
            auto* run = window.findChild<QPushButton*>(QStringLiteral("inputLabRun"));
            auto* saved = window.findChild<QComboBox*>(QStringLiteral("inputLabSavedPlans"));
            auto* save = window.findChild<QPushButton*>(QStringLiteral("inputLabSavePlan"));
            auto* load = window.findChild<QPushButton*>(QStringLiteral("inputLabLoadPlan"));
            auto* erase = window.findChild<QPushButton*>(QStringLiteral("inputLabDeletePlan"));
            auto* moveMode = window.findChild<QComboBox*>(QStringLiteral("inputLabMoveMode"));
            auto* moveX = window.findChild<QSpinBox*>(QStringLiteral("inputLabMoveX"));
            auto* moveY = window.findChild<QSpinBox*>(QStringLiteral("inputLabMoveY"));
            auto* moveDuration = window.findChild<QSpinBox*>(QStringLiteral("inputLabMoveDuration"));
            auto* timedHold = window.findChild<QSpinBox*>(QStringLiteral("inputLabHoldMs"));
            auto* text = window.findChild<QPlainTextEdit*>(QStringLiteral("inputLabText"));
            auto* captureMouse = window.findChild<QPushButton*>(QStringLiteral("inputLabCaptureMouse"));
            auto* captureKey = window.findChild<QPushButton*>(QStringLiteral("inputLabCaptureKey"));
            auto* quickHold = window.findChild<QSpinBox*>(QStringLiteral("quickHoldDuration"));
            bool pass = nav && lab && table && add && type && run && lab->isVisible() &&
                        saved && save && load && erase && timedHold && text && captureMouse && captureKey && quickHold &&
                        moveMode && moveX && moveY && moveDuration;
            if (pass) {
                const int before = table->rowCount();
                type->setCurrentIndex(1); // Key press.
                add->click();
                pass &= table->rowCount() == before + 1 &&
                        table->item(table->rowCount() - 1, 0)->text().contains(QStringLiteral("Key"));
                pass &= run->isEnabled() && save->isEnabled();
                pass &= !load->isEnabled() && !erase->isEnabled();
                type->setCurrentIndex(8); // Timed mouse hold.
                pass &= timedHold->isVisible();
                timedHold->setValue(425);
                pass &= table->item(table->rowCount() - 1, 1)->text().contains(QStringLiteral("425"));
                type->setCurrentIndex(10); // Type text.
                pass &= text->isVisible();
                text->setPlainText(QString::fromUtf8("Hello, мир!"));
                pass &= table->item(table->rowCount() - 1, 1)->text().contains(QStringLiteral("characters"));
                type->setCurrentIndex(11); // Pointer movement.
                pass &= moveMode->isVisible() && moveX->isVisible() &&
                        moveY->isVisible() && moveDuration->isVisible();
                moveMode->setCurrentIndex(1); // Screen coordinates.
                moveX->setValue(450);
                moveY->setValue(220);
                moveDuration->setValue(250);
                pass &= table->item(table->rowCount() - 1, 1)->text().contains(QStringLiteral("450")) &&
                        table->item(table->rowCount() - 1, 1)->text().contains(QStringLiteral("250"));
                type->setCurrentIndex(4); // Untimed mouse button down.
                pass &= table->item(table->rowCount() - 1, 0)->text().contains(QStringLiteral("until"));
                pass &= quickHold->maximum() == 60000;
                // The common tasks must be one click away. Rare editing tools
                // and saved plans are available but not permanently expanded.
                auto* quickClick = window.findChild<QPushButton*>(QStringLiteral("inputLabQuickClick"));
                auto* quickKey = window.findChild<QPushButton*>(QStringLiteral("inputLabQuickKey"));
                auto* quickText = window.findChild<QPushButton*>(QStringLiteral("inputLabQuickText"));
                auto* quickWait = window.findChild<QPushButton*>(QStringLiteral("inputLabQuickWait"));
                auto* quickCode = window.findChild<QPushButton*>(QStringLiteral("inputLabQuickCode"));
                auto* selectAll = window.findChild<QPushButton*>(QStringLiteral("inputLabSelectAll"));
                auto* autosave = window.findChild<QLabel*>(QStringLiteral("inputLabAutosaveStatus"));
                auto* more = window.findChild<QPushButton*>(QStringLiteral("inputLabMoreTools"));
                auto* extra = window.findChild<QWidget*>(QStringLiteral("inputLabSecondaryTools"));
                auto* menuButton = table->cellWidget(table->rowCount() - 1, 2);
                auto* savedToggle = window.findChild<QPushButton*>(QStringLiteral("inputLabSavedToggle"));
                auto* savedGroup = window.findChild<QGroupBox*>(QStringLiteral("inputLabSavedGroup"));
                pass &= quickClick && quickKey && quickText && quickWait && quickCode &&
                        selectAll && autosave &&
                        table->selectionMode() == QAbstractItemView::ExtendedSelection &&
                        !more && !extra && menuButton && savedToggle && savedGroup && !savedGroup->isVisible();
                if (quickClick && quickText && quickKey && quickWait) {
                    const int rows = table->rowCount();
                    quickClick->click();
                    pass &= table->rowCount() == rows + 1 &&
                            table->item(rows, 0)->text().contains(QStringLiteral("Mouse click"));
                    quickText->click();
                    pass &= table->rowCount() == rows + 2 &&
                            table->item(rows + 1, 0)->text().contains(QStringLiteral("Type text"));
                    quickKey->click();
                    pass &= table->rowCount() == rows + 3 &&
                            table->item(rows + 2, 0)->text().contains(QStringLiteral("Key press"));
                    quickWait->click();
                    pass &= table->rowCount() == rows + 4 &&
                            table->item(rows + 3, 0)->text().contains(QStringLiteral("Wait"));
                    pass &= !quickClick->toolTip().isEmpty() &&
                            quickClick->property("actionRole").toString() == QStringLiteral("primary");
                }
                if (savedToggle && savedGroup) {
                    savedToggle->click(); pass &= savedGroup->isVisible();
                }
            }
            if (!pass) qCritical() << "Input Lab UI smoke test failed";
            app.exit(pass ? 0 : 1);
        });
    }
    if (app.arguments().contains(QStringLiteral("--smoke-test-adaptive"))) {
        QTimer::singleShot(90, &app, [&app, &window] {
            window.resize(820, 640);
            auto* nav = window.findChild<QListWidget*>(QStringLiteral("navigation"));
            if (nav) nav->setCurrentRow(1);
            app.processEvents();
            auto* table = window.findChild<QTableWidget*>(QStringLiteral("inputLabActions"));
            auto* quick = window.findChild<QPushButton*>(QStringLiteral("inputLabQuickClick"));
            auto* record = window.findChild<QPushButton*>(QStringLiteral("inputLabRecord"));
            auto* run = window.findChild<QPushButton*>(QStringLiteral("inputLabRun"));
            const bool pass = nav && nav->width() <= 150 && table && quick && record && run &&
                quick->isVisible() && record->isVisible() && run->isVisible() &&
                !table->toolTip().isEmpty() && !run->toolTip().isEmpty();
            if (!pass) qCritical() << "Adaptive unified layout smoke test failed";
            app.exit(pass ? 0 : 1);
        });
    }
    if (app.arguments().contains(QStringLiteral("--smoke-test-ui"))) {
        // Headless-ish layout check: does not send clicks or register a macro.
        QTimer::singleShot(50, &app, [&app, &window] {
            window.resize(500, 360);
            app.processEvents();
            const QString diagnosticPath = QCoreApplication::applicationDirPath() +
                QStringLiteral("/gui-layout-smoke.log");
            QFile::remove(diagnosticPath); // No stale report after a successful run.
            QStringList layoutIssues;
            const auto checkLayout = [&layoutIssues](bool passed, const QString& reason) {
                if (!passed) layoutIssues.push_back(reason);
                return passed;
            };
            bool fieldsHaveMinimumHeight = true;
            for (const auto* spin : window.findChildren<QAbstractSpinBox*>()) {
                fieldsHaveMinimumHeight &= checkLayout(spin->height() >= 44,
                    QStringLiteral("spinbox %1: height=%2, minHeight=%3 (expected >=44)")
                        .arg(spin->objectName().isEmpty() ? QStringLiteral("<unnamed>") : spin->objectName())
                        .arg(spin->height()).arg(spin->minimumHeight()));
            }
            for (const auto* combo : window.findChildren<QComboBox*>()) {
                fieldsHaveMinimumHeight &= checkLayout(combo->height() >= 44,
                    QStringLiteral("combo %1: height=%2, minHeight=%3 (expected >=44)")
                        .arg(combo->objectName().isEmpty() ? QStringLiteral("<unnamed>") : combo->objectName())
                        .arg(combo->height()).arg(combo->minimumHeight()));
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
            fieldsHaveMinimumHeight &= checkLayout(nav != nullptr && nav->count() == 3 &&
                nav->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
                QStringLiteral("navigation missing, wrong page count, or horizontal scrollbar enabled"));
            auto* clickTabs = window.findChild<QTabWidget*>(QStringLiteral("clickConfigurationTabs"));
            auto* settingsTabs = window.findChild<QTabWidget*>(QStringLiteral("personalSettingsTabs"));
            auto* optionalDetails = window.findChild<QCheckBox*>(QStringLiteral("showOptionalDetails"));
            fieldsHaveMinimumHeight &= checkLayout(clickTabs && clickTabs->count() == 2 &&
                settingsTabs && settingsTabs->count() == 3 && optionalDetails,
                QStringLiteral("expected click tabs=2, settings tabs=3 and details toggle"));
            if (optionalDetails && clickTabs) {
                optionalDetails->setChecked(true);
                fieldsHaveMinimumHeight &= checkLayout(clickTabs->isVisible(),
                    QStringLiteral("optional click configuration tabs not visible after expansion"));
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
            fieldsHaveMinimumHeight &= checkLayout(scrolls.size() == 7,
                QStringLiteral("expected 7 scroll areas, got %1").arg(scrolls.size()));
            // Wheel-over-input must scroll the containing page, not change settings.
            auto* interval = window.findChild<QSpinBox*>(QStringLiteral("quickClickInterval"));
            auto* startDelay = window.findChild<QSpinBox*>(QStringLiteral("quickStartDelayValue"));
            auto* startUnit = window.findChild<QComboBox*>(QStringLiteral("quickStartDelayUnit"));
            auto* startMode = window.findChild<QComboBox*>(QStringLiteral("quickStartMode"));
            auto* startAt = window.findChild<QTimeEdit*>(QStringLiteral("quickStartAtTime"));
            const bool startControlsOK = startMode && startAt && startMode->count() == 2 &&
                startMode->currentIndex() == 0 && startMode->minimumHeight() >= 44 &&
                startAt->minimumHeight() >= 44 &&
                startAt->displayFormat() == QStringLiteral("HH:mm:ss");
            fieldsHaveMinimumHeight &= checkLayout(startControlsOK,
                QStringLiteral("start controls: modePresent=%1, timePresent=%2, modeCount=%3, "
                               "modeIndex=%4, modeMinHeight=%5, timeMinHeight=%6, format=%7")
                    .arg(static_cast<int>(startMode != nullptr)).arg(static_cast<int>(startAt != nullptr))
                    .arg(startMode ? startMode->count() : -1)
                    .arg(startMode ? startMode->currentIndex() : -1)
                    .arg(startMode ? startMode->minimumHeight() : -1)
                    .arg(startAt ? startAt->minimumHeight() : -1)
                    .arg(startAt ? startAt->displayFormat() : QStringLiteral("<none>")));
            if (startMode && startAt && startDelay) {
                startMode->setCurrentIndex(1);
                fieldsHaveMinimumHeight &= checkLayout(startAt->isEnabled() && !startDelay->isEnabled(),
                    QStringLiteral("clock mode did not enable clock and disable delay"));
                startAt->setTime(QTime(23, 59, 58));
                fieldsHaveMinimumHeight &= checkLayout(startAt->time() == QTime(23, 59, 58),
                    QStringLiteral("clock editor failed to accept 23:59:58"));
                startMode->setCurrentIndex(0);
                fieldsHaveMinimumHeight &= checkLayout(startDelay->isEnabled() && !startAt->isEnabled(),
                    QStringLiteral("delay mode did not enable delay and disable clock"));
            }
            fieldsHaveMinimumHeight &= checkLayout(startDelay && startUnit && startDelay->minimum() == 0 &&
                startDelay->maximum() == std::numeric_limits<int>::max() &&
                startDelay->value() == 3 && startUnit->count() == 4,
                QStringLiteral("start-delay defaults, range or unit selection incorrect"));
            if (startDelay && startUnit) {
                startDelay->setValue(0);  // Immediate start is configurable.
                fieldsHaveMinimumHeight &= startDelay->value() == 0;
                startDelay->setValue(7);
                startUnit->setCurrentIndex(3);  // Seven days.
                fieldsHaveMinimumHeight &= startDelay->value() == 7 && startUnit->currentIndex() == 3;
            }
            auto* mouseButton = window.findChild<QComboBox*>(QStringLiteral("quickMouseButton"));
            auto* pages = window.findChild<QStackedWidget*>();
            auto* clickScroll = pages ? qobject_cast<QScrollArea*>(pages->widget(0)) : nullptr;
            fieldsHaveMinimumHeight &= checkLayout(interval && mouseButton && clickScroll,
                QStringLiteral("Auto Clicker controls or scroll page missing"));
            if (interval && mouseButton && clickScroll) {
                auto* bar = clickScroll->verticalScrollBar();
                fieldsHaveMinimumHeight &= checkLayout(bar && bar->maximum() > 0,
                    QStringLiteral("Auto Clicker scrolling unavailable: range=%1")
                        .arg(bar ? bar->maximum() : -1));
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
                fieldsHaveMinimumHeight &= checkLayout(interval->value() == originalInterval,
                    QStringLiteral("wheel unexpectedly modified the click interval"));
                if (bar) fieldsHaveMinimumHeight &= checkLayout(bar->value() > 0,
                    QStringLiteral("wheel did not scroll: range=%1, position=%2")
                        .arg(bar->maximum()).arg(bar->value()));
                const int originalButton = mouseButton->currentIndex();
                wheelDown(mouseButton);
                fieldsHaveMinimumHeight &= checkLayout(mouseButton->currentIndex() == originalButton,
                    QStringLiteral("wheel unexpectedly modified mouse-button selection"));
            }
            if (!fieldsHaveMinimumHeight) {
                // Qt Windows-subsystem executables can lose stderr, so write a
                // useful report that the launcher can print on CTest failure.
                layoutIssues.push_back(QStringLiteral("navigation=%1, scrollAreas=%2, tabs=%3/%4")
                    .arg(nav ? nav->count() : -1)
                    .arg(scrolls.size())
                    .arg(clickTabs ? clickTabs->count() : -1)
                    .arg(settingsTabs ? settingsTabs->count() : -1));
                layoutIssues.push_back(QStringLiteral("start mode=%1, delay enabled=%2, time enabled=%3")
                    .arg(startMode ? startMode->currentIndex() : -1)
                    .arg(static_cast<int>(startDelay && startDelay->isEnabled()))
                    .arg(static_cast<int>(startAt && startAt->isEnabled())));
                const QString message = QStringLiteral("gui_layout_smoke: one or more layout checks failed\n") +
                    layoutIssues.join(QLatin1Char('\n')) + QLatin1Char('\n');
                qCritical().noquote() << message;
                QFile diagnostic(diagnosticPath);
                if (diagnostic.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
                    diagnostic.write(message.toUtf8());
            }
            app.exit(fieldsHaveMinimumHeight ? 0 : 1);
        });
    }
}
