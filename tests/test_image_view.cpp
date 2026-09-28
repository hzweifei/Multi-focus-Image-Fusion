#include "main_window.hpp"
#include "widgets/image_view.hpp"
#include <QAbstractSlider>
#include <QApplication>
#include <QColor>
#include <QEventLoop>
#include <QLineF>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using mif::desktop::ImageView;
using mif::desktop::ViewState;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

/// 处理布局和视口更新，不依赖机器速度或固定睡眠时间。
void flushEvents() {
    for (int i = 0; i < 4; ++i) {
        QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QApplication::processEvents(QEventLoop::AllEvents);
    }
}

struct SignalCounts {
    int source = 0;
    int result = 0;
    int total() const { return source + result; }
};

/// 从实际绘制变换和场景坐标观测视野，不使用 viewState 中缓存的倍率或中心作为结果。
struct ObservedView {
    double zoom;
    QPointF center;
    double tolerance_x;
    double tolerance_y;
};

ObservedView observe(const ImageView& view, const QSize& image_size) {
    require(!image_size.isEmpty(), "Cannot inspect an empty image fixture");
    const QSize available = view.maximumViewportSize();
    const double fit_scale = std::min((available.width() - 4.0) / image_size.width(),
                                      (available.height() - 4.0) / image_size.height());
    const double scale = view.transform().m11();
    require(fit_scale > 0 && std::isfinite(scale) && scale > 0,
            "Preview has an invalid rendering transform");
    require(std::abs(view.transform().m22() - scale) < 1e-9,
            "Preview distorts the image aspect ratio");
    const QPointF center = view.mapToScene(view.viewport()->rect().center());
    // centerOn 与滚动条使用整数设备坐标，允许每侧最多约 2.5 个视口像素的舍入误差。
    return {scale / fit_scale,
            {center.x() / image_size.width(), center.y() / image_size.height()},
            2.5 / (scale * image_size.width()), 2.5 / (scale * image_size.height())};
}

void checkSameView(const ObservedView& first, const ObservedView& second, const std::string& label) {
    require(std::abs(first.zoom - second.zoom) < 1e-7 * std::max({1.0, first.zoom, second.zoom}),
            label + ": rendered relative zoom differs");
    require(std::abs(first.center.x() - second.center.x()) <= first.tolerance_x + second.tolerance_x &&
            std::abs(first.center.y() - second.center.y()) <= first.tolerance_y + second.tolerance_y,
            label + ": rendered normalized centers differ: (" +
            std::to_string(first.center.x()) + ", " + std::to_string(first.center.y()) + ") versus (" +
            std::to_string(second.center.x()) + ", " + std::to_string(second.center.y()) + ")");
}

void checkRequestedView(const ImageView& view, const QSize& image_size,
                        const ViewState& requested, const std::string& label) {
    const auto actual = observe(view, image_size);
    require(std::abs(actual.zoom - requested.zoom) < 1e-7 * std::max(1.0, requested.zoom),
            label + ": requested zoom was only cached, not rendered");
    require(std::abs(actual.center.x() - requested.center.x()) <= actual.tolerance_x &&
            std::abs(actual.center.y() - requested.center.y()) <= actual.tolerance_y,
            label + ": requested center was only cached, not rendered");
}

void checkLinked(const ImageView& source, const QSize& source_size,
                 const ImageView& result, const QSize& result_size, const std::string& label) {
    require(source.viewState().fit == result.viewState().fit, label + ": fit modes differ");
    checkSameView(observe(source, source_size), observe(result, result_size), label);
}

/// 一次真实用户操作可以改变横纵滚动条多次，但被同步的一侧不能再反向发信号。
void checkUserSignals(const SignalCounts& current, const SignalCounts& before,
                      bool from_source, const std::string& label) {
    const int active = from_source ? current.source - before.source : current.result - before.result;
    const int passive = from_source ? current.result - before.result : current.source - before.source;
    require(active > 0, label + ": user interaction did not publish its view");
    require(passive == 0, label + ": applying a synchronized view emitted an echo");
    require(current.total() - before.total() < 24, label + ": excessive synchronization signals");
}

void wheel(ImageView& view, int delta) {
    const QPoint point = view.viewport()->rect().center();
    QWheelEvent event(QPointF(point), QPointF(view.viewport()->mapToGlobal(point)),
                      QPoint(), QPoint(0, delta), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(view.viewport(), &event);
    flushEvents();
}

void mouse(ImageView& view, QEvent::Type type, const QPoint& position,
           Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent event(type, QPointF(position), QPointF(view.viewport()->mapToGlobal(position)),
                      button, buttons, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &event);
}

void drag(ImageView& view) {
    const QPoint start = view.viewport()->rect().center();
    mouse(view, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(view, QEvent::MouseMove, start + QPoint(18, -12), Qt::NoButton, Qt::LeftButton);
    mouse(view, QEvent::MouseMove, start + QPoint(37, -29), Qt::NoButton, Qt::LeftButton);
    mouse(view, QEvent::MouseButtonRelease, start + QPoint(37, -29), Qt::LeftButton, Qt::NoButton);
    flushEvents();
}

void doubleClick(ImageView& view) {
    const QPoint position = view.viewport()->rect().center();
    mouse(view, QEvent::MouseButtonDblClick, position, Qt::LeftButton, Qt::LeftButton);
    mouse(view, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
    flushEvents();
}

QImage previewImage(const QSize& size) {
    QImage image(size, QImage::Format_RGB32);
    image.fill(QColor(75, 125, 180));
    return image;
}

void checkFit(const ImageView& source, const QSize& source_size,
              const ImageView& result, const QSize& result_size, const std::string& label) {
    checkLinked(source, source_size, result, result_size, label);
    require(source.viewState().fit && result.viewState().fit, label + ": fit mode was not restored");
    ViewState expected;
    checkRequestedView(source, source_size, expected, label + " source");
    checkRequestedView(result, result_size, expected, label + " result");
}

} // 匿名命名空间

/// 通过主窗口的真实双向连接验证联动，不在测试中另外连接两个预览来替代产品接线。
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("MultiFocusImageViewTests");
    app.setApplicationName("PreviewSynchronization");
    app.setQuitOnLastWindowClosed(false);
    try {
        QTemporaryDir settings;
        require(settings.isValid(), "Cannot create isolated test settings");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());

        SignalCounts counts;
        mif::desktop::MainWindow window;
        window.resize(1420, 900);
        window.show();
        flushEvents();
        auto* source = window.findChild<ImageView*>("sourcePreview");
        auto* result = window.findChild<ImageView*>("resultPreview");
        auto* fit = window.findChild<QPushButton*>("fitPreviews");
        require(source && result && fit, "The main window is missing named preview controls");
        QObject::connect(source, &ImageView::viewChanged, &window, [&](const ViewState&) { ++counts.source; });
        QObject::connect(result, &ImageView::viewChanged, &window, [&](const ViewState&) { ++counts.result; });

        // 刻意让面板宽度和图片宽高比不同，排除只复制绝对 transform 恰好通过的情况。
        QSplitter* splitter = nullptr;
        for (auto* parent = source->parentWidget(); parent && !splitter; parent = parent->parentWidget())
            splitter = qobject_cast<QSplitter*>(parent);
        require(splitter != nullptr, "Cannot locate the preview splitter");
        splitter->setSizes({320, 640});
        QSize source_size(1200, 900), result_size(800, 1100);
        source->setImage(previewImage(source_size));
        result->setImage(previewImage(result_size));
        flushEvents();
        require(std::abs(source->viewport()->width() - result->viewport()->width()) > 30,
                "Preview fixture did not create unequal panel widths");
        checkFit(*source, source_size, *result, result_size, "Initial fit");

        auto before = counts;
        wheel(*source, 1200);
        checkUserSignals(counts, before, true, "Source wheel");
        require(!source->viewState().fit && observe(*source, source_size).zoom > 4,
                "Source wheel did not create a scrollable manual view");
        checkLinked(*source, source_size, *result, result_size, "Source wheel");

        before = counts;
        const double previous_zoom = observe(*result, result_size).zoom;
        wheel(*result, -120);
        checkUserSignals(counts, before, false, "Result wheel");
        require(observe(*result, result_size).zoom < previous_zoom, "Result wheel did not zoom out");
        checkLinked(*source, source_size, *result, result_size, "Result wheel");

        before = counts;
        const auto before_drag = observe(*source, source_size);
        drag(*source);
        checkUserSignals(counts, before, true, "Source drag");
        require(QLineF(before_drag.center, observe(*source, source_size).center).length() > 1e-4,
                "Mouse drag did not move the rendered image");
        checkLinked(*source, source_size, *result, result_size, "Source drag");

        // 触发真实滚动条动作，验证非鼠标拖动的平移也会走相同的联动路径。
        before = counts;
        auto* horizontal = result->horizontalScrollBar();
        auto* vertical = result->verticalScrollBar();
        require(horizontal->maximum() > horizontal->minimum() && vertical->maximum() > vertical->minimum(),
                "Result fixture is not scrollable in both directions");
        const int old_horizontal = horizontal->value(), old_vertical = vertical->value();
        horizontal->triggerAction(QAbstractSlider::SliderSingleStepAdd);
        vertical->triggerAction(QAbstractSlider::SliderSingleStepSub);
        flushEvents();
        require(horizontal->value() != old_horizontal && vertical->value() != old_vertical,
                "Scrollbar actions did not move the image");
        checkUserSignals(counts, before, false, "Result scrollbars");
        checkLinked(*source, source_size, *result, result_size, "Result scrollbars");

        // applyViewState 是接收端接口，应真实更新渲染而不发布新的变化信号。
        before = counts;
        ViewState requested = source->viewState();
        requested.center = {0.54, 0.48};
        requested.zoom *= 1.1;
        requested.fit = false;
        result->applyViewState(requested);
        flushEvents();
        checkRequestedView(*result, result_size, requested, "Quiet apply");
        require(counts.total() == before.total(), "applyViewState must not emit viewChanged");
        result->applyViewState(source->viewState());
        flushEvents();

        // 手动视野在布局变化后仍应按各面板的新适应尺度呈现；重复处理事件不能继续震荡。
        before = counts;
        const auto before_resize = observe(*source, source_size);
        window.resize(1540, 850);
        splitter->setSizes({420, 660});
        flushEvents();
        checkSameView(before_resize, observe(*source, source_size), "Manual view after resize");
        checkLinked(*source, source_size, *result, result_size, "Resize synchronization");
        require(counts.total() - before.total() < 8, "Resize caused excessive view synchronization");
        const int settled = counts.total();
        flushEvents();
        require(counts.total() == settled, "Preview layout did not settle after resize");

        // 替换不同尺寸的图像并保留视野，应维持相对倍率和归一化中心。
        const auto before_replace = observe(*source, source_size);
        source_size = {1400, 840};
        result_size = {840, 1400};
        source->setImage(previewImage(source_size), true);
        result->setImage(previewImage(result_size), true);
        flushEvents();
        checkSameView(before_replace, observe(*source, source_size), "Preserved view after image replacement");
        checkLinked(*source, source_size, *result, result_size, "Replacement synchronization");

        // 清空一侧及在空侧交互不能把另一侧恢复为适应窗口；空侧仍应接收后续视野。
        before = counts;
        const auto before_clear = observe(*source, source_size);
        result->setImage(QImage());
        wheel(*result, 120);
        doubleClick(*result);
        checkSameView(before_clear, observe(*source, source_size), "Empty preview changed its peer");
        require(counts.total() == before.total(), "An empty preview emitted a reset");
        wheel(*source, 120);
        const auto while_empty = observe(*source, source_size);
        result->setImage(previewImage(result_size), true);
        flushEvents();
        checkSameView(while_empty, observe(*source, source_size), "Reloading the result reset the source");
        checkLinked(*source, source_size, *result, result_size, "Reloaded preview synchronization");

        before = counts;
        doubleClick(*result);
        checkUserSignals(counts, before, false, "Result double click");
        checkFit(*source, source_size, *result, result_size, "Double-click fit");

        wheel(*source, 1200);
        before = counts;
        fit->click();
        flushEvents();
        checkFit(*source, source_size, *result, result_size, "Fit button");
        require(counts.total() - before.total() > 0 && counts.total() - before.total() < 8,
                "Fit button did not publish a bounded fit update");
        window.resize(1320, 920);
        flushEvents();
        checkFit(*source, source_size, *result, result_size, "Fit after resize");

        std::cout << "PASS linked previews: wheel / drag / scrollbars / fit / resize / empty / replacement\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
