// SPDX-License-Identifier: Apache-2.0
#include "input_capture_dialog.hpp"
#include "ui_icons.hpp"

#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {
class CaptureDialog final : public QDialog {
public:
    CaptureDialog(QWidget* parent, bool russian, bool mouse)
        : QDialog(parent), mouseMode_(mouse) {
        setWindowTitle(russian ? QStringLiteral("Выбор кнопки устройством")
                               : QStringLiteral("Choose using the device"));
        setModal(true);
        resize(450, 230);
        auto* layout = new QVBoxLayout(this);
        auto* text = new QLabel(mouse
            ? (russian ? QStringLiteral("Нажмите нужную кнопку мыши ВНУТРИ области ниже. Работают левая, правая, средняя и X1/X2, если Windows их сообщает.")
                       : QStringLiteral("Press a mouse button INSIDE the area below. Left, right, middle and X1/X2 are accepted if Windows reports them."))
            : (russian ? QStringLiteral("Нажмите нужную клавишу, пока это окно активно. Esc отменяет выбор.")
                       : QStringLiteral("Press a key while this dialog is focused. Esc cancels.")));
        text->setWordWrap(true);
        layout->addWidget(text);
        target_ = new QFrame;
        target_->setFrameShape(QFrame::StyledPanel);
        target_->setMinimumHeight(100);
        target_->setFocusPolicy(Qt::StrongFocus);
        target_->setStyleSheet(QStringLiteral("QFrame {border: 2px dashed #64748b; border-radius:8px;}"));
        target_->installEventFilter(this);
        auto* caption = new QLabel(mouse ? (russian ? QStringLiteral("НАЖМИТЕ ЗДЕСЬ") : QStringLiteral("CLICK HERE"))
                                          : (russian ? QStringLiteral("НАЖМИТЕ КЛАВИШУ") : QStringLiteral("PRESS A KEY")), target_);
        caption->setAlignment(Qt::AlignCenter);
        auto* area = new QVBoxLayout(target_);
        area->addWidget(caption);
        caption->installEventFilter(this);
        layout->addWidget(target_);
        auto* cancel = new QPushButton(russian ? QStringLiteral("Отмена") : QStringLiteral("Cancel"));
        cancel->setProperty("actionRole", QStringLiteral("secondary"));
        cancel->setToolTip(russian ? QStringLiteral("Закрыть окно без выбора устройства или кнопки.")
                                   : QStringLiteral("Close without changing the selected button or key."));
        clicknova::gui::setButtonIcon(cancel, clicknova::gui::Icon::Close);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        layout->addWidget(cancel);
        QTimer::singleShot(0, target_, [this] { target_->setFocus(Qt::OtherFocusReason); });
    }
    std::optional<clicknova::input::Button> button() const { return button_; }
    std::optional<int> key() const { return key_; }

protected:
    bool eventFilter(QObject* source, QEvent* event) override {
        if (!mouseMode_ && source == target_ && event->type() == QEvent::KeyPress) {
            const auto* press = static_cast<QKeyEvent*>(event);
            if (press->key() == Qt::Key_Escape) { reject(); return true; }
#ifdef _WIN32
            const int vk = static_cast<int>(press->nativeVirtualKey());
            if (!press->isAutoRepeat() && vk >= 8 && vk <= 254) {
                key_ = vk;
                accept();
                return true;
            }
#endif
        }
        if (mouseMode_ && (source == target_ || source->parent() == target_) &&
            event->type() == QEvent::MouseButtonPress) {
            const auto* press = static_cast<QMouseEvent*>(event);
            switch (press->button()) {
            case Qt::LeftButton: button_ = clicknova::input::Button::Left; break;
            case Qt::RightButton: button_ = clicknova::input::Button::Right; break;
            case Qt::MiddleButton: button_ = clicknova::input::Button::Middle; break;
            case Qt::BackButton: button_ = clicknova::input::Button::X1; break;
            case Qt::ForwardButton: button_ = clicknova::input::Button::X2; break;
            default: break;
            }
            if (button_) { accept(); return true; }
        }
        return QDialog::eventFilter(source, event);
    }
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) { reject(); return; }
        if (!mouseMode_ && !event->isAutoRepeat()) {
#ifdef _WIN32
            const auto vk = static_cast<int>(event->nativeVirtualKey());
            if (vk >= 8 && vk <= 254) { key_ = vk; accept(); return; }
#endif
        }
        QDialog::keyPressEvent(event);
    }
private:
    bool mouseMode_;
    QFrame* target_{nullptr};
    std::optional<clicknova::input::Button> button_;
    std::optional<int> key_;
};
} // namespace

std::optional<clicknova::input::Button> chooseMouseButtonByPress(QWidget* parent, bool russian) {
    CaptureDialog dialog(parent, russian, true);
    return dialog.exec() == QDialog::Accepted ? dialog.button() : std::nullopt;
}
std::optional<int> chooseVirtualKeyByPress(QWidget* parent, bool russian) {
    CaptureDialog dialog(parent, russian, false);
    return dialog.exec() == QDialog::Accepted ? dialog.key() : std::nullopt;
}
