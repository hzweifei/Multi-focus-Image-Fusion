#pragma once

#include <mif/fusion/options_base.hpp>

namespace mif {

/// DCT 块方差融合参数：以空间域方差衡量块的全部交流能量，不显式融合 DCT 系数。
/// 各非重叠块选取清晰来源，再对来源索引做两次中值一致性处理。
struct BlockVarianceFusionOptions final : FusionOptionsBase {
    /// 方形块边长，[2, 128]，单位为输入像素；不足整块的右侧/下侧边缘仍参与计算。
    int block_size = 8;
    /// 块级索引中值窗口边长，[1, 31] 内的奇数；1 表示不平滑来源选择。
    /// 该窗口以块为单位，默认 7 对应最多 7×7 个相邻块。
    int consistency_window_size = 7;
    /// 复制完整具体配置，供后台任务与 Python 绑定保存独立参数快照。
    std::unique_ptr<FusionOptionsBase> clone() const override {
        return std::make_unique<BlockVarianceFusionOptions>(*this);
    }

};

} // 命名空间 mif
