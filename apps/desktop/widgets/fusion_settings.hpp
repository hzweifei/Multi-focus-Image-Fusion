#pragma once

#include <mif/fusion/options_base.hpp>

#include <QGroupBox>
#include <memory>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace mif::desktop {

/// 界面内部的方法标识，只用于选择表单；核心通过具体配置类型选择算法。
enum class FusionMethod { GuidedFilter, LaplacianPyramid, BlockVariance, Dtcwt, GfgFgf };

/// 融合设置面板：每种方法单独保存参数，切换时只显示当前方法的表单。
/// 主窗口仅在启动任务时读取快照，不直接操作方法内部的字段。
class FusionSettings : public QGroupBox {
public:
    explicit FusionSettings(QWidget* parent = nullptr);
    /// 创建当前方法的独立配置快照；隐藏表单的参数继续由各自控件保留。
    std::unique_ptr<FusionOptionsBase> options() const;

private:
    /// 两种方法拥有同名但相互独立的清晰度和细节权重设置，控件由 Qt 父子关系管理。
    struct MethodFields {
        QWidget* panel = nullptr;
        QComboBox* focus = nullptr;
        QSpinBox* window_size = nullptr;
        QSpinBox* detail_radius = nullptr;
        QDoubleSpinBox* detail_epsilon = nullptr;
    };

    /// 块方差方法仅配置分块与块级决策平滑，不借用其他方法的清晰度指标。
    struct BlockVarianceFields {
        QWidget* panel = nullptr;
        QSpinBox* block_size = nullptr;
        QSpinBox* consistency_window_size = nullptr;
    };
    /// 小波方法在各尺度的系数上比较活动度，没有单一空间权重图的参数。
    struct DtcwtFields {
        QWidget* panel = nullptr;
        QSpinBox* max_levels = nullptr;
        QSpinBox* activity_window_size = nullptr;
    };
    /// 可选筛帧、GFG 聚焦度量与快速引导滤波各有独立参数，切换方法时全部保留。
    struct GfgFgfFields {
        QWidget* panel = nullptr;
        QSpinBox* local_mean_window_size = nullptr;
        QDoubleSpinBox* selection_ratio = nullptr;
        QDoubleSpinBox* gfg_threshold = nullptr;
        QSpinBox* guided_radius = nullptr;
        QDoubleSpinBox* guided_epsilon = nullptr;
        QSpinBox* guided_subsample_factor = nullptr;
    };

    /// 更新当前方法说明及表单可见性，保留隐藏表单中的值。
    void updateMethodVisibility();
    /// 恢复当前方法的默认值；方法选择和其他方法的配置保持不变。
    void resetCurrentMethod();

    QComboBox* method_ = nullptr;
    QLabel* method_hint_ = nullptr;
    MethodFields guided_;
    MethodFields pyramid_;
    BlockVarianceFields block_variance_;
    DtcwtFields dtcwt_;
    GfgFgfFields gfg_fgf_;
    QSpinBox* base_radius_ = nullptr;
    QDoubleSpinBox* base_epsilon_ = nullptr;
    QSpinBox* max_levels_ = nullptr;
};

} // 命名空间 mif::desktop
