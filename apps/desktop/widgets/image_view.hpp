#pragma once

#include <QGraphicsView>
#include <QImage>
#include <QMetaType>
#include <QPointF>

class QGraphicsPixmapItem;

namespace mif::desktop {

/// 与面板大小、图像尺寸无关的导航状态，两个预览通过此状态同步。
struct ViewState {
    double zoom = 1;              ///< 相对于本面板“适应窗口”尺度的倍率。
    QPointF center{0.5, 0.5};      ///< 可视中心在图像宽高中的比例，范围为 [0, 1]。
    bool fit = true;              ///< 自动适应模式；此时倍率为 1，中心为图像中心。
};

/// 通用图片预览控件：滚轮缩放、按住鼠标平移、双击恢复适应窗口，可同步导航状态。
/// 控件及其 QPixmap 必须在界面线程使用，不参与核心算法的数据计算。
class ImageView : public QGraphicsView {
    Q_OBJECT
public:
    /// 初始化归本控件所有的场景与图片项。
    explicit ImageView(QWidget* parent = nullptr);
    /// 更新显示图像；默认恢复适应窗口，preserveView 为 true 时保留缓存的导航状态。
    /// 空图像用于清空预览，此操作不发导航信号，因此不会意外重置另一个面板。
    void setImage(const QImage& image, bool preserveView = false);
    /// 返回理想导航状态；应用状态时保留中心值，不因滚动条像素取整而反向漂移。
    ViewState viewState() const { return state_; }
    /// 应用另一个面板的状态，不发送 viewChanged；空图时也缓存状态供稍后显示。
    /// 非法数值被替换或限制为安全范围，避免产生不可逆或非有限的显示变换。
    void applyViewState(const ViewState& state);
    /// 用户主动适应窗口：恢复整图并发信号，使另一个面板也适应自己的窗口。
    void fitImage();

signals:
    /// 用户缩放、平移或适应窗口后的状态；程序应用状态及调整窗口大小不会发出。
    void viewChanged(const ViewState& state);

protected:
    /// 重新按本面板大小应用理想状态，屏蔽布局引起的滚动变化，不向另一个面板广播。
    void resizeEvent(QResizeEvent* event) override;
    /// 以鼠标所在位置为中心连续缩放，并限制极端倍率。
    void wheelEvent(QWheelEvent* event) override;
    /// 双击恢复自动适应模式。
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    /// 手形拖动、滚动条和键盘平移均通过此入口捕获，避免只同步鼠标拖动。
    void scrollContentsBy(int dx, int dy) override;

private:
    /// 图片是否非空；构造期间场景尚未准备好时也可安全查询。
    bool hasImage() const;
    /// 计算整图适应尺度，以最大视口为基准，避免滚动条显隐导致倍率反复变化。
    double fitScale() const;
    /// 按缓存状态设置变换和中心；包含抑制滚动/布局反馈的保护。
    void applyCachedView();
    /// 用户导航完成后从实际可视中心更新状态，并发送一次导航信号。
    void publishNavigation();

    // 图片项由场景管理；场景通过 Qt 父子对象关系随本控件一起销毁。
    QGraphicsPixmapItem* item_ = nullptr;
    // 保存未被滚动范围夹取或像素取整改变的目标状态，便于切换图片和调整面板大小。
    ViewState state_;
    // 修改变换、图片和布局时屏蔽 scrollContentsBy，阻止双向联动产生回环。
    bool applying_ = false;
};
} // namespace mif::desktop

Q_DECLARE_METATYPE(mif::desktop::ViewState)

