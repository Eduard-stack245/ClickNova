// SPDX-License-Identifier: Apache-2.0
#include "recording_review_dialog.hpp"
#include "ui_icons.hpp"
#include "clicknova/recording_review.hpp"
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QSpinBox>
#include <QLineEdit>
#include <QRegularExpressionValidator>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <type_traits>
#include <variant>
#include <utility>

using clicknova::input::Action;
using clicknova::input::Button;
using clicknova::input::Axis;
using clicknova::input::MoveMode;

RecordingReviewDialog::RecordingReviewDialog(std::vector<Action> actions,
        std::size_t capturedEvents, std::uint64_t recordedMs, bool russian, QWidget* parent, std::vector<clicknova::recording::WindowInfo> windows)
    : QDialog(parent), actions_(std::move(actions)), windows_(std::move(windows)),
      capturedEvents_(capturedEvents), recordedMs_(recordedMs), russian_(russian) {
    setObjectName(QStringLiteral("recordingReviewDialog"));
    setWindowTitle(text("Review recorded actions", "Просмотр записанных действий"));
    setMinimumSize(575, 430);
    resize(860, 600);
    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(text(
        "Review before importing. Edit, remove or reorder steps here; after importing you can keep editing in Sequence.",
        "Проверьте запись перед импортом. Здесь можно изменить, удалить и переставить действия; после импорта редактирование продолжится в разделе «Шаги»."));
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    stats_ = new QLabel;
    stats_->setObjectName(QStringLiteral("reviewSummary"));
    stats_->setWordWrap(true);
    layout->addWidget(stats_);
    table_ = new QTableWidget(0, 3);
    table_->setObjectName(QStringLiteral("recordingReviewActions"));
    table_->setHorizontalHeaderLabels({text("Action", "Действие"), text("Details", "Параметры"), text("Window / application", "Окно / программа")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    table_->setColumnWidth(2, 215);
    table_->setMinimumHeight(180);
    layout->addWidget(table_, 1);
    auto* tools = new QHBoxLayout;
    edit_ = new QPushButton(text("Edit selected", "Изменить выбранное"));
    edit_->setObjectName(QStringLiteral("reviewEdit"));
    edit_->setProperty("actionRole", QStringLiteral("primary"));
    edit_->setToolTip(text("Edit the selected action before importing.", "Изменить выбранное действие до импорта."));
    remove_ = new QPushButton(text("Remove", "Удалить"));
    remove_->setObjectName(QStringLiteral("reviewRemove"));
    remove_->setProperty("actionRole", QStringLiteral("danger"));
    remove_->setToolTip(text("Remove selected action; paired press and release stay consistent.",
        "Удалить действие. Связанные нажатие и отпускание остаются согласованными."));
    up_ = new QPushButton(text("Move up", "Выше"));
    up_->setObjectName(QStringLiteral("reviewUp"));
    down_ = new QPushButton(text("Move down", "Ниже"));
    down_->setObjectName(QStringLiteral("reviewDown"));
    clicknova::gui::setButtonIcon(edit_, clicknova::gui::Icon::Edit, QColor(QStringLiteral("#ffffff")));
    clicknova::gui::setButtonIcon(remove_, clicknova::gui::Icon::Delete, QColor(QStringLiteral("#ffcbd2")));
    clicknova::gui::setButtonIcon(up_, clicknova::gui::Icon::Up);
    clicknova::gui::setButtonIcon(down_, clicknova::gui::Icon::Down);
    for (auto* button : {edit_, remove_, up_, down_}) tools->addWidget(button);
    layout->addLayout(tools);
    feedback_ = new QLabel;
    feedback_->setObjectName(QStringLiteral("reviewFeedback"));
    feedback_->setWordWrap(true);
    layout->addWidget(feedback_);
    save_ = new QCheckBox(text("Also save as a named scenario after importing (optional)",
                               "Также сохранить под именем после импорта (необязательно)"));
    save_->setObjectName(QStringLiteral("reviewSaveOptional"));
    save_->setChecked(false);
    save_->setToolTip(text("Unchecked: no separate named scenario is created. The current editor still autosaves its draft locally.",
                               "Если выключено, именованный сценарий не создаётся. Черновик текущего редактора всё равно автосохраняется локально."));
    layout->addWidget(save_);
    auto* buttons = new QHBoxLayout;
    replace_ = new QPushButton(text("Replace current actions", "Заменить текущие действия"));
    replace_->setObjectName(QStringLiteral("reviewReplace"));
    replace_->setProperty("actionRole", QStringLiteral("primary"));
    replace_->setToolTip(text("Replace the current actions with this recording; the editor autosaves its draft.",
        "Заменить действия редактора текущей записью; черновик сохраняется автоматически."));
    append_ = new QPushButton(text("Append to current actions", "Добавить в конец"));
    append_->setObjectName(QStringLiteral("reviewAppend"));
    append_->setProperty("actionRole", QStringLiteral("success"));
    append_->setToolTip(text("Append recorded actions to the current plan.",
        "Добавить запись после существующих действий."));
    auto* cancel = new QPushButton(text("Cancel (keep unchanged)", "Отмена (ничего не менять)"));
    cancel->setObjectName(QStringLiteral("reviewCancel"));
    cancel->setProperty("actionRole", QStringLiteral("secondary"));
    cancel->setToolTip(text("Close the review without changing any actions.",
        "Закрыть просмотр без изменения сценария."));
    clicknova::gui::setButtonIcon(replace_, clicknova::gui::Icon::Check, QColor(QStringLiteral("#ffffff")));
    clicknova::gui::setButtonIcon(append_, clicknova::gui::Icon::List, QColor(QStringLiteral("#d8fff1")));
    clicknova::gui::setButtonIcon(cancel, clicknova::gui::Icon::Close);
    for (auto* button : {replace_, append_, cancel}) {
        button->setMinimumHeight(36);
        buttons->addWidget(button);
    }
    layout->addLayout(buttons);
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] { refresh(table_->currentRow()); });
    connect(edit_, &QPushButton::clicked, this, [this] { editSelected(); });
    connect(remove_, &QPushButton::clicked, this, [this] { removeSelected(); });
    connect(up_, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(down_, &QPushButton::clicked, this, [this] { moveSelected(1); });
    connect(replace_, &QPushButton::clicked, this, [this] { import(Decision::Replace); });
    connect(append_, &QPushButton::clicked, this, [this] { import(Decision::Append); });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    refresh(actions_.empty() ? -1 : 0);
}

QString RecordingReviewDialog::text(const char* english, const char* russian) const {
    return russian_ ? QString::fromUtf8(russian) : QString::fromLatin1(english);
}

bool RecordingReviewDialog::saveRequested() const { return save_->isChecked(); }

QString RecordingReviewDialog::actionName(const Action& action) const {
    return std::visit([this](const auto& a) -> QString {
        using T = std::decay_t<decltype(a)>;
        if constexpr (std::is_same_v<T, clicknova::input::Click>) return text("Click", "Клик");
        else if constexpr (std::is_same_v<T, clicknova::input::ButtonDown>) return text("Mouse down", "Зажать мышь");
        else if constexpr (std::is_same_v<T, clicknova::input::ButtonUp>) return text("Mouse up", "Отпустить мышь");
        else if constexpr (std::is_same_v<T, clicknova::input::MouseHold>) return text("Timed mouse hold", "Удержание мыши");
        else if constexpr (std::is_same_v<T, clicknova::input::Wheel>) return text("Wheel", "Колесо");
        else if constexpr (std::is_same_v<T, clicknova::input::KeyTap>) return text("Key press", "Нажать клавишу");
        else if constexpr (std::is_same_v<T, clicknova::input::KeyDown>) return text("Key down", "Зажать клавишу");
        else if constexpr (std::is_same_v<T, clicknova::input::KeyUp>) return text("Key up", "Отпустить клавишу");
        else if constexpr (std::is_same_v<T, clicknova::input::KeyHold>) return text("Timed key hold", "Удержание клавиши");
        else if constexpr (std::is_same_v<T, clicknova::input::TypeText>) return text("Type text", "Ввести текст");
        else if constexpr (std::is_same_v<T, clicknova::input::Wait>) return text("Wait", "Ожидание");
        else if constexpr (std::is_same_v<T, clicknova::input::MovePointer>) return text("Move cursor", "Переместить курсор");
        else return QString();
    }, action);
}

QString RecordingReviewDialog::actionDetails(const Action& action) const {
    return std::visit([this](const auto& a) -> QString {
        using T = std::decay_t<decltype(a)>;
        if constexpr (std::is_same_v<T, clicknova::input::Click>)
            return QStringLiteral("%1 × %2 · %3 ms").arg(static_cast<int>(a.button)).arg(a.count).arg(a.intervalMs);
        else if constexpr (std::is_same_v<T, clicknova::input::ButtonDown> || std::is_same_v<T, clicknova::input::ButtonUp>)
            return QStringLiteral("%1").arg(static_cast<int>(a.button));
        else if constexpr (std::is_same_v<T, clicknova::input::MouseHold>)
            return QStringLiteral("%1 · %2 ms").arg(static_cast<int>(a.button)).arg(a.durationMs);
        else if constexpr (std::is_same_v<T, clicknova::input::Wheel>)
            return QStringLiteral("%1 · %2").arg(a.axis == Axis::Vertical ? text("Vertical", "Вертикально") : text("Horizontal", "Горизонтально")).arg(a.delta);
        else if constexpr (std::is_same_v<T, clicknova::input::KeyTap> || std::is_same_v<T, clicknova::input::KeyDown> || std::is_same_v<T, clicknova::input::KeyUp>) {
            if (a.virtualKey >= 0x41 && a.virtualKey <= 0x5A)
                return QString(QChar(ushort(a.virtualKey)));
            if (a.virtualKey >= 0x30 && a.virtualKey <= 0x39)
                return QString(QChar(ushort(a.virtualKey)));
            const QStringList names = {QStringLiteral("Backspace"), QStringLiteral("Tab"),
                QStringLiteral("Enter"), QStringLiteral("Shift"), QStringLiteral("Ctrl"),
                QStringLiteral("Alt"), QStringLiteral("Esc"), QStringLiteral("Space")};
            const int codes[] = {8, 9, 13, 16, 17, 18, 27, 32};
            for (int i = 0; i < 8; ++i) if (a.virtualKey == codes[i]) return names[i];
            return QStringLiteral("VK %1").arg(a.virtualKey);
        } else if constexpr (std::is_same_v<T, clicknova::input::KeyHold>)
            return QStringLiteral("VK %1 · %2 ms").arg(a.virtualKey).arg(a.durationMs);
        else if constexpr (std::is_same_v<T, clicknova::input::TypeText>) {
            QString preview = QString::fromStdU16String(a.text).left(80).replace(QLatin1Char('\n'), QStringLiteral("↵"));
            if (a.text.size() > 80) preview += QStringLiteral("…");
            return QStringLiteral("\"%1\" · %2 %3").arg(preview).arg(static_cast<qulonglong>(a.text.size()))
                .arg(text("characters", "символов"));
        }
        else if constexpr (std::is_same_v<T, clicknova::input::MovePointer>)
            return QStringLiteral("X:%1 Y:%2 · %3 ms · %4").arg(a.x).arg(a.y).arg(a.durationMs)
                .arg(a.mode == MoveMode::Absolute ? text("Screen", "Экран") : text("Relative", "Относительно"));
        else if constexpr (std::is_same_v<T, clicknova::input::Wait>)
            return QStringLiteral("%1 ms").arg(a.durationMs);
        else return QString();
    }, action);
}

void RecordingReviewDialog::refresh(int selection) {
    const QSignalBlocker blocker(table_);
    table_->setRowCount(static_cast<int>(actions_.size()));
    for (int i = 0; i < static_cast<int>(actions_.size()); ++i) {
        const auto& a = actions_[static_cast<std::size_t>(i)];
        auto* title = new QTableWidgetItem(QStringLiteral("%1. %2").arg(i + 1).arg(actionName(a)));
        clicknova::gui::Icon pictogram = clicknova::gui::Icon::Click;
        if (std::holds_alternative<clicknova::input::KeyTap>(a) ||
            std::holds_alternative<clicknova::input::KeyDown>(a) ||
            std::holds_alternative<clicknova::input::KeyUp>(a) ||
            std::holds_alternative<clicknova::input::KeyHold>(a))
            pictogram = clicknova::gui::Icon::Keyboard;
        else if (std::holds_alternative<clicknova::input::TypeText>(a))
            pictogram = clicknova::gui::Icon::Text;
        else if (std::holds_alternative<clicknova::input::Wait>(a))
            pictogram = clicknova::gui::Icon::Wait;
        else if (std::holds_alternative<clicknova::input::MovePointer>(a))
            pictogram = clicknova::gui::Icon::Cursor;
        else if (std::holds_alternative<clicknova::input::Wheel>(a))
            pictogram = clicknova::gui::Icon::Mouse;
        title->setIcon(clicknova::gui::icon(pictogram));
        table_->setItem(i, 0, title);
        table_->setItem(i, 1, new QTableWidgetItem(actionDetails(a)));
        QString label = text("Unknown window", "Окно не определено");
        if (static_cast<std::size_t>(i) < windows_.size()) {
            const auto& origin = windows_[static_cast<std::size_t>(i)];
            const QString windowTitle = QString::fromStdU16String(origin.title);
            const QString process = QString::fromStdU16String(origin.process);
            if (!windowTitle.isEmpty() || !process.isEmpty())
                label = !windowTitle.isEmpty() ? windowTitle : process;
            if (!windowTitle.isEmpty() && !process.isEmpty()) label += QStringLiteral(" (%1)").arg(process);
        }
        auto* originItem = new QTableWidgetItem(label);
        originItem->setToolTip(label);
        table_->setItem(i, 2, originItem);
    }
    if (selection >= 0 && selection < table_->rowCount()) table_->selectRow(selection);
    else table_->clearSelection();
    const bool selected = table_->currentRow() >= 0 && table_->currentRow() < table_->rowCount();
    edit_->setEnabled(selected);
    remove_->setEnabled(selected);
    up_->setEnabled(selected && table_->currentRow() > 0);
    down_->setEnabled(selected && table_->currentRow() + 1 < table_->rowCount());
    replace_->setEnabled(!actions_.empty());
    append_->setEnabled(!actions_.empty());
    const auto sum = clicknova::recording::review::summarize(actions_);
    const auto duration = recordedMs_;
    stats_->setText(text("Recorded time:", "Время записи:") + QStringLiteral(" %1 s · ").arg(duration / 1000.0, 0, 'f', 1) +
        text("Raw events:", "Исходных событий:") + QStringLiteral(" %1 · ").arg(static_cast<qulonglong>(capturedEvents_)) +
        text("Actions kept:", "Оставлено действий:") + QStringLiteral(" %1 · ").arg(static_cast<qulonglong>(sum.actions)) +
        text("Mouse:", "Мышь:") + QStringLiteral(" %1 · ").arg(static_cast<qulonglong>(sum.mousePresses)) +
        text("Keys:", "Клавиши:") + QStringLiteral(" %1 · ").arg(static_cast<qulonglong>(sum.keyPresses)) +
        text("Moves:", "Перемещений:") + QStringLiteral(" %1 · ").arg(static_cast<qulonglong>(sum.moves)) +
        text("Wheel:", "Колесо:") + QStringLiteral(" %1").arg(static_cast<qulonglong>(sum.wheelActions)));
}

void RecordingReviewDialog::removeSelected() {
    const int row = table_->currentRow();
    if (row < 0) return;
    const std::size_t originalSize = actions_.size();
    std::vector<std::size_t> removed;
    if (!clicknova::recording::review::eraseWithPair(actions_, static_cast<std::size_t>(row), &removed)) {
        feedback_->setText(text("Cannot remove an action while preserving a valid sequence.",
                                "Нельзя удалить действие, сохранив корректность сценария."));
        return;
    }
    // A key/button pair may be nonadjacent. Remove precisely the same original
    // indexes in reverse order so the other recorded window labels survive.
    if (windows_.size() == originalSize) {
        for (auto it = removed.rbegin(); it != removed.rend(); ++it)
            windows_.erase(windows_.begin() + static_cast<std::ptrdiff_t>(*it));
    } else {
        windows_.clear(); // Do not show misaligned annotations from old data.
    }
    feedback_->setText(text("Action removed. Paired releases/presses are removed together.",
                            "Действие удалено. Связанные зажатия и отпускания удаляются вместе."));
    refresh(std::min(row, static_cast<int>(actions_.size()) - 1));
}

void RecordingReviewDialog::moveSelected(int distance) {
    const int row = table_->currentRow();
    if (row < 0) return;
    const int target = row + distance;
    if (target < 0 || target >= static_cast<int>(actions_.size())) return;
    if (!clicknova::recording::review::moveValidated(actions_, static_cast<std::size_t>(row), static_cast<std::size_t>(target))) {
        feedback_->setText(text("Cannot move a release before its press, or break a held-input sequence.",
                                "Нельзя переместить отпускание до зажатия или нарушить порядок удержания."));
        return;
    }
    if (windows_.size() == actions_.size()) {
        std::swap(windows_[static_cast<std::size_t>(row)], windows_[static_cast<std::size_t>(target)]);
    }
    feedback_->clear();
    refresh(target);
}

void RecordingReviewDialog::import(Decision decision) {
    const auto problem = clicknova::input::validate({actions_, 1, 0});
    if (!problem.empty()) {
        QMessageBox::warning(this, QStringLiteral("ClickNova"),
            text("Cannot import the edited recording: ", "Нельзя импортировать изменённую запись: ") +
            QString::fromStdString(problem));
        return;
    }
    decision_ = decision;
    accept();
}

void RecordingReviewDialog::editSelected() {
    const int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(actions_.size())) return;
    Action replacement = actions_[static_cast<std::size_t>(row)];
    QDialog dialog(this);
    dialog.setWindowTitle(text("Edit recorded action", "Редактировать записанное действие"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addLayout(form);
    std::vector<std::function<void()>> apply;
    auto spin = [&](const QString& label, int lower, int upper, int value, bool milliseconds = false) {
        auto* control = new QSpinBox(&dialog);
        control->setRange(lower, upper);
        control->setValue(value);
        if (milliseconds) control->setSuffix(QStringLiteral(" ms"));
        form->addRow(label, control);
        return control;
    };
    auto wide = [&](const QString& label, std::int64_t value) {
        auto* control = new QLineEdit(QString::number(value), &dialog);
        control->setValidator(new QRegularExpressionValidator(
            QRegularExpression(QStringLiteral("[0-9]{0,19}")), control));
        form->addRow(label, control);
        return control;
    };
    auto choice = [&](const QString& label, const QStringList& options, int value) {
        auto* control = new QComboBox(&dialog);
        control->addItems(options);
        control->setCurrentIndex(value);
        form->addRow(label, control);
        return control;
    };
    const QString buttonLabel = text("Mouse button", "Кнопка мыши");
    const QStringList buttons = {text("Left", "Левая"), text("Right", "Правая"),
            text("Middle", "Средняя"), QStringLiteral("X1"), QStringLiteral("X2")};
    std::visit([&](const auto& a) {
        using T = std::decay_t<decltype(a)>;
        if constexpr (std::is_same_v<T, clicknova::input::Click> || std::is_same_v<T, clicknova::input::ButtonDown> ||
                      std::is_same_v<T, clicknova::input::ButtonUp> || std::is_same_v<T, clicknova::input::MouseHold>) {
            auto* button = choice(buttonLabel, buttons, static_cast<int>(a.button));
            apply.push_back([&, button] { std::get<T>(replacement).button = static_cast<Button>(button->currentIndex()); });
        }
        if constexpr (std::is_same_v<T, clicknova::input::Click>) {
            auto* count = wide(text("Clicks", "Количество кликов"), a.count);
            auto* interval = spin(text("Between clicks", "Между кликами"), 0, 60000, a.intervalMs, true);
            apply.push_back([&, count, interval] {
                bool ok = false;
                const auto n = count->text().toLongLong(&ok);
                auto& v = std::get<T>(replacement);
                v.count = ok ? n : -1;
                v.intervalMs = interval->value();
            });
        } else if constexpr (std::is_same_v<T, clicknova::input::Wait>) {
            auto* duration = wide(text("Duration (ms)", "Длительность (мс)"), a.durationMs);
            apply.push_back([&, duration] {
                bool ok = false;
                const auto ms = duration->text().toLongLong(&ok);
                std::get<T>(replacement).durationMs = ok ? ms : -1;
            });
        } else if constexpr (std::is_same_v<T, clicknova::input::MouseHold> || std::is_same_v<T, clicknova::input::KeyHold>) {
            auto* duration = spin(text("Duration", "Длительность"), 1,
                                  60000, a.durationMs, true);
            apply.push_back([&, duration] { std::get<T>(replacement).durationMs = duration->value(); });
        }
        if constexpr (std::is_same_v<T, clicknova::input::KeyTap> || std::is_same_v<T, clicknova::input::KeyDown> ||
                      std::is_same_v<T, clicknova::input::KeyUp> || std::is_same_v<T, clicknova::input::KeyHold>) {
            auto* key = spin(text("Windows key code", "Код клавиши Windows"), 8, 254, a.virtualKey);
            apply.push_back([&, key] { std::get<T>(replacement).virtualKey = key->value(); });
        }
        if constexpr (std::is_same_v<T, clicknova::input::Wheel>) {
            auto* axis = choice(text("Direction", "Направление"),
                {text("Vertical", "Вертикально"), text("Horizontal", "Горизонтально")}, static_cast<int>(a.axis));
            auto* delta = spin(text("Amount", "Величина"), -1200, 1200, a.delta);
            apply.push_back([&, axis, delta] { auto& v = std::get<T>(replacement); v.axis = static_cast<Axis>(axis->currentIndex()); v.delta = delta->value(); });
        }
        if constexpr (std::is_same_v<T, clicknova::input::MovePointer>) {
            auto* mode = choice(text("Position mode", "Режим координат"),
                {text("Relative", "Относительно"), text("Screen", "Экран")}, static_cast<int>(a.mode));
            auto* x = spin(QStringLiteral("X"), -32768, 32767, a.x);
            auto* y = spin(QStringLiteral("Y"), -32768, 32767, a.y);
            auto* duration = spin(text("Movement duration", "Время движения"), 0, 60000, a.durationMs, true);
            apply.push_back([&, mode, x, y, duration] {
                auto& v = std::get<T>(replacement);
                v.mode = static_cast<MoveMode>(mode->currentIndex()); v.x = x->value(); v.y = y->value();
                v.durationMs = duration->value();
            });
        }
        if constexpr (std::is_same_v<T, clicknova::input::TypeText>) {
            auto* textBox = new QPlainTextEdit(&dialog);
            textBox->setPlainText(QString::fromStdU16String(a.text));
            textBox->setToolTip(text("Do not store passwords or other secrets in scenarios.",
                                     "Не сохраняйте пароли или секреты в сценариях."));
            form->addRow(text("Text (not recommended for secrets)", "Текст (не для секретов)"), textBox);
            apply.push_back([&, textBox] { std::get<T>(replacement).text = textBox->toPlainText().toStdU16String(); });
        }
    }, replacement);
    auto* buttonsBar = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttonsBar);
    connect(buttonsBar, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonsBar, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    for (const auto& commit : apply) commit();
    if (!clicknova::recording::review::replaceValidated(actions_, static_cast<std::size_t>(row), std::move(replacement))) {
        QMessageBox::warning(this, QStringLiteral("ClickNova"),
            text("This edit would create an invalid sequence. The action was not changed.",
                 "Изменение сделает сценарий некорректным. Действие не изменено."));
        return;
    }
    feedback_->clear();
    refresh(row);
}
