#pragma once

namespace mif {

/// 双树复小波融合参数；各颜色通道独立处理，不需要空间域清晰度评分。
struct DtcwtOptions {
    /// 分解层数上限，[1, 16]；任一维的低频平面只剩两个采样点时停止继续分解。
    /// 首层使用 near_sym_a，后续使用 qshift_a，每层产生六个复数方向子带。
    int levels = 4;
    /// 高频活动度及多数一致性窗口边长，必须是 [1, 31] 内的奇数。
    /// 单位是当前小波子带的采样点，因此同一值在粗层对应更大的原图区域。
    int activity_window = 3;
};

} // 命名空间 mif
