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
        layout->addLayout(bar);
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
        QTimer::singleShot(0, this, [this] { fitImage(); });
    }

    double zoomFactor() const { return scale_; }
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
    class ZoomView final : public QGraphicsView {
    public:
        explicit ZoomView(QWidget* parent = nullptr) : QGraphicsView(parent) {}
        std::function<void(double)> zoomRequested;
    protected:
        void wheelEvent(QWheelEvent* event) override {
            if (event->modifiers() & Qt::ControlModifier) {
                if (zoomRequested) zoomRequested(event->angleDelta().y() > 0 ? 1.25 : 1.0 / 1.25);
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
    double scale_{1.0};
};

// Session-only thumbnail rendering. Original QImage is retained separately for zoom.
class StepThumbnailDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
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
        const QRect thumbArea(option.rect.left() + 6, option.rect.top() + 5, 96,
                              qMax(8, option.rect.height() - 10));
        const QSize size = preview.size().scaled(thumbArea.size(), Qt::KeepAspectRatio);
        const QRect dest(QPoint(thumbArea.center().x() - size.width() / 2,
                               thumbArea.center().y() - size.height() / 2), size);
        painter->save();
        painter->setClipRect(option.rect);
        painter->drawPixmap(dest, preview);
        const QRect textArea = option.rect.adjusted(110, 0, -6, 0);
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
            original.setHeight(qMax(original.height(), 78));
        return original;
    }
};
