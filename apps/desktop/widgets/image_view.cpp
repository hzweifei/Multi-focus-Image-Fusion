#include "image_view.hpp"
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QWheelEvent>
#include <cmath>

namespace mif::desktop {
ImageView::ImageView(QWidget* parent) : QGraphicsView(parent) {
    setScene(new QGraphicsScene(this));
    item_ = scene()->addPixmap(QPixmap());
    setDragMode(ScrollHandDrag);
    setTransformationAnchor(AnchorUnderMouse);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setBackgroundBrush(QColor("#111b2a"));
    setFrameShape(QFrame::NoFrame);
    setMinimumSize(180, 180);
}
void ImageView::setImage(const QImage& image) {
    item_->setPixmap(QPixmap::fromImage(image));
    scene()->setSceneRect(item_->boundingRect());
    fitImage();
}
void ImageView::fitImage() {
    fit_ = true;
    resetTransform();
    if (!item_->pixmap().isNull()) fitInView(item_, Qt::KeepAspectRatio);
}
void ImageView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    if (fit_) fitImage();
}
void ImageView::wheelEvent(QWheelEvent* event) {
    if (item_->pixmap().isNull()) return;
    const double factor = std::pow(1.0015, event->angleDelta().y());
    const double next = transform().m11() * factor;
    if (next > 0.005 && next < 64) { fit_ = false; scale(factor, factor); }
    event->accept();
}
void ImageView::mouseDoubleClickEvent(QMouseEvent* event) {
    fitImage();
    event->accept();
}
}

