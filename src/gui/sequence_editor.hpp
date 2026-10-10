// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "clicknova/sequence.hpp"
#include "clicknova/window_target.hpp"
#include "ui_translation.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDropEvent>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Move whole actions, not individual table cells (the default QTableWidget behavior).
class ActionTable final : public QTableWidget {
public:
    using QTableWidget::QTableWidget;
    std::function<void(int,int)> onMove;
protected:
    void dropEvent(QDropEvent* event) override {
        const int from = currentRow();
        const QModelIndex target = indexAt(event->position().toPoint());
        const int to = target.isValid() ? target.row() : rowCount() - 1;
        if (from >= 0 && to >= 0 && from != to && onMove) {
            onMove(from, to);
            event->acceptProposedAction();
        } else event->ignore();
    }
};

class SequenceEditor final : public QWidget {
public:
    explicit SequenceEditor(QWidget* parent = nullptr);

    std::function<void()> onCapture;
    std::function<void()> onPickClick;
    std::function<void()> onPickHover;
    std::function<void()> onPickWindow;
    std::function<void()> onChooseWindow;
    std::function<void()> onChange;
    void setSpaceProvider(std::function<clicknova::sequence::Space()> provider) { space_ = std::move(provider); }
    void setWindowsProvider(std::function<std::vector<clicknova::platform::WindowTarget>()> provider) {
        windows_ = std::move(provider);
        refreshWindows();
    }
    const std::vector<clicknova::sequence::Step>& steps() const { return steps_; }
    int passDelayMs() const { return passDelay_->value(); }
    void setPassDelayMs(int ms) { passDelay_->setValue(ms); }
    void setPickShortcutText(const QString& text, bool enabled) {
        pickShortcutText_ = text;
        pickShortcutEnabled_ = enabled;
        updateHoverLabel();
    }
    int captureDelaySeconds() const { return pickSeconds_->value(); }
    void setCaptureDelaySeconds(int seconds) { pickSeconds_->setValue(seconds); }
    void setSteps(const std::vector<clicknova::sequence::Step>& steps) {
        if (locked_ || !canUse(steps)) return;
        steps_ = steps;
        refresh(steps_.empty() ? -1 : 0);
    }
    void setInitialPoint(int x, int y, int button, int delay) {
        // Opening the Sequence page must not overwrite an already selected action.
        if (locked_ || hasSelectedAction()) return;
        x_->setValue(x); y_->setValue(y);
        button_->setCurrentIndex(button); delay_->setValue(delay);
    }
    std::optional<clicknova::platform::WindowTarget> pickedStepWindow() const {
        if (window_->currentIndex() < 2) return std::nullopt;
        const auto handle = window_->currentData(Qt::UserRole).toULongLong();
        const auto pid = window_->currentData(Qt::UserRole + 1).toUInt();
        if (!handle || !pid) return std::nullopt;
        return clicknova::platform::WindowTarget{
            static_cast<std::uintptr_t>(handle), pid,
            window_->currentData(Qt::UserRole + 2).toString().toStdWString()};
    }
    bool wantsOwnWindow() const { return window_->currentIndex() != 0; }
    bool hasSelectedAction() const {
        return table_->currentRow() >= 0 && table_->currentRow() < static_cast<int>(steps_.size());
    }
    // Action edits are committed immediately; dirty_ only controls Undo.
    bool hasUnsavedActionChanges() const { return false; }
    bool assignPickedWindow(const clicknova::platform::WindowTarget& target) {
        if (locked_ || !hasSelectedAction() || !target.handle || !target.processId) return false;
        const int row = table_->currentRow();
        auto& step = steps_[static_cast<std::size_t>(row)];
        // Coordinates from another window (or screen) must never be reused as
        // client coordinates of the newly selected target.
        const bool sameTarget = step.ownWindow && step.windowHandle == target.handle &&
                                step.processId == target.processId &&
                                step.space == clicknova::sequence::Space::Client;
        step.configured = step.configured && sameTarget;
        step.ownWindow = true;
        step.windowHandle = target.handle;
        step.processId = target.processId;
        step.windowTitle = QString::fromStdWString(target.title).toUtf8().toStdString();
        step.space = clicknova::sequence::Space::Client;
        refresh(row);
        return true;
    }
    void addCapturedPoint(int x, int y, clicknova::sequence::Space space) {
        if (locked_) return;
        const int row = table_->currentRow();
        if (row >= 0 && row < static_cast<int>(steps_.size())) {
            auto candidate = steps_;
            auto& step = candidate[static_cast<std::size_t>(row)];
            step.x = x; step.y = y; step.space = space; step.configured = true;
            assignWindow(step);
            if (!canUse(candidate)) return;
            steps_ = std::move(candidate);
            refresh(row);
        } else {
            clicknova::sequence::Step step{x, y, button_->currentIndex(), delay_->value(), space};
            step.clickCount = clicks_->value();
            step.intervalMs = interval_->value();
            assignWindow(step);
            append(step);
        }
    }
    void setLocked(bool value) {
        locked_ = value;
        const std::initializer_list<QWidget*> controls = {x_, y_, button_, delay_, interval_, clicks_, window_, add_, pick_, hover_, capture_, pickWindow_, chooseWindow_, pickSeconds_, passDelay_, windowSettingsToggle_, editToggle_};
        for (QWidget* w : controls) w->setEnabled(!locked_);
        revert_->setEnabled(!locked_ && dirty_);
        table_->setEnabled(!locked_);
        updateButtons();
    }
    void setLanguage(const QString& language) {
        language_ = language;
        hint_->setText(label(QStringLiteral("First add an action. Then select a point by click, shortcut or timer.")));
        detailGroup_->setTitle(label(QStringLiteral("Selected action")));
        windowSettingsToggle_->setText(label(QStringLiteral("Window settings (optional)")));
        note_->setText(label(QStringLiteral("Drag rows to reorder. Individual windows require foreground focus; saved windows must be selected again after restart.")));
        updateEditStatus();
        add_->setText(label(QStringLiteral("Add action")));
        pick_->setText(label(QStringLiteral("Pick with click")));
        capture_->setText(label(QStringLiteral("Timed capture")));
        updateHoverLabel();
        pickSeconds_->setToolTip(label(QStringLiteral("Capture delay in seconds")));
        update_->setText(label(QStringLiteral("Done")));
        revert_->setText(label(QStringLiteral("Undo edits")));
        editToggle_->setText(label(QStringLiteral("Edit selected action")));
        editToggle_->setToolTip(label(QStringLiteral("Show or hide details for the selected action")));
        add_->setToolTip(label(QStringLiteral("Create a new action. Select its row, then choose where it should click.")));
        pick_->setToolTip(label(QStringLiteral("ClickNova hides. Click once at the destination to record its position; Esc cancels.")));
        capture_->setToolTip(label(QStringLiteral("ClickNova hides and captures the cursor when the countdown ends. Use the capture shortcut to finish early.")));
        pickSeconds_->setToolTip(label(QStringLiteral("Time to position the cursor before automatic capture. Use the shortcut to capture earlier.")));
        chooseWindow_->setToolTip(label(QStringLiteral("Select a target from the list. A targeted click still requires the window to be active.")));
        pickWindow_->setToolTip(label(QStringLiteral("Hover over a window during the countdown to select it.")));
        passDelay_->setToolTip(label(QStringLiteral("Extra wait after the last action before the sequence repeats.")));
        x_->setToolTip(label(QStringLiteral("Horizontal click coordinate of the selected action.")));
        y_->setToolTip(label(QStringLiteral("Vertical click coordinate of the selected action.")));
        button_->setToolTip(label(QStringLiteral("Mouse button used by the selected action.")));
        clicks_->setToolTip(label(QStringLiteral("Number of mouse presses in the selected action.")));
        delay_->setToolTip(label(QStringLiteral("Wait before this action begins.")));
        interval_->setToolTip(label(QStringLiteral("Wait between clicks in the selected action.")));
        window_->setToolTip(label(QStringLiteral("Optional window for this action. Selecting a new window requires choosing coordinates again.")));
        update_->setToolTip(label(QStringLiteral("Close the editor. Changes are applied automatically.")));
        revert_->setToolTip(label(QStringLiteral("Restore the selected action to its values before editing.")));
        table_->setToolTip(label(QStringLiteral("Select a row to edit it. Drag its handle to reorder, or use the three-dot menu.")));

        xLabel_->setText(label(QStringLiteral("X coordinate")));
        yLabel_->setText(label(QStringLiteral("Y coordinate")));
        buttonLabel_->setText(label(QStringLiteral("Mouse button")));
        delayLabel_->setText(label(QStringLiteral("Wait before action")));
        intervalLabel_->setText(label(QStringLiteral("Interval between clicks")));
        passLabel_->setText(label(QStringLiteral("Wait between sequence passes")));
        clicksLabel_->setText(label(QStringLiteral("Clicks in this action")));
        windowLabel_->setText(label(QStringLiteral("Target window")));
        chooseWindow_->setText(label(QStringLiteral("Choose from list...")));
        pickWindow_->setText(label(QStringLiteral("Pick window by hovering")));
        pickWindow_->setToolTip(label(QStringLiteral("Hover over a window during the countdown to select it.")));
        table_->setHorizontalHeaderLabels({label(QStringLiteral("Step")), label(QStringLiteral("Position / window")),
                                          label(QStringLiteral("Clicks")), label(QStringLiteral("Timing")), QStringLiteral("⋯")});
        button_->setItemText(0, label(QStringLiteral("Left")));
        button_->setItemText(1, label(QStringLiteral("Right")));
        button_->setItemText(2, label(QStringLiteral("Middle")));
        if (!dirty_) refresh(table_->currentRow(), false);
        else { updateEditStatus(); updateButtons(); }
    }
private:
    void updateHoverLabel();
    void updateCompactTable();

    QString label(const QString& text) const;

    bool canUse(const std::vector<clicknova::sequence::Step>& candidate);

    void append(const clicknova::sequence::Step& step);

    void assignWindow(clicknova::sequence::Step& step) const;

    void refreshWindows();

    void showSelected(int row);

    void applyChanges();

    void menuForRow(int row, QPushButton* anchor);

    void markActionDirty();

    void updateEditStatus();

    void updateButtons();

    void updateRow(int row);

    void refresh(int row, bool notify = true);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:

    QString language_{QStringLiteral("en")};
    QString pickShortcutText_{QStringLiteral("F8")};
    bool pickShortcutEnabled_{true};
    std::vector<clicknova::sequence::Step> steps_;
    std::function<clicknova::sequence::Space()> space_{[] { return clicknova::sequence::Space::Screen; }};
    std::function<std::vector<clicknova::platform::WindowTarget>()> windows_;
    bool locked_{false};
    bool dirty_{false};
    bool loadingAction_{false};
    clicknova::sequence::Step undoStep_{};
    QLabel *hint_{}, *note_{}, *selection_{}, *count_{}, *editStatus_{};
    QLabel *xLabel_{}, *yLabel_{}, *buttonLabel_{}, *delayLabel_{}, *intervalLabel_{}, *passLabel_{}, *clicksLabel_{}, *windowLabel_{};
    QGroupBox* detailGroup_{};
    QToolButton* windowSettingsToggle_{};
    QToolButton* editToggle_{};
    QWidget* windowSettingsPanel_{};
    ActionTable* table_{};
    QSpinBox *x_{}, *y_{}, *delay_{}, *interval_{}, *passDelay_{}, *pickSeconds_{}, *clicks_{};
    QComboBox *button_{}, *window_{};
    QPushButton *add_{}, *pick_{}, *hover_{}, *update_{}, *revert_{}, *capture_{}, *pickWindow_{}, *chooseWindow_{};
};
