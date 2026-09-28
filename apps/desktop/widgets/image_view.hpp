#pragma once
#include <QGraphicsView>
#include <QImage>
class QGraphicsPixmapItem;
namespace mif::desktop {
class ImageView : public QGraphicsView {
    Q_OBJECT
public:
    explicit ImageView(QWidget* parent = nullptr);
    void setImage(const QImage& image);
    void fitImage();
protected:
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
private:
    QGraphicsPixmapItem* item_;
    bool fit_ = true;
};
}

