#pragma once

#include <mif/registration/options_base.hpp>

namespace mif {

/// 通过 SIFT 特征匹配与 RANSAC 估计单应性；固定返回 3×3 变换，不接收 ECC 模型。
struct SiftRegistrationOptions final : RegistrationOptionsBase {
    /// 最多保留的 SIFT 特征数，[64, 100000]。
    int max_features = 4000;
    /// 最近邻/次近邻描述子距离比阈值，有限且在 (0, 1)；越小筛选越严格。
    double match_ratio_threshold = 0.75;
    /// RANSAC 重投影误差阈值，单位为工作图像素，须为有限正数。
    double ransac_reprojection_threshold = 3.0;
    /// 内点数占去重后有效匹配数的最低比例，有限且在 (0, 1]；同时至少需要 6 个内点。
    double min_inlier_ratio = 0.25;

    std::unique_ptr<RegistrationOptionsBase> clone() const override {
        return std::make_unique<SiftRegistrationOptions>(*this);
    }
};

} // 命名空间 mif
