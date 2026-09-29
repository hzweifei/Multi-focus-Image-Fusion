#pragma once

#include <mif/fusion_options.hpp>

#include <QGroupBox>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace mif::desktop {

/// 融合设置面板：每种方法单独保存参数，切换时只显示当前方法的表单。
/// 主窗口仅在启动任务时读取快照，不直接操作方法内部的字段。
class FusionSettings : public QGroupBox {
public:
    explicit FusionSettings(QWidget* parent = nullptr);
    /// 同时收集所有方法的配置；核心算法只使用并校验 method 指定的那一套。
    FusionOptions options() const;

private:
    /// 两种方法拥有同名但相互独立的清晰度和细节权重设置，控件由 Qt 父子关系管理。
    struct MethodFields {
        QWidget* panel = nullptr;
        QComboBox* focus = nullptr;
        QSpinBox* window = nullptr;
        QSpinBox* detail_radius = nullptr;
        QDoubleSpinBox* detail_epsilon = nullptr;
    };

    /// 块方差方法仅配置分块与块级决策平滑，不借用其他方法的清晰度指标。
    struct DctFields {
        QWidget* panel = nullptr;
        QSpinBox* block_size = nullptr;
        QSpinBox* consistency_window = nullptr;
    };
    /// 小波方法在各尺度的系数上比较活动度，没有单一空间权重图的参数。
    struct DtcwtFields {
        QWidget* panel = nullptr;
        QSpinBox* levels = nullptr;
        QSpinBox* activity_window = nullptr;
    };
    /// 梯度筛帧、局部差异判定与引导滤波各有独立参数，切换方法时全部保留。
    struct GfgfgfFields {
        QWidget* panel = nullptr;
        QSpinBox* difference_window = nullptr;
        QDoubleSpinBox* selection_ratio = nullptr;
        QDoubleSpinBox* difference_threshold = nullptr;
        QSpinBox* guided_radius = nullptr;
        QDoubleSpinBox* guided_epsilon = nullptr;
    };

    /// 更新当前方法说明及表单可见性，保留隐藏表单中的值。
    void updateMethodVisibility();
    /// 恢复当前方法的默认值；方法选择和其他方法的配置保持不变。
    void resetCurrentMethod();

    QComboBox* method_ = nullptr;
    QLabel* method_hint_ = nullptr;
    MethodFields guided_;
    MethodFields pyramid_;
    DctFields dct_;
    DtcwtFields dtcwt_;
    GfgfgfFields gfgfgf_;
    QSpinBox* base_radius_ = nullptr;
    QDoubleSpinBox* base_epsilon_ = nullptr;
    QSpinBox* levels_ = nullptr;
};

} // 命名空间 mif::desktop
