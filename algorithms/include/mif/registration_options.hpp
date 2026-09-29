#pragma once

namespace mif {

/// 以第一张图像为参考的配准模式；ECC 根据灰度相关性求解，SIFT 根据特征匹配求解。
enum class Alignment {
    None = 0,               ///< 跳过变换，返回各输入的独立副本。
    Translation = 1,        ///< ECC 平移，仅估计水平和垂直位移。
    Affine = 2,             ///< ECC 六参数仿射，支持平移、旋转、缩放和剪切。
    FeatureHomography = 3,  ///< SIFT 特征匹配 + RANSAC，估计八自由度单应性。
    EccHomography = 4       ///< ECC 单应性，需要较好的初始对齐。
};

/// 独立配准参数。registerImages() 校验所有字段，包括当前方法未使用的字段。
struct RegistrationOptions {
    /// 默认跳过配准；开启后裁掉无法被所有输入共同覆盖的边缘。
    Alignment method = Alignment::None;
    /// 每张图像 ECC 配准的最大迭代次数，[1, 10000]。
    int iterations = 150;
    /// ECC 相关系数收敛阈值，必须为有限正数。
    double epsilon = 1e-5;
    /// 估计变换时的最长边上限，[16, 8192]；不会放大小图，短边须至少为 16。
    /// 最终重采样在原始分辨率上执行，适用于所有配准方法。
    int max_size = 1200;
    /// SIFT 最多保留的特征数，[64, 100000]；仅 FeatureHomography 使用。
    int max_features = 4000;
    /// 最近邻/次近邻描述子距离比值阈值，有限且位于 (0, 1)；越小筛选越严格。
    double match_ratio = 0.75;
    /// RANSAC 重投影误差阈值，工作分辨率下的像素数，必须为有限正数。
    double ransac_threshold = 3.0;
    /// RANSAC 内点占有效匹配的最低比例，有限且位于 (0, 1]；还须至少 6 个内点。
    double min_inlier_ratio = 0.25;
};

} // 命名空间 mif
