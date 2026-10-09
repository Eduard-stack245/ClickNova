// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "clicknova/sequence.hpp"
#include "ui_translation.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <utility>
#include <vector>

// Editing UI only; validation and execution remain in the platform-neutral core.
class SequenceEditor final : public QWidget {
public:
    explicit SequenceEditor(QWidget* parent = nullptr) : QWidget(parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(9);

        hint_ = new QLabel(this);
        hint_->setWordWrap(true);
        layout->addWidget(hint_);

        auto* addRow = new QHBoxLayout;
        add_ = new QPushButton(this);
        add_->setObjectName(QStringLiteral("sequenceAddPoint"));
        capture_ = new QPushButton(this);
        count_ = new QLabel(this);
        count_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        addRow->addWidget(add_);
        addRow->addWidget(capture_);
        addRow->addStretch(1);
        addRow->addWidget(count_);
        layout->addLayout(addRow);

        table_ = new QTableWidget(0, 4, this);
        table_->setObjectName(QStringLiteral("sequenceSteps"));
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        table_->setAlternatingRowColors(true);
        table_->setMinimumHeight(175);
        table_->verticalHeader()->setVisible(false);
        auto* header = table_->horizontalHeader();
        header->setSectionResizeMode(0, QHeaderView::Fixed);
        header->resizeSection(0, 55);
        header->setSectionResizeMode(1, QHeaderView::Stretch);
        header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        layout->addWidget(table_, 1);
        connect(table_, &QTableWidget::currentCellChanged, this,
                [this](int row, int, int, int) { showSelected(row); updateButtons(); });

        auto* detailGroup = new QGroupBox(this);
        detailGroup_ = detailGroup;
        auto* detailLayout = new QVBoxLayout(detailGroup);
        detailLayout->setContentsMargins(12, 19, 12, 10);
        detailLayout->setSpacing(8);
        selection_ = new QLabel(this);
        detailLayout->addWidget(selection_);
        auto* fields = new QGridLayout;
        fields->setHorizontalSpacing(12);
        fields->setVerticalSpacing(5);
        xLabel_ = new QLabel(this);
        yLabel_ = new QLabel(this);
        buttonLabel_ = new QLabel(this);
        delayLabel_ = new QLabel(this);
        x_ = new QSpinBox(this);
        y_ = new QSpinBox(this);
        x_->setObjectName(QStringLiteral("sequenceCoordinateX"));
        y_->setObjectName(QStringLiteral("sequenceCoordinateY"));
        button_ = new QComboBox(this);
        delay_ = new QSpinBox(this);
        x_->setRange(-131072, 131072);
        y_->setRange(-131072, 131072);
        button_->addItems({QStringLiteral("Left"), QStringLiteral("Right"), QStringLiteral("Middle")});
        delay_->setRange(10, 60000);
        delay_->setValue(200);
        delay_->setSuffix(QStringLiteral(" ms"));
        for (auto* spin : {x_, y_, delay_}) spin->setMinimumHeight(44);
        button_->setMinimumHeight(44);
        fields->addWidget(xLabel_, 0, 0);
        fields->addWidget(yLabel_, 0, 1);
        fields->addWidget(x_, 1, 0);
        fields->addWidget(y_, 1, 1);
        fields->addWidget(buttonLabel_, 2, 0);
        fields->addWidget(delayLabel_, 2, 1);
        fields->addWidget(button_, 3, 0);
        fields->addWidget(delay_, 3, 1);
        detailLayout->addLayout(fields);
        update_ = new QPushButton(this);
        update_->setObjectName(QStringLiteral("sequenceUpdateSelected"));
        detailLayout->addWidget(update_);
        layout->addWidget(detailGroup);

        auto* manageRow = new QHBoxLayout;
        up_ = new QPushButton(this);
        down_ = new QPushButton(this);
        remove_ = new QPushButton(this);
        clear_ = new QPushButton(this);
        manageRow->addWidget(up_);
        manageRow->addWidget(down_);
        manageRow->addWidget(remove_);
        manageRow->addStretch(1);
        manageRow->addWidget(clear_);
        layout->addLayout(manageRow);

        note_ = new QLabel(this);
        note_->setWordWrap(true);
        layout->addWidget(note_);

        connect(add_, &QPushButton::clicked, this, [this] {
            if (!locked_) append({x_->value(), y_->value(), button_->currentIndex(), delay_->value(), space_()});
        });
        connect(update_, &QPushButton::clicked, this, [this] {
            const int row = table_->currentRow();
            if (locked_ || row < 0 || row >= static_cast<int>(steps_.size())) return;
            auto replacement = steps_;
            replacement[static_cast<std::size_t>(row)] =
                {x_->value(), y_->value(), button_->currentIndex(), delay_->value(), space_()};
            if (!canUse(replacement)) return;
            steps_ = std::move(replacement);
            refresh(row);
        });
        connect(capture_, &QPushButton::clicked, this, [this] {
            if (!locked_ && onCapture) onCapture();
        });
        connect(remove_, &QPushButton::clicked, this, [this] {
            const int row = table_->currentRow();
            if (locked_ || row < 0 || row >= static_cast<int>(steps_.size())) return;
            steps_.erase(steps_.begin() + row);
            refresh(std::min(row, static_cast<int>(steps_.size()) - 1));
        });
        connect(up_, &QPushButton::clicked, this, [this] { move(-1); });
        connect(down_, &QPushButton::clicked, this, [this] { move(1); });
        connect(clear_, &QPushButton::clicked, this, [this] {
            if (locked_ || steps_.empty()) return;
            if (QMessageBox::question(this, label(QStringLiteral("Clear sequence")),
                    label(QStringLiteral("Remove all sequence steps?"))) != QMessageBox::Yes) return;
            steps_.clear();
            refresh(-1);
        });
        setLanguage(QStringLiteral("en"));
        refresh(-1);
    }

    std::function<void()> onCapture;
    std::function<void()> onChange;
    void setSpaceProvider(std::function<clicknova::sequence::Space()> provider) { space_ = std::move(provider); }
    const std::vector<clicknova::sequence::Step>& steps() const { return steps_; }
    void setSteps(const std::vector<clicknova::sequence::Step>& steps) {
        if (locked_) return;
        clicknova::sequence::Plan plan{steps, 1, false};
        if (!steps.empty() && !clicknova::sequence::validate(plan).empty()) return;
        steps_ = steps;
        refresh(steps_.empty() ? -1 : 0);
    }
    void setInitialPoint(int x, int y, int button, int delay) {
        if (locked_) return;
        x_->setValue(x);
        y_->setValue(y);
        button_->setCurrentIndex(button);
        delay_->setValue(delay);
    }
    void addCapturedPoint(int x, int y, clicknova::sequence::Space space) {
        if (!locked_) append({x, y, button_->currentIndex(), delay_->value(), space});
    }
    void setLocked(bool locked) {
        locked_ = locked;
        const std::initializer_list<QWidget*> controls = {x_, y_, delay_, button_, add_, capture_};
        for (QWidget* w : controls) w->setEnabled(!locked);
        table_->setEnabled(!locked);
        updateButtons();
    }
    void setLanguage(const QString& language) {
        language_ = language;
        hint_->setText(label(QStringLiteral("Add a point manually or hover over a target and capture it in 3 seconds.")));
        detailGroup_->setTitle(label(QStringLiteral("Step details")));
        note_->setText(label(QStringLiteral("Sequence steps use screen coordinates or target-window client coordinates. Switch target mode only with compatible steps.")));
        add_->setText(label(QStringLiteral("Add point")));
        capture_->setText(label(QStringLiteral("Capture point")));
        update_->setText(label(QStringLiteral("Save step changes")));
        up_->setText(label(QStringLiteral("Move up")));
        down_->setText(label(QStringLiteral("Move down")));
        remove_->setText(label(QStringLiteral("Remove")));
        clear_->setText(label(QStringLiteral("Clear all")));
        xLabel_->setText(label(QStringLiteral("X coordinate")));
        yLabel_->setText(label(QStringLiteral("Y coordinate")));
        buttonLabel_->setText(label(QStringLiteral("Mouse button")));
        delayLabel_->setText(label(QStringLiteral("Wait before click")));
        table_->setHorizontalHeaderLabels({label(QStringLiteral("Step")), label(QStringLiteral("Coordinates")),
                                           label(QStringLiteral("Button")), label(QStringLiteral("Delay"))});
        button_->setItemText(0, label(QStringLiteral("Left")));
        button_->setItemText(1, label(QStringLiteral("Right")));
        button_->setItemText(2, label(QStringLiteral("Middle")));
        refresh(table_->currentRow(), false);
    }
private:
    QString label(const QString& english) const { return translateUiText(english, language_); }
    bool canUse(const std::vector<clicknova::sequence::Step>& candidate) {
        clicknova::sequence::Plan plan{candidate, 1, false};
        const auto error = clicknova::sequence::validate(plan);
        if (error.empty()) return true;
        QMessageBox::warning(this, QStringLiteral("ClickNova"), label(QString::fromStdString(error)));
        return false;
    }
    void append(clicknova::sequence::Step step) {
        auto candidate = steps_;
        candidate.push_back(step);
        if (!canUse(candidate)) return;
        steps_ = std::move(candidate);
        refresh(static_cast<int>(steps_.size()) - 1);
        table_->scrollToBottom();
    }
    void move(int delta) {
        const int row = table_->currentRow();
        const int to = row + delta;
        if (locked_ || row < 0 || to < 0 || to >= static_cast<int>(steps_.size())) return;
        std::swap(steps_[static_cast<std::size_t>(row)], steps_[static_cast<std::size_t>(to)]);
        refresh(to);
    }
    void showSelected(int row) {
        if (row >= 0 && row < static_cast<int>(steps_.size())) {
            const auto& step = steps_[static_cast<std::size_t>(row)];
            x_->setValue(step.x);
            y_->setValue(step.y);
            button_->setCurrentIndex(step.button);
            delay_->setValue(step.delayMs);
        }
        updateButtons();
    }
    void updateButtons() {
        const int row = table_->currentRow();
        const bool selected = row >= 0 && row < static_cast<int>(steps_.size());
        update_->setEnabled(!locked_ && selected);
        remove_->setEnabled(!locked_ && selected);
        up_->setEnabled(!locked_ && selected && row > 0);
        down_->setEnabled(!locked_ && selected && row + 1 < static_cast<int>(steps_.size()));
        clear_->setEnabled(!locked_ && !steps_.empty());
        count_->setText(label(QStringLiteral("%1 steps")).arg(static_cast<int>(steps_.size())));
        selection_->setText(selected ? label(QStringLiteral("Selected: step %1")).arg(row + 1)
                                    : label(QStringLiteral("No step selected")));
    }
    void refresh(int selectedRow, bool notify = true) {
        const QSignalBlocker block(table_);
        table_->setRowCount(static_cast<int>(steps_.size()));
        for (int i = 0; i < static_cast<int>(steps_.size()); ++i) {
            const auto& step = steps_[static_cast<std::size_t>(i)];
            const QString position = QStringLiteral("(%1, %2) · %3").arg(step.x).arg(step.y).arg(
                label(step.space == clicknova::sequence::Space::Client ?
                      QStringLiteral("Client") : QStringLiteral("Screen")));
            const QStringList cells = {QString::number(i + 1), position,
                button_->itemText(step.button), QStringLiteral("%1 ms").arg(step.delayMs)};
            for (int col = 0; col < cells.size(); ++col)
                table_->setItem(i, col, new QTableWidgetItem(cells[col]));
        }
        if (selectedRow >= 0 && selectedRow < static_cast<int>(steps_.size())) {
            table_->selectRow(selectedRow);
            showSelected(selectedRow);
        } else {
            table_->clearSelection();
        }
        updateButtons();
        if (notify && onChange) onChange();
    }
    QString language_{QStringLiteral("en")};
    std::vector<clicknova::sequence::Step> steps_;
    std::function<clicknova::sequence::Space()> space_{[] { return clicknova::sequence::Space::Screen; }};
    bool locked_{false};
    QLabel *hint_{}, *note_{}, *selection_{}, *count_{};
    QLabel *xLabel_{}, *yLabel_{}, *buttonLabel_{}, *delayLabel_{};
    QGroupBox* detailGroup_{};
    QTableWidget* table_{};
    QSpinBox *x_{}, *y_{}, *delay_{};
    QComboBox* button_{};
    QPushButton *add_{}, *update_{}, *capture_{}, *remove_{}, *up_{}, *down_{}, *clear_{};
};
