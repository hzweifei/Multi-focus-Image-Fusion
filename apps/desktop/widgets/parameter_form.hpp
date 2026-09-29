#pragma once

#include <QFormLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QWheelEvent>

namespace mif::desktop {

// 仅供桌面的配准和融合参数页共用；字段范围、默认值与参数快照仍由各页面管理。

/// 把滚轮交给父滚动区，避免滚动参数页时误改字段；点击和键盘操作沿用 Qt 行为。
template <typename Widget>
class ScrollSafeWidget final : public Widget {
public:
    explicit ScrollSafeWidget(QWidget* parent = nullptr) : Widget(parent) {}

protected:
    void wheelEvent(QWheelEvent* event) override { event->ignore(); }
};

/// 统一表单间距和伸展规则，独立父容器负责整组显隐并持有布局。
inline QFormLayout* createParameterForm(QWidget* parent) {
    auto* form = new QFormLayout(parent);
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(8);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    return form;
}

/// 对齐标签列并共享字段说明；省略 row 时追加，指定 row 时插入对应位置。
/// 布局接管标签；buddy 和无障碍名称让键盘与辅助工具能识别对应字段。
inline void addParameter(QFormLayout* form, const QString& title, QWidget* field,
                         int label_width, int row = -1) {
    auto* label = new QLabel(title);
    label->setMinimumWidth(label_width);
    label->setToolTip(field->toolTip());
    label->setBuddy(field);
    field->setAccessibleName(title);
    field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (row < 0) form->addRow(label, field);
    else form->insertRow(row, label, field);
}

} // 命名空间 mif::desktop
