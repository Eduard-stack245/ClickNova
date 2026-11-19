// SPDX-License-Identifier: MIT
#pragma once
#include <QDialog>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QPen>
#include <QtMath>
#include <QTimer>
#include <QPainter>
#include <functional>
#include <QShortcut>
#include <QKeySequence>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QAbstractItemModel>
#include <QStyle>

// Reusable, read-only viewer for genuine full-resolution step images.
// Never enlarges a saved thumbnail as a substitute for original image data.
class StepImageViewer final : public QDialog {
public:
    explicit StepImageViewer(const QImage& image, const QString& caption,
                             QRect target = {}, QWidget* parent = nullptr)
        : QDialog(parent) {
        setObjectName(QStringLiteral("stepImageViewer"));
        setWindowTitle(caption);
        resize(960, 680);
        auto* layout = new QVBoxLayout(this);
        auto* bar = new QHBoxLayout;
        auto* smaller = new QPushButton(QStringLiteral("−"), this);
        auto* larger = new QPushButton(QStringLiteral("+"), this);
        auto* actual = new QPushButton(QStringLiteral("100%"), this);
        auto* fit = new QPushButton(QStringLiteral("Fit / Вписать"), this);
        smaller->setObjectName(QStringLiteral("stepImageZoomOut"));
        larger->setObjectName(QStringLiteral("stepImageZoomIn"));
        actual->setObjectName(QStringLiteral("stepImageActualSize"));
        fit->setObjectName(QStringLiteral("stepImageFit"));
        zoomLabel_ = new QLabel(this);
        zoomLabel_->setObjectName(QStringLiteral("stepImageZoomValue"));
        for (auto* button : {smaller, larger, actual, fit}) bar->addWidget(button);
        bar->addWidget(zoomLabel_);
        bar->addStretch();
        galleryPrevious_ = new QPushButton(QStringLiteral("◀"), this);
        galleryNext_ = new QPushButton(QStringLiteral("▶"), this);
        galleryPrevious_->setObjectName(QStringLiteral("stepImagePrevious"));
        galleryNext_->setObjectName(QStringLiteral("stepImageNext"));
        galleryPrevious_->setToolTip(QStringLiteral("Previous image / Предыдущий снимок (←)"));
        galleryNext_->setToolTip(QStringLiteral("Next image / Следующий снимок (→)"));
        galleryPrevious_->hide();
        galleryNext_->hide();
        bar->addWidget(galleryPrevious_);
        bar->addWidget(galleryNext_);
        galleryJump_ = new QSpinBox(this);
        galleryJump_->setObjectName(QStringLiteral("stepImageJump"));
        galleryJump_->setPrefix(QStringLiteral("#"));
        galleryJump_->setRange(1, 1);
        galleryJump_->setKeyboardTracking(false);
        galleryJump_->setMinimumWidth(95);
        galleryJump_->setToolTip(QStringLiteral(
            "Enter an action number and press Enter / Номер шага и Enter"));
        galleryJump_->hide();
        bar->addWidget(galleryJump_);
        layout->addLayout(bar);
        galleryDescription_ = new QLabel(this);
        galleryDescription_->setObjectName(QStringLiteral("stepImageDescription"));
        galleryDescription_->setTextFormat(Qt::PlainText); // Window labels are untrusted plain text.
        galleryDescription_->setWordWrap(true);
        galleryDescription_->hide();
        layout->addWidget(galleryDescription_);
        view_ = new ZoomView(this);
        view_->setObjectName(QStringLiteral("stepImageCanvas"));
        view_->setDragMode(QGraphicsView::ScrollHandDrag);
        view_->setRenderHint(QPainter::SmoothPixmapTransform);
        view_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        scene_ = new QGraphicsScene(this);
        view_->setScene(scene_);
        layout->addWidget(view_, 1);
        if (!image.isNull()) {
            imageItem_ = scene_->addPixmap(QPixmap::fromImage(image));
            scene_->setSceneRect(imageItem_->boundingRect());
            if (!target.isEmpty()) {
                const QRect valid = target.intersected(image.rect());
                if (!valid.isEmpty()) {
                    auto* marker = scene_->addRect(QRectF(valid), QPen(QColor(255, 69, 58), 3));
                    marker->setZValue(10);
                }
            }
        }
        connect(smaller, &QPushButton::clicked, this, [this] { zoom(1.0 / 1.25); });
        connect(larger, &QPushButton::clicked, this, [this] { zoom(1.25); });
        connect(actual, &QPushButton::clicked, this, [this] { setZoom(1.0); });
        connect(fit, &QPushButton::clicked, this, [this] { fitImage(); });
        view_->zoomRequested = [this](double factor) { zoom(factor); };
        connect(galleryPrevious_, &QPushButton::clicked, this,
                [this] { navigateGallery(-1); });
        connect(galleryNext_, &QPushButton::clicked, this,
                [this] { navigateGallery(1); });
        connect(galleryJump_, &QSpinBox::editingFinished, this, [this] {
            const int requested = galleryJump_->value() - 1;
            if (!jumpToGallery(requested) && galleryPosition_ >= 0) {
                const QSignalBlocker blocker(galleryJump_);
                galleryJump_->setValue(galleryPosition_ + 1);
                galleryDescription_->setText(QStringLiteral(
                    "No screenshot for step #%1 / Нет снимка для шага #%1")
                    .arg(requested + 1));
                galleryDescription_->show();
            }
        });
        auto* previousKey = new QShortcut(QKeySequence(Qt::Key_Left), this);
        auto* nextKey = new QShortcut(QKeySequence(Qt::Key_Right), this);
        previousKey->setContext(Qt::WidgetWithChildrenShortcut);
        nextKey->setContext(Qt::WidgetWithChildrenShortcut);
        connect(previousKey, &QShortcut::activated, this, [this] { navigateGallery(-1); });
        connect(nextKey, &QShortcut::activated, this, [this] { navigateGallery(1); });
        QTimer::singleShot(0, this, [this] { fitImage(); });
    }

    double zoomFactor() const { return scale_; }
    int galleryPosition() const { return galleryPosition_; }
    int galleryCount() const { return galleryCount_; }

    // Navigation is optional. The image loader is called only on demand and
    // returns an empty image for missing, invalid or unauthorized screenshots.
    // All positions are real action indexes; gaps are skipped, not renumbered.
    void enableGallery(int position, int count,
                       std::function<QImage(int)> loadOriginal,
                       QString caption = QStringLiteral("Step screenshots / Снимки шагов"),
                       std::function<QString(int)> describe = {}) {
        if (count < 1 || position < 0 || position >= count || !loadOriginal) return;
        galleryPosition_ = position;
        galleryCount_ = count;
        galleryLoad_ = std::move(loadOriginal);
        galleryCaption_ = std::move(caption);
        galleryDescribe_ = std::move(describe);
        galleryJump_->setRange(1, count);
        galleryJump_->setVisible(count > 1);
        galleryPrevious_->setVisible(count > 1);
        galleryNext_->setVisible(count > 1);
        updateGalleryControls();
    }

    // Jump by the REAL 0-based action index. Missing images never change
    // the displayed image or current gallery position.
    bool jumpToGallery(int index) {
        if (!galleryLoad_ || index < 0 || index >= galleryCount_) return false;
        if (index == galleryPosition_) { updateGalleryControls(); return true; }
        const QImage image = galleryLoad_(index);
        if (image.isNull()) return false;
        imageItem_ = nullptr;
        scene_->clear();
        imageItem_ = scene_->addPixmap(QPixmap::fromImage(image));
        scene_->setSceneRect(imageItem_->boundingRect());
        galleryPosition_ = index;
        fitImage();
        updateGalleryControls();
        return true;
    }

    // Returns true if a valid next/previous image was displayed. Never changes
    // selection when only unpictured steps remain in the requested direction.
    bool navigateGallery(int direction) {
        if (!galleryLoad_ || (direction != -1 && direction != 1)) return false;
        for (int candidate = galleryPosition_ + direction;
             candidate >= 0 && candidate < galleryCount_; candidate += direction)
            if (jumpToGallery(candidate)) return true;
        return false;
    }

    void setZoom(double value) {
        if (!imageItem_) return;
        scale_ = qBound(0.05, value, 16.0);
        view_->resetTransform();
        view_->scale(scale_, scale_);
        zoomLabel_->setText(QString::number(qRound(scale_ * 100)) + QStringLiteral("%"));
    }
    void zoom(double factor) { setZoom(scale_ * factor); }
    void fitImage() {
        if (!imageItem_ || view_->viewport()->width() < 1) return;
        const QSizeF imageSize = imageItem_->boundingRect().size();
        const QSize available = view_->viewport()->size() - QSize(12, 12);
        if (imageSize.isEmpty()) return;
        setZoom(qMin(double(available.width()) / imageSize.width(),
                     double(available.height()) / imageSize.height()));
        view_->centerOn(imageItem_);
    }

private:
    void updateGalleryControls() {
        if (galleryCount_ < 1) return;
        setWindowTitle(QStringLiteral("%1 — #%2 / %3")
            .arg(galleryCaption_).arg(galleryPosition_ + 1).arg(galleryCount_));
        {
            const QSignalBlocker blocker(galleryJump_);
            galleryJump_->setValue(galleryPosition_ + 1);
        }
        const QString description = galleryDescribe_ ? galleryDescribe_(galleryPosition_) : QString();
        galleryDescription_->setText(description);
        galleryDescription_->setToolTip(description);
        galleryDescription_->setVisible(!description.isEmpty());
        galleryPrevious_->setEnabled(galleryPosition_ > 0);
        galleryNext_->setEnabled(galleryPosition_ + 1 < galleryCount_);
    }

    class ZoomView final : public QGraphicsView {
    public:
        explicit ZoomView(QWidget* parent = nullptr) : QGraphicsView(parent) {}
        std::function<void(double)> zoomRequested;
    protected:
        void wheelEvent(QWheelEvent* event) override {
            if (event->angleDelta().y() != 0 &&
                !(event->modifiers() & Qt::ShiftModifier)) {
                if (zoomRequested)
                    zoomRequested(event->angleDelta().y() > 0 ? 1.25 : 1.0 / 1.25);
                event->accept();
                return;
            }
            QGraphicsView::wheelEvent(event);
        }
    };
    ZoomView* view_{};
    QGraphicsScene* scene_{};
    QGraphicsPixmapItem* imageItem_{};
    QLabel* zoomLabel_{};
    QPushButton* galleryPrevious_{};
    QPushButton* galleryNext_{};
    QSpinBox* galleryJump_{};
    QLabel* galleryDescription_{};
    std::function<QImage(int)> galleryLoad_;
    std::function<QString(int)> galleryDescribe_;
    QString galleryCaption_;
    int galleryPosition_{-1};
    int galleryCount_{};
    double scale_{1.0};
};

// Session-only thumbnail rendering. Original QImage is retained separately for zoom.
class StepThumbnailDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void setThumbnailWidth(int width) { thumbnailWidth_ = qBound(80, width, 176); }
    int thumbnailWidth() const { return thumbnailWidth_; }
    int thumbnailHeight() const { return (thumbnailWidth_ * 17 + 12) / 24; }
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const QPixmap preview = index.data(Qt::UserRole + 42).value<QPixmap>();
        if (preview.isNull()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem blank(option);
        blank.text.clear();
        QStyledItemDelegate::paint(painter, blank, index);
        const QRect thumbArea(option.rect.left() + 6, option.rect.top() + 5,
                              thumbnailWidth_, qMax(8, option.rect.height() - 10));
        const QSize size = preview.size().scaled(thumbArea.size(), Qt::KeepAspectRatio);
        const QRect dest(QPoint(thumbArea.center().x() - size.width() / 2,
                               thumbArea.center().y() - size.height() / 2), size);
        painter->save();
        painter->setClipRect(option.rect);
        painter->drawPixmap(dest, preview);
        const QRect textArea = option.rect.adjusted(thumbnailWidth_ + 14, 0, -6, 0);
        const QString label = option.fontMetrics.elidedText(
            index.data(Qt::DisplayRole).toString(), Qt::ElideRight, qMax(0, textArea.width()));
        painter->setPen(option.palette.color(option.state & QStyle::State_Selected
                                                ? QPalette::HighlightedText : QPalette::Text));
        painter->drawText(textArea, Qt::AlignVCenter | Qt::AlignLeft, label);
        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override {
        QSize original = QStyledItemDelegate::sizeHint(option, index);
        if (!index.data(Qt::UserRole + 42).value<QPixmap>().isNull())
            original.setHeight(qMax(original.height(), thumbnailHeight() + 10));
        return original;
    }
private:
    int thumbnailWidth_{96};
};
