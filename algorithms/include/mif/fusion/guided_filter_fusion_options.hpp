#pragma once

#include <mif/fusion/options_base.hpp>

#include <mif/fusion/focus_measure_options.hpp>

namespace mif {

/// 双尺度引导滤波融合的独立参数；滤波在归一化到 [0, 1] 的浮点图像上执行。
struct GuidedFilterFusionOptions final : FusionOptionsBase {
    /// 本方法自己的清晰度评分配置。
    FocusMeasureOptions focus;
    /// 基础层均值滤波及基础权重引导滤波的半径，[1, 255]，单位为输入图像像素。
    int base_radius = 15;
    /// 细节权重引导滤波半径，[1, 255]；实际窗口边长为 2 * radius + 1。
    int detail_radius = 3;
    /// 基础权重正则项，范围 [1e-6, FLT_MAX]；与灰度局部方差相加，越大通常越平滑。
    /// 下限用于避免官方 float32 滤波在平坦区把过小正则项舍去后除零。
    double base_epsilon = 0.01;
    /// 细节权重正则项，范围 [1e-6, FLT_MAX]，含义同 base_epsilon。
    double detail_epsilon = 0.0001;
    /// 复制完整具体配置，供后台任务与 Python 绑定保存独立参数快照。
    std::unique_ptr<FusionOptionsBase> clone() const override {
        return std::make_unique<GuidedFilterFusionOptions>(*this);
    }

};

} // 命名空间 mif
