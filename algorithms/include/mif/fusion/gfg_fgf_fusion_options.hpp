#pragma once

#include <mif/fusion/options_base.hpp>

namespace mif {

/// GFG-FGF：G/R 独立滤波、G 候选优先、Sobel 消歧与决策权重滤波。
/// 彩色输入转灰度评分和引导，融合权重共同作用于原图的全部颜色通道。
struct GfgFgfFusionOptions final : FusionOptionsBase {
    /// 局部均值窗口边长，[1, 255] 内的奇数；灰度减去局部均值后取绝对值。
    int local_mean_window_size = 7;
    /// 可选工程扩展：保留全图 Scharr 分数不低于最高分数此比例的输入。
    /// 默认 0 保留全部焦面，符合论文；有限且位于 [0, 1]，无纹理时也保留全部。
    double selection_ratio = 0;
    /// 原始 G 达到阈值即成为 G 候选；有候选时仅比较其滤波后 G，否则比较滤波后 R。
    /// 有限且位于 [0, 1]，作用于归一化图像的原始 G；不对滤波响应应用阈值。
    double gfg_threshold = 0.005;
    /// 两阶段引导滤波共用的窗口半径，[1, 255]，单位为输入像素。
    int guided_radius = 5;
    /// 两阶段共用正则项，[1e-6, FLT_MAX]；这是项目默认值，论文未给出该参数。
    double guided_epsilon = 0.3;
    /// 两阶段共用下采样倍数，[1, 16]；1 为全分辨率，4 为项目默认值。
    /// 小图自动降低有效倍数；低分辨率系数上采样后结合原分辨率引导图输出。
    int guided_subsample_factor = 4;
    /// 复制完整具体配置，供后台任务与 Python 绑定保存独立参数快照。
    std::unique_ptr<FusionOptionsBase> clone() const override {
        return std::make_unique<GfgFgfFusionOptions>(*this);
    }

};

} // 命名空间 mif
