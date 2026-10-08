#pragma once

#include <mif/fusion/options_base.hpp>

namespace mif {

/// 双树复小波融合参数；各颜色通道独立处理，不需要空间域清晰度评分。
struct DtcwtFusionOptions final : FusionOptionsBase {
    /// 分解层数上限，[1, 16]；任一维的低频平面只剩两个采样点时停止继续分解。
    /// 首层使用 near_sym_a，后续使用 qshift_a，每层产生六个复数方向子带。
    int max_levels = 4;
    /// 高频活动度及多数一致性窗口边长，必须是 [1, 31] 内的奇数。
    /// 单位是当前小波子带的采样点，因此同一值在粗层对应更大的原图区域。
    int activity_window_size = 3;
    /// 复制完整具体配置，供后台任务与 Python 绑定保存独立参数快照。
    std::unique_ptr<FusionOptionsBase> clone() const override {
        return std::make_unique<DtcwtFusionOptions>(*this);
    }

};

} // 命名空间 mif
