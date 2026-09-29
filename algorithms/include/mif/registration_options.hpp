#pragma once

namespace mif {

/// 以第一张图像为参考的配准方法；方法决定怎样估计图像之间的几何关系。
enum class RegistrationMethod {
    None = 0, ///< 跳过变换，返回各输入的独立副本。
    Ecc = 1,  ///< 根据灰度相关性优化变换，需要较好的初始对齐。
    Sift = 2  ///< SIFT 特征匹配 + RANSAC，固定估计单应性变换。
};

/// ECC 使用的运动模型；平移、仿射和单应性是同一配准方法的不同几何约束。
enum class MotionModel {
    Translation = 0, ///< 仅估计水平和垂直位移。
    Affine = 1,      ///< 六参数仿射，支持平移、旋转、缩放和剪切。
    Homography = 2   ///< 八自由度单应性，支持平面透视变化。
};

/// 独立配准参数。registerImages() 校验所有字段，包括当前方法未使用的字段。
struct RegistrationOptions {
    /// 默认跳过配准；开启后裁掉无法被所有输入共同覆盖的边缘。
    RegistrationMethod method = RegistrationMethod::None;
    /// 仅 ECC 根据此字段选择模型；SIFT 固定使用单应性，None 不执行变换。
    /// 所有方法均检查该枚举是否有效，即使当前方法不使用它。
    MotionModel motion_model = MotionModel::Translation;
    /// 每张图像 ECC 配准的最大迭代次数，[1, 10000]。
    int iterations = 150;
    /// ECC 相关系数收敛阈值，必须为有限正数。
    double epsilon = 1e-5;
    /// 估计变换时的最长边上限，[16, 8192]；不会放大小图，短边须至少为 16。
    /// 最终重采样在原始分辨率上执行，适用于所有配准方法。
    int max_size = 1200;
    /// SIFT 最多保留的特征数，[64, 100000]；仅 Sift 方法使用。
    int max_features = 4000;
    /// 最近邻/次近邻描述子距离比值阈值，有限且位于 (0, 1)；越小筛选越严格。
    double match_ratio = 0.75;
    /// RANSAC 重投影误差阈值，工作分辨率下的像素数，必须为有限正数。
    double ransac_threshold = 3.0;
    /// RANSAC 内点占有效匹配的最低比例，有限且位于 (0, 1]；还须至少 6 个内点。
    double min_inlier_ratio = 0.25;
};

} // 命名空间 mif
