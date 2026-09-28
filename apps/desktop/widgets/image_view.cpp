#include "image_view.hpp"
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QWheelEvent>
#include <cmath>

namespace mif::desktop {
ImageView::ImageView(QWidget* parent) : QGraphicsView(parent) {
    // 场景负责图片项的生命周期，视图负责输入事件和显示变换。
    setScene(new QGraphicsScene(this));
    item_ = scene()->addPixmap(QPixmap());
    // 手形拖拽实现平移，缩放锚点设为鼠标位置，便于检查局部融合边缘。
    setDragMode(ScrollHandDrag);
    setTransformationAnchor(AnchorUnderMouse);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setBackgroundBrush(QColor("#111b2a"));
    setFrameShape(QFrame::NoFrame);
    setMinimumSize(180, 180);
}
void ImageView::setImage(const QImage& image) {
    // QPixmap 持有可显示的像素数据，不依赖调用方传入 QImage 的生命周期。
    item_->setPixmap(QPixmap::fromImage(image));
    scene()->setSceneRect(item_->boundingRect());
    fitImage();
}
void ImageView::fitImage() {
    fit_ = true;
    // 先清除累计缩放，避免多次适应时叠加已有变换。
    resetTransform();
    if (!item_->pixmap().isNull()) fitInView(item_, Qt::KeepAspectRatio);
}
void ImageView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    if (fit_) fitImage();
}
void ImageView::wheelEvent(QWheelEvent* event) {
    if (item_->pixmap().isNull()) return;
    // 指数映射使正反方向缩放互为倒数，并兼容不同幅度的滚轮增量。
    const double factor = std::pow(1.0015, event->angleDelta().y());
    const double next = transform().m11() * factor;
    // 倍率边界避免将图像缩小到不可见或放大到难以操作。
    if (next > 0.005 && next < 64) { fit_ = false; scale(factor, factor); }
    event->accept();
}
void ImageView::mouseDoubleClickEvent(QMouseEvent* event) {
    fitImage();
    event->accept();
}
} // namespace mif::desktop

