#pragma once

#include <mif/registration/options_base.hpp>

namespace mif {

/// ECC 使用的几何约束；平移、仿射和单应性是同一种求解方法的不同模型。
enum class MotionModel {
    Translation = 0, ///< 两参数平移，仅改变水平和垂直位置。
    Affine = 1,      ///< 六参数仿射，允许平移、旋转、缩放和剪切。
    Homography = 2   ///< 八自由度单应性，可描述平面透视变化。
};

/// 根据灰度相关性迭代估计变换；适合初始位置接近且具有共同结构的图像。
struct EccRegistrationOptions final : RegistrationOptionsBase {
    /// 运动模型，默认仅平移；仿射和单应性允许更多几何变化。
    MotionModel motion_model = MotionModel::Translation;
    /// 每张源图的迭代次数上限，[1, 10000]。
    int max_iterations = 150;
    /// 相邻迭代相关系数的变化阈值，须为有限正数；不是像素对齐误差。
    double convergence_tolerance = 1e-5;

    std::unique_ptr<RegistrationOptionsBase> clone() const override {
        return std::make_unique<EccRegistrationOptions>(*this);
    }
};

} // 命名空间 mif
