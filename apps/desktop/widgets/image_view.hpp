#pragma once

#include <QGraphicsView>
#include <QImage>

class QGraphicsPixmapItem;

namespace mif::desktop {
/// 通用图片预览控件：滚轮缩放、按住鼠标平移、双击恢复适应窗口。
/// 控件及其 QPixmap 必须在界面线程使用，不参与核心算法的数据计算。
class ImageView : public QGraphicsView {
    Q_OBJECT
public:
    /// 初始化归本控件所有的场景与图片项。
    explicit ImageView(QWidget* parent = nullptr);
    /// 更新显示图像并恢复适应窗口；空图像用于清空预览。
    void setImage(const QImage& image);
    /// 保持图像宽高比，将整张图像放进视口并启用自动适应模式。
    void fitImage();
protected:
    /// 自动适应模式下跟随视口尺寸变化；手动缩放后保留用户的倍率。
    void resizeEvent(QResizeEvent* event) override;
    /// 以鼠标所在位置为中心连续缩放，并限制极端倍率。
    void wheelEvent(QWheelEvent* event) override;
    /// 双击恢复自动适应模式。
    void mouseDoubleClickEvent(QMouseEvent* event) override;
private:
    // 图片项由场景管理；场景通过 Qt 父子对象关系随本控件一起销毁。
    QGraphicsPixmapItem* item_;
    // true 表示窗口大小变化时自动重新适应；滚轮缩放后切换为 false。
    bool fit_ = true;
};
} // namespace mif::desktop

