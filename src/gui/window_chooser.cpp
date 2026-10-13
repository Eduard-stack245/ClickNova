// SPDX-License-Identifier: Apache-2.0
#include "window_chooser.hpp"
#include "ui_icons.hpp"
#include "ui_translation.hpp"

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QString>

// The target chooser never activates or clicks the selected window.
std::optional<clicknova::platform::WindowTarget> chooseTargetWindow(
    QWidget* parent, const QString& language,
    std::function<std::vector<clicknova::platform::WindowTarget>()> provider) {
    const auto tr = [&language](const QString& source) { return translateUiText(source, language); };
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("chooseWindowDialog"));
    dialog.setWindowTitle(tr(QStringLiteral("Select target window")));
    dialog.setMinimumSize(580, 450);
    auto* column = new QVBoxLayout(&dialog);
    auto* info = new QLabel(tr(QStringLiteral("Choose an application from the list. No countdown or mouse capture is needed.")), &dialog);
    info->setWordWrap(true);
    column->addWidget(info);
    auto* search = new QLineEdit(&dialog);
    search->setObjectName(QStringLiteral("windowSearch"));
    search->setPlaceholderText(tr(QStringLiteral("Search by window name or PID...")));
    column->addWidget(search);
    auto* list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("windowSearchResults"));
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    column->addWidget(list, 1);
    auto* note = new QLabel(tr(QStringLiteral("The selected window must be foreground when automation runs. Background clicks are not supported.")), &dialog);
    note->setWordWrap(true);
    column->addWidget(note);
    auto* row = new QHBoxLayout;
    auto* refresh = new QPushButton(tr(QStringLiteral("Refresh windows")), &dialog);
    auto* cancel = new QPushButton(tr(QStringLiteral("Cancel")), &dialog);
    auto* confirm = new QPushButton(tr(QStringLiteral("Use selected window")), &dialog);
    confirm->setObjectName(QStringLiteral("confirmWindowSelection"));
    confirm->setProperty("actionRole", QStringLiteral("primary"));
    confirm->setToolTip(language == QStringLiteral("ru")
        ? QString::fromUtf8("Использовать выделенное окно как цель. Окно должно быть активным при выполнении.")
        : QStringLiteral("Use the selected window. It must be foreground during playback."));
    refresh->setProperty("actionRole", QStringLiteral("secondary"));
    cancel->setProperty("actionRole", QStringLiteral("secondary"));
    confirm->setDefault(true);
    confirm->setEnabled(false);
    clicknova::gui::setButtonIcon(refresh, clicknova::gui::Icon::Refresh);
    clicknova::gui::setButtonIcon(cancel, clicknova::gui::Icon::Close);
    clicknova::gui::setButtonIcon(confirm, clicknova::gui::Icon::Window, QColor(QStringLiteral("#ffffff")));
    row->addWidget(refresh);
    row->addStretch();
    row->addWidget(cancel);
    row->addWidget(confirm);
    column->addLayout(row);

    std::vector<clicknova::platform::WindowTarget> windows;
    std::optional<clicknova::platform::WindowTarget> chosen;
    const auto filter = [&]() {
        const QString query = search->text().trimmed();
        list->clear();
        for (int i = 0; i < static_cast<int>(windows.size()); ++i) {
            const auto& item = windows[static_cast<std::size_t>(i)];
            const QString display = QStringLiteral("%1 (%2, PID %3)")
                .arg(QString::fromStdWString(item.title).simplified())
                .arg(item.process.empty() ? QStringLiteral("Unknown application") :
                    QString::fromStdWString(item.process))
                .arg(item.processId);
            if (!query.isEmpty() && !display.contains(query, Qt::CaseInsensitive)) continue;
            auto* rowItem = new QListWidgetItem(display, list);
            rowItem->setData(Qt::UserRole, i);
            rowItem->setToolTip(display);
        }
        confirm->setEnabled(false);
        if (list->count() == 1) list->setCurrentRow(0);
    };
    const auto reload = [&]() {
        windows = provider();
        filter();
    };
    const auto acceptSelected = [&]() {
        if (!list->currentItem()) return;
        const int index = list->currentItem()->data(Qt::UserRole).toInt();
        if (index < 0 || index >= static_cast<int>(windows.size())) return;
        chosen = windows[static_cast<std::size_t>(index)];
        dialog.accept();
    };
    QObject::connect(search, &QLineEdit::textChanged, &dialog, [&](const QString&) { filter(); });
    QObject::connect(refresh, &QPushButton::clicked, &dialog, reload);
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(confirm, &QPushButton::clicked, &dialog, acceptSelected);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog,
                     [&](QListWidgetItem*) { acceptSelected(); });
    QObject::connect(list, &QListWidget::currentItemChanged, &dialog,
                     [&](QListWidgetItem* current, QListWidgetItem*) { confirm->setEnabled(current != nullptr); });
    reload();
    search->setFocus();
    if (dialog.exec() == QDialog::Accepted) return chosen;
    return std::nullopt;
}
