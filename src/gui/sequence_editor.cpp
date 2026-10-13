// SPDX-License-Identifier: Apache-2.0
#include "sequence_editor.hpp"
#include <QFormLayout>
#include <QResizeEvent>
#include <QTimer>
#include <limits>
#include "wrap_layout.hpp"
#include "ui_icons.hpp"

SequenceEditor::SequenceEditor(QWidget* parent) : QWidget(parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        hint_ = new QLabel(this);
        hint_->setWordWrap(true);
        layout->addWidget(hint_);

        auto* toolbar = new WrapLayout(nullptr, 8);
        add_ = new QPushButton(this);
        add_->setObjectName(QStringLiteral("sequenceAddPoint"));
        add_->setProperty("actionRole", QStringLiteral("primary"));
        add_->setMinimumHeight(42);
        pick_ = new QPushButton(this);
        pick_->setObjectName(QStringLiteral("sequencePickWithClick"));
        pick_->setProperty("actionRole", QStringLiteral("primary"));
        pick_->setMinimumHeight(42);
        hover_ = new QPushButton(this);
        hover_->setObjectName(QStringLiteral("sequencePickWithShortcut"));
        hover_->setProperty("actionRole", QStringLiteral("secondary"));
        hover_->setMinimumHeight(42);
        capture_ = new QPushButton(this);
        capture_->setMinimumHeight(42);
        pickSeconds_ = new QSpinBox(this);
        pickSeconds_->setObjectName(QStringLiteral("sequenceCaptureSeconds"));
        pickSeconds_->setRange(1, 60);
        pickSeconds_->setValue(5);
        pickSeconds_->setSuffix(QStringLiteral(" s"));
        pickSeconds_->setToolTip(QStringLiteral("Seconds before the cursor position is captured"));
        pickSeconds_->setMinimumHeight(44);
        pickSeconds_->setFixedWidth(80);
        count_ = new QLabel(this);
        toolbar->addWidget(add_);
        toolbar->addWidget(hover_);
        toolbar->addWidget(pick_);
        toolbar->addWidget(count_);
        layout->addLayout(toolbar);
        auto* secondaryToolbar = new WrapLayout(nullptr, 8);
        secondaryToolbar->addWidget(capture_);
        secondaryToolbar->addWidget(pickSeconds_);
        chooseWindow_ = new QPushButton(this);
        chooseWindow_->setObjectName(QStringLiteral("sequenceChooseWindow"));
        chooseWindow_->setProperty("actionRole", QStringLiteral("secondary"));
        chooseWindow_->setMinimumHeight(40);
        pickWindow_ = new QPushButton(this);
        pickWindow_->setObjectName(QStringLiteral("sequencePickWindowUnderCursor"));
        pickWindow_->setMinimumHeight(40);
        editToggle_ = new QToolButton(this);
        editToggle_->setObjectName(QStringLiteral("sequenceEditToggle"));
        editToggle_->setProperty("actionRole", QStringLiteral("secondary"));
        editToggle_->setCheckable(true);
        editToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        editToggle_->setArrowType(Qt::RightArrow);
        editToggle_->setMinimumHeight(40);
        secondaryToolbar->addWidget(chooseWindow_);
        secondaryToolbar->addWidget(pickWindow_);
        secondaryToolbar->addWidget(editToggle_);
        layout->addLayout(secondaryToolbar);

        table_ = new ActionTable(0, 5, this);
        table_->setObjectName(QStringLiteral("sequenceSteps"));
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setDragEnabled(true);
        table_->setAcceptDrops(true);
        table_->viewport()->setAcceptDrops(true);
        table_->setDragDropMode(QAbstractItemView::InternalMove);
        table_->setDefaultDropAction(Qt::MoveAction);
        table_->setDragDropOverwriteMode(false);
        table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        table_->setAlternatingRowColors(true);
        table_->setMinimumHeight(220);
        table_->setMaximumHeight(380);
        table_->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
        table_->verticalHeader()->setVisible(false);
        table_->verticalHeader()->setDefaultSectionSize(46);
        auto* header = table_->horizontalHeader();
        header->setSectionResizeMode(0, QHeaderView::Fixed);
        header->resizeSection(0, 65);
        header->setSectionResizeMode(1, QHeaderView::Stretch);
        header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(4, QHeaderView::Fixed);
        header->resizeSection(4, 56);
        table_->onMove = [this](int from, int to) {
            if (locked_) return;
            auto moved = steps_[static_cast<std::size_t>(from)];
            steps_.erase(steps_.begin() + from);
            steps_.insert(steps_.begin() + to, std::move(moved));
            refresh(to);
        };
        connect(table_, &QTableWidget::currentCellChanged, this,
                [this](int row, int, int previousRow, int) {
                    Q_UNUSED(previousRow);
                    showSelected(row);
                });
        layout->addWidget(table_, 1);

        detailGroup_ = new QGroupBox(this);
        auto* detail = new QVBoxLayout(detailGroup_);
        detail->setContentsMargins(12, 17, 12, 10);
        selection_ = new QLabel(this);
        detail->addWidget(selection_);
        auto* form = new QGridLayout;
        form->setHorizontalSpacing(12);
        form->setVerticalSpacing(6);
        xLabel_ = new QLabel(this); yLabel_ = new QLabel(this);
        buttonLabel_ = new QLabel(this); delayLabel_ = new QLabel(this); intervalLabel_ = new QLabel(this);
        clicksLabel_ = new QLabel(this); windowLabel_ = new QLabel(this);
        x_ = new QSpinBox(this); y_ = new QSpinBox(this);
        x_->setObjectName(QStringLiteral("sequenceCoordinateX"));
        y_->setObjectName(QStringLiteral("sequenceCoordinateY"));
        x_->setRange(-131072, 131072); y_->setRange(-131072, 131072);
        button_ = new QComboBox(this);
        button_->addItems({QStringLiteral("Left"), QStringLiteral("Right"), QStringLiteral("Middle")});
        delay_ = new QSpinBox(this); delay_->setRange(0, 60000);
        delay_->setValue(200); delay_->setSuffix(QStringLiteral(" ms"));
        interval_ = new QSpinBox(this); interval_->setObjectName(QStringLiteral("sequenceClickInterval"));
        interval_->setRange(10, 60000); interval_->setValue(200);
        interval_->setSuffix(QStringLiteral(" ms"));
        clicks_ = new QSpinBox(this); clicks_->setObjectName(QStringLiteral("sequenceStepClickCount"));
        clicks_->setRange(1, std::numeric_limits<int>::max()); clicks_->setValue(1);
        window_ = new QComboBox(this); window_->setObjectName(QStringLiteral("sequenceStepWindow"));
        const std::initializer_list<QWidget*> fields = {x_, y_, button_, delay_, interval_, clicks_, window_};
        for (QWidget* w : fields) w->setMinimumHeight(44);
        form->addWidget(xLabel_, 0, 0); form->addWidget(yLabel_, 0, 1);
        form->addWidget(x_, 1, 0); form->addWidget(y_, 1, 1);
        form->addWidget(buttonLabel_, 2, 0); form->addWidget(clicksLabel_, 2, 1);
        form->addWidget(button_, 3, 0); form->addWidget(clicks_, 3, 1);
        form->addWidget(delayLabel_, 4, 0); form->addWidget(intervalLabel_, 4, 1);
        form->addWidget(delay_, 5, 0); form->addWidget(interval_, 5, 1);

        // Window targeting is uncommon: keep it one click away rather than
        // taking up two permanent rows for every selected action.
        windowSettingsToggle_ = new QToolButton(detailGroup_);
        windowSettingsToggle_->setObjectName(QStringLiteral("sequenceWindowSettingsToggle"));
        windowSettingsToggle_->setCheckable(true);
        windowSettingsToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        windowSettingsToggle_->setArrowType(Qt::RightArrow);
        windowSettingsPanel_ = new QWidget(detailGroup_);
        windowSettingsPanel_->setObjectName(QStringLiteral("sequenceWindowSettingsPanel"));
        auto* windowForm = new QFormLayout(windowSettingsPanel_);
        windowForm->setContentsMargins(0, 6, 0, 0);
        windowForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        windowForm->addRow(windowLabel_, window_);
        windowSettingsPanel_->hide();
        connect(windowSettingsToggle_, &QToolButton::toggled, this, [this](bool expanded) {
            windowSettingsToggle_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            windowSettingsPanel_->setVisible(expanded);
        });
        auto* passRow = new QHBoxLayout;
        passLabel_ = new QLabel(this);
        passDelay_ = new QSpinBox(this);
        passDelay_->setObjectName(QStringLiteral("sequencePassDelay"));
        passDelay_->setRange(0, 60000); passDelay_->setSuffix(QStringLiteral(" ms"));
        passDelay_->setMinimumHeight(44);
        passRow->addWidget(passLabel_);
        passRow->addWidget(passDelay_);
        connect(passDelay_, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this](int) { if (!locked_ && onChange) onChange(); });
        connect(window_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int index) {
                    // A specific window always uses client-relative coordinates.
                    if (index > 0) { x_->setMinimum(0); y_->setMinimum(0); }
                    else { x_->setMinimum(-131072); y_->setMinimum(-131072); }
                });
        detail->addLayout(form);
        detail->addWidget(windowSettingsToggle_);
        detail->addWidget(windowSettingsPanel_);
        // This pause belongs to the full sequence, not the selected action.
        layout->insertLayout(layout->indexOf(table_), passRow);
        editStatus_ = new QLabel(this);
        editStatus_->setObjectName(QStringLiteral("actionEditStatus"));
        editStatus_->setWordWrap(true);
        detail->addWidget(editStatus_);
        update_ = new QPushButton(this);
        update_->setObjectName(QStringLiteral("sequenceUpdateSelected"));
        update_->setMinimumHeight(40);
        revert_ = new QPushButton(this);
        revert_->setObjectName(QStringLiteral("sequenceDiscardChanges"));
        revert_->setMinimumHeight(40);
        auto* editActions = new QHBoxLayout;
        editActions->addWidget(update_, 1);
        editActions->addWidget(revert_);
        detail->addLayout(editActions);
        connect(revert_, &QPushButton::clicked, this, [this] {
            if (!locked_ && dirty_ && hasSelectedAction()) {
                const int row = table_->currentRow();
                steps_[static_cast<std::size_t>(row)] = undoStep_;
                refresh(row);
            }
        });
        layout->addWidget(detailGroup_);
        detailGroup_->hide();
        connect(editToggle_, &QToolButton::toggled, this, [this](bool enabled) {
            editToggle_->setArrowType(enabled ? Qt::DownArrow : Qt::RightArrow);
            detailGroup_->setVisible(enabled && hasSelectedAction());
        });

        note_ = new QLabel(this); note_->setWordWrap(true);
        layout->addWidget(note_);
        connect(add_, &QPushButton::clicked, this, [this] {
            if (locked_) return;
            clicknova::sequence::Step step{};
            step.button = button_->currentIndex();
            step.delayMs = delay_->value();
            step.clickCount = clicks_->value();
            step.intervalMs = interval_->value();
            step.space = space_();
            step.configured = false;
            append(step);
            editToggle_->setChecked(true);
        });
        connect(update_, &QPushButton::clicked, this, [this] { applyChanges(); });
        for (auto* field : {x_, y_, delay_, interval_, clicks_}) {
            connect(field, QOverload<int>::of(&QSpinBox::valueChanged), this,
                    [this](int) { markActionDirty(); });
        }
        for (auto* choice : {button_, window_}) {
            connect(choice, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [this](int) { markActionDirty(); });
        }
        connect(chooseWindow_, &QPushButton::clicked, this, [this] {
            if (!locked_ && hasSelectedAction() && onChooseWindow) onChooseWindow();
        });
        connect(pickWindow_, &QPushButton::clicked, this, [this] {
            if (!locked_ && hasSelectedAction() && onPickWindow) onPickWindow();
        });
        connect(pick_, &QPushButton::clicked, this, [this] {
            if (!locked_ && onPickClick) onPickClick();
        });
        connect(capture_, &QPushButton::clicked, this, [this] {
            if (!locked_ && onCapture) onCapture();
        });
        connect(hover_, &QPushButton::clicked, this, [this] {
            if (!locked_ && onPickHover) onPickHover();
        });
        connect(pickSeconds_, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this](int) { if (onChange) onChange(); });
        using clicknova::gui::Icon;
        using clicknova::gui::setButtonIcon;
        setButtonIcon(add_, Icon::Click, QColor(QStringLiteral("#ffffff")));
        setButtonIcon(pick_, Icon::Target, QColor(QStringLiteral("#ffffff")));
        setButtonIcon(hover_, Icon::Cursor);
        setButtonIcon(capture_, Icon::Wait);
        setButtonIcon(chooseWindow_, Icon::Window);
        setButtonIcon(pickWindow_, Icon::Target);
        setButtonIcon(editToggle_, Icon::Edit);
        setButtonIcon(windowSettingsToggle_, Icon::Window);
        setButtonIcon(update_, Icon::Check);
        setButtonIcon(revert_, Icon::Undo);
        setLanguage(QStringLiteral("en"));
        refresh(-1);
    }

void SequenceEditor::updateHoverLabel() {
        hover_->setText(pickShortcutEnabled_
            ? label(QStringLiteral("Hover + %1")).arg(pickShortcutText_)
            : label(QStringLiteral("Hover + shortcut")));
        hover_->setToolTip(label(QStringLiteral("Switch windows using Alt+Tab, hover the target, then press the shown shortcut.")));
    }

QString SequenceEditor::label(const QString& text) const { return translateUiText(text, language_); }

bool SequenceEditor::canUse(const std::vector<clicknova::sequence::Step>& candidate) {
        if (candidate.size() > 128) return false;
        if (candidate.empty()) return true;
        auto check = candidate;
        for (auto& step : check) step.configured = true; // Drafts are editable but cannot execute.
        const clicknova::sequence::Plan plan{check, 1, false};
        const auto problem = clicknova::sequence::validate(plan);
        if (problem.empty()) return true;
        QMessageBox::warning(this, QStringLiteral("ClickNova"), label(QString::fromStdString(problem)));
        return false;
    }

void SequenceEditor::append(const clicknova::sequence::Step& step) {
        auto next = steps_;
        next.push_back(step);
        if (!canUse(next)) return;
        steps_ = std::move(next);
        refresh(static_cast<int>(steps_.size()) - 1);
        table_->scrollToBottom();
    }

void SequenceEditor::assignWindow(clicknova::sequence::Step& step) const {
        if (window_->currentIndex() == 0) {
            step.ownWindow = false; step.windowHandle = 0;
            step.processId = 0; step.windowTitle.clear();
            return;
        }
        step.ownWindow = true;
        step.space = clicknova::sequence::Space::Client;
        if (const auto target = pickedStepWindow()) {
            step.windowHandle = target->handle;
            step.processId = target->processId;
            step.windowTitle = window_->currentData(Qt::UserRole + 2).toString().toUtf8().toStdString();
        } else { step.windowHandle = 0; step.processId = 0; }
    }

void SequenceEditor::refreshWindows() {
        const QSignalBlocker blocker(window_);
        window_->clear();
        window_->addItem(label(QStringLiteral("Use Auto Clicker window setting")));
        window_->addItem(label(QStringLiteral("Choose a specific window...")));
        if (windows_) {
            const auto list = windows_();
            for (const auto& w : list) {
                const int index = window_->count();
                window_->addItem(QStringLiteral("%1 (PID %2)").arg(QString::fromStdWString(w.title)).arg(w.processId));
                window_->setItemData(index, QVariant::fromValue(static_cast<qulonglong>(w.handle)), Qt::UserRole);
                window_->setItemData(index, QVariant::fromValue(w.processId), Qt::UserRole + 1);
                window_->setItemData(index, QString::fromStdWString(w.title), Qt::UserRole + 2);
            }
        }
    }

void SequenceEditor::showSelected(int row) {
        loadingAction_ = true;
        if (row < 0 || row >= static_cast<int>(steps_.size())) {
            loadingAction_ = false; dirty_ = false;
            updateButtons(); updateEditStatus(); return;
        }
        const auto& s = steps_[static_cast<std::size_t>(row)];
        x_->setValue(s.x); y_->setValue(s.y);
        button_->setCurrentIndex(s.button); delay_->setValue(s.delayMs); interval_->setValue(s.intervalMs);
        clicks_->setValue(s.clickCount);
        refreshWindows();
        if (s.ownWindow) {
            int match = 1;
            for (int i = 2; i < window_->count(); ++i)
                if (window_->itemData(i, Qt::UserRole).toULongLong() == s.windowHandle &&
                    window_->itemData(i, Qt::UserRole + 1).toUInt() == s.processId) { match = i; break; }
            window_->setCurrentIndex(match);
        } else window_->setCurrentIndex(0);
        loadingAction_ = false;
        dirty_ = false;
        undoStep_ = s;
        updateButtons();
        updateEditStatus();
    }

void SequenceEditor::applyChanges() {
        if (locked_ || !hasSelectedAction()) return;
        // Edits are already reflected in the model. Done simply closes the panel.
        editToggle_->setChecked(false);
        dirty_ = false;
        undoStep_ = steps_[static_cast<std::size_t>(table_->currentRow())];
        updateButtons();
        updateEditStatus();
    }

void SequenceEditor::menuForRow(int row, QPushButton* anchor) {
        if (locked_ || row < 0 || row >= static_cast<int>(steps_.size())) return;
        QMenu menu(anchor);
        auto* edit = menu.addAction(label(QStringLiteral("Edit action")));
        auto* duplicate = menu.addAction(label(QStringLiteral("Duplicate")));
        auto* remove = menu.addAction(label(QStringLiteral("Delete action")));
        auto* picked = menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
        if (picked == edit) { table_->setCurrentCell(row, 0); editToggle_->setChecked(true); }
        else if (picked == duplicate && steps_.size() < 128) {
            const auto duplicateStep = steps_[static_cast<std::size_t>(row)];
            steps_.insert(steps_.begin() + row + 1, duplicateStep);
            refresh(row + 1);
        } else if (picked == remove) {
            steps_.erase(steps_.begin() + row);
            refresh(std::min(row, static_cast<int>(steps_.size()) - 1));
        }
    }

void SequenceEditor::markActionDirty() {
        if (loadingAction_ || locked_ || !hasSelectedAction()) return;
        const int row = table_->currentRow();
        auto& step = steps_[static_cast<std::size_t>(row)];
        const bool coordinatesChanged = step.x != x_->value() || step.y != y_->value();
        const bool windowChanged = window_->currentIndex() > 0 &&
            (!step.ownWindow || !pickedStepWindow() ||
             step.windowHandle != pickedStepWindow()->handle ||
             step.processId != pickedStepWindow()->processId);
        step.x = x_->value();
        step.y = y_->value();
        step.button = button_->currentIndex();
        step.delayMs = delay_->value();
        step.intervalMs = interval_->value();
        step.clickCount = clicks_->value();
        if (windowChanged) step.configured = false;
        if (window_->currentIndex() > 0) step.space = clicknova::sequence::Space::Client;
        else if (step.ownWindow) { step.space = space_(); step.configured = false; }
        assignWindow(step);
        // A newly added draft becomes configured after manually choosing its coordinates.
        // Window binding changes require a fresh point selection.
        if (coordinatesChanged && !windowChanged && !step.configured && window_->currentIndex() == 0)
            step.configured = true;
        dirty_ = true;
        updateRow(row);
        updateEditStatus();
        updateButtons();
        if (onChange) onChange();
    }

void SequenceEditor::updateEditStatus() {
        if (!editStatus_) return;
        editStatus_->setText(dirty_
            ? label(QStringLiteral("Changes are saved automatically. Undo is available until you select another action."))
            : label(QStringLiteral("Changes update the selected action immediately.")));
    }

void SequenceEditor::updateButtons() {
        const int row = table_->currentRow();
        const bool selected = row >= 0 && row < static_cast<int>(steps_.size());
        update_->setEnabled(!locked_ && selected);
        revert_->setEnabled(!locked_ && selected && dirty_);
        if (hint_) {
            const QString help = selected
                ? label(QStringLiteral("Selected action %1: choose its point, select a window, or open Edit to change clicks and delays.")).arg(row + 1)
                : label(QStringLiteral("First add an action. Then select a point by click, shortcut or timer."));
            hint_->setText(help);
        }
        editToggle_->setEnabled(!locked_ && selected);
        detailGroup_->setVisible(editToggle_->isChecked() && selected);
        add_->setEnabled(!locked_);
        pick_->setEnabled(!locked_ && selected);
        hover_->setEnabled(!locked_ && selected);
        pick_->setToolTip(label(QStringLiteral("ClickNova hides. Click once at the destination to record its position; Esc cancels.")));
        capture_->setEnabled(!locked_ && selected);
        pickWindow_->setEnabled(!locked_ && selected);
        chooseWindow_->setEnabled(!locked_ && selected);
        count_->setText(label(QStringLiteral("%1 actions")).arg(static_cast<int>(steps_.size())));
        selection_->setText(selected ? label(QStringLiteral("Selected action %1")).arg(row + 1)
                                     : label(QStringLiteral("Select an action or add a new one")));
    }

void SequenceEditor::updateRow(int row) {
        if (row < 0 || row >= static_cast<int>(steps_.size())) return;
        const auto& s = steps_[static_cast<std::size_t>(row)];
        QString point = s.configured ? QStringLiteral("(%1, %2)").arg(s.x).arg(s.y)
                                     : label(QStringLiteral("Choose point"));
        if (s.ownWindow) {
            point += QStringLiteral(" · %1").arg(s.windowTitle.empty()
                ? label(QStringLiteral("Select window")) : QString::fromUtf8(s.windowTitle.c_str()));
            if (!s.windowHandle) point += QStringLiteral(" · %1").arg(label(QStringLiteral("Rebind")));
        } else point += QStringLiteral(" · %1").arg(label(s.space == clicknova::sequence::Space::Client
                                                      ? QStringLiteral("Client") : QStringLiteral("Screen")));
        const QStringList cells = {QStringLiteral("☰ %1").arg(row + 1), point,
            QStringLiteral("%1 × %2").arg(s.clickCount).arg(button_->itemText(s.button)),
            QStringLiteral("%1 + %2 ms").arg(s.delayMs).arg(s.intervalMs)};
        for (int col = 0; col < cells.size(); ++col) {
            auto* cell = table_->item(row, col);
            if (!cell) { cell = new QTableWidgetItem; table_->setItem(row, col, cell); }
            cell->setText(cells[col]);
            if (col == 0) cell->setIcon(clicknova::gui::icon(
                s.configured ? clicknova::gui::Icon::Click : clicknova::gui::Icon::Target));
            cell->setToolTip(QStringLiteral("%1\n%2\n%3\n%4").arg(cells[0], cells[1], cells[2], cells[3]));
        }
    }

void SequenceEditor::refresh(int row, bool notify) {
        dirty_ = false;
        const QSignalBlocker block(table_);
        table_->setRowCount(static_cast<int>(steps_.size()));
        for (int i = 0; i < static_cast<int>(steps_.size()); ++i) {
            updateRow(i);
            auto* menuButton = new QPushButton(table_);
            menuButton->setIcon(clicknova::gui::icon(clicknova::gui::Icon::More));
            menuButton->setIconSize(QSize(21, 21));
            menuButton->setToolTip(label(QStringLiteral("Action options")));
            menuButton->setFlat(true);
            connect(menuButton, &QPushButton::clicked, this,
                    [this, i, menuButton] { menuForRow(i, menuButton); });
            table_->setCellWidget(i, 4, menuButton);
        }
        if (row >= 0 && row < static_cast<int>(steps_.size())) {
            table_->setCurrentCell(row, 0);
            table_->selectRow(row);
            showSelected(row);
        } else {
            table_->setCurrentCell(-1, -1);
            table_->clearSelection();
        }
        updateButtons();
        updateEditStatus();
        if (notify && onChange) onChange();
    }

void SequenceEditor::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // Column visibility is based on the final layout width after resizing.
    QTimer::singleShot(0, this, [this] { updateCompactTable(); });
}

void SequenceEditor::updateCompactTable() {
    if (!table_) return;
    const bool compact = table_->width() < 700;
    if (table_->isColumnHidden(2) != compact) table_->setColumnHidden(2, compact);
    if (table_->isColumnHidden(3) != compact) table_->setColumnHidden(3, compact);
}
