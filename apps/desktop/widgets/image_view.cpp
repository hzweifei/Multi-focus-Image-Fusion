#include "image_view.hpp"
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace mif::desktop {
namespace {

// 限制相对于整图显示的倍率，既允许缩小留白，也避免滚动范围膨胀到极端数值。
constexpr double min_zoom = 0.05;
constexpr double max_zoom = 64.0;

ViewState validState(ViewState state) {
    if (!std::isfinite(state.zoom) || state.zoom <= 0) state.zoom = 1;
    state.zoom = std::clamp(state.zoom, min_zoom, max_zoom);
    const auto coordinate = [](double value) {
        return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.5;
    };
    state.center = {coordinate(state.center.x()), coordinate(state.center.y())};
    // 适应模式始终表示完整居中显示，不保留无实际意义的自定义中心与倍率。
    if (state.fit) {
        state.zoom = 1;
        state.center = {0.5, 0.5};
    }
    return state;
}

} // 匿名命名空间

ImageView::ImageView(QWidget* parent) : QGraphicsView(parent) {
    qRegisterMetaType<ViewState>("ViewState");
    // 场景负责图片项的生命周期，视图负责输入事件和显示变换。
    setScene(new QGraphicsScene(this));
    item_ = scene()->addPixmap(QPixmap());
    // 手形拖拽复用 Qt 的滚动机制；缩放与 resize 的中心由下方导航状态统一控制。
    // 不依赖 Qt 的自动鼠标锚点，避免应用另一个面板的状态时读取本面板鼠标位置。
    setDragMode(ScrollHandDrag);
    setTransformationAnchor(NoAnchor);
    setResizeAnchor(NoAnchor);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setBackgroundBrush(QColor("#111b2a"));
    setFrameShape(QFrame::NoFrame);
    setMinimumSize(180, 180);
}

bool ImageView::hasImage() const {
    return item_ && !item_->pixmap().isNull();
}

double ImageView::fitScale() const {
    if (!hasImage()) return 1;
    const auto bounds = item_->boundingRect();
    // 最大视口不受 AsNeeded 滚动条显隐影响；留出少量边距吸收整数像素取整。
    // 若以当前 viewport 大小反复 fit，滚动条出现/消失会再次改变 viewport 大小。
    const auto available = maximumViewportSize();
    return std::min(std::max(1, available.width() - 4) / bounds.width(),
                    std::max(1, available.height() - 4) / bounds.height());
}

void ImageView::applyCachedView() {
    QScopedValueRollback<bool> guard(applying_, true);
    if (!hasImage()) {
        resetTransform();
        return;
    }
    const auto bounds = item_->boundingRect();
    const double scale = fitScale() * state_.zoom;
    setTransform(QTransform::fromScale(scale, scale));
    centerOn(bounds.left() + state_.center.x() * bounds.width(),
             bounds.top() + state_.center.y() * bounds.height());
    // centerOn 可能受边界和整像素滚动条限制；保留理想状态，不用实际中心覆盖缓存。
}

void ImageView::setImage(const QImage& image, bool preserveView) {
    QScopedValueRollback<bool> guard(applying_, true);
    if (!preserveView) state_ = {};
    // QPixmap 持有可显示的像素数据，不依赖调用方传入 QImage 的生命周期。
    item_->setPixmap(QPixmap::fromImage(image));
    // 空 QRect 会让场景恢复自动范围，可能保留历史最大边界；用微小固定范围清空滚动条。
    scene()->setSceneRect(hasImage() ? item_->boundingRect() : QRectF(0, 0, 1, 1));
    applyCachedView();
}

void ImageView::applyViewState(const ViewState& state) {
    state_ = validState(state);
    applyCachedView();
}

void ImageView::fitImage() {
    applyViewState({});
    emit viewChanged(state_);
}

void ImageView::resizeEvent(QResizeEvent* event) {
    const bool nested = applying_;
    QScopedValueRollback<bool> guard(applying_, true);
    QGraphicsView::resizeEvent(event);
    // setTransform 可能间接触发视口 resize；只在最外层重新应用一次状态。
    // 用户手动缩放后也保持相对倍率和理想中心，不让窗口尺寸变化变成一次导航。
    if (!nested) applyCachedView();
}

void ImageView::wheelEvent(QWheelEvent* event) {
    if (!hasImage()) {
        event->ignore();
        return;
    }
    // 指数映射使正反方向缩放互为倒数，并兼容不同幅度的滚轮增量。
    // 先限制输入增量，避免人工构造的极端事件使指数运算溢出。
    const double factor = std::pow(1.0015, std::clamp(event->angleDelta().y(), -2400, 2400));
    const double zoom = std::clamp(state_.zoom * factor, min_zoom, max_zoom);
    if (zoom != state_.zoom) {
        const auto mouse = event->position().toPoint();
        const auto anchor = mapToScene(mouse);
        {
            QScopedValueRollback<bool> guard(applying_, true);
            state_.zoom = zoom;
            state_.fit = false;
            applyCachedView();
            // 缩放后补偿鼠标下的场景坐标位移，保持正在检查的局部位于光标下。
            const auto shifted_anchor = mapToScene(mouse);
            const auto center = mapToScene(viewport()->rect().center());
            centerOn(center + anchor - shifted_anchor);
        }
        // 只在完整缩放与锚点补偿结束后发一次信号，不广播中间滚动条值。
        publishNavigation();
    }
    event->accept();
}

void ImageView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (hasImage()) fitImage();
    event->accept();
}

void ImageView::scrollContentsBy(int dx, int dy) {
    QGraphicsView::scrollContentsBy(dx, dy);
    // 整图适应时没有可拖动区域；忽略自动布局造成的中心取整变化。
    // 其余平移（手形拖动、滚动条、方向键）均经由此函数同步。
    if (!applying_ && !state_.fit && hasImage() && (dx != 0 || dy != 0)) publishNavigation();
}

void ImageView::publishNavigation() {
    if (applying_ || !hasImage()) return;
    const auto bounds = item_->boundingRect();
    const auto center = mapToScene(viewport()->rect().center());
    state_.fit = false;
    state_.center = {(center.x() - bounds.left()) / bounds.width(),
                     (center.y() - bounds.top()) / bounds.height()};
    state_ = validState(state_);
    emit viewChanged(state_);
}

} // namespace mif::desktop

