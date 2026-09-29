#pragma once

#include <mif/registration_options.hpp>
#include <opencv2/core.hpp>
#include <memory>

namespace mif::detail::registration {

/// 内部变换估计接口：每次配准创建一个对象，可以缓存参考图或参考特征。
/// 输入为工作分辨率下、同尺寸的 CV_32FC1 灰度图，值域 [0, 1]。
/// 实现只估计变换，不裁剪或重采样原图，也不持有跨任务的可变全局状态。
class Estimator {
public:
    virtual ~Estimator() = default;
    /// 返回参考坐标到源图坐标的 3×3 CV_64F 齐次矩阵；无法可靠求解时必须抛异常。
    /// 平移/仿射也补齐末行 [0, 0, 1]，使公共流程与具体求解方法解耦。
    virtual cv::Mat estimate(const cv::Mat& source_gray) = 0;
};

/// ECC 灰度优化，模型由 Translation / Affine / EccHomography 决定。
std::unique_ptr<Estimator> makeEccEstimator(const cv::Mat& reference_gray, const RegistrationOptions& options);
/// SIFT + 描述子匹配 + RANSAC 单应性估计，参考特征仅提取一次。
std::unique_ptr<Estimator> makeHomographyEstimator(const cv::Mat& reference_gray, const RegistrationOptions& options);

} // 命名空间 mif::detail::registration
