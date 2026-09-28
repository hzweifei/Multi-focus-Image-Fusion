#pragma once

#include <mif/fusion.hpp>
#include <memory>

namespace mif::detail::registration {

/// 将归一化浮点图像配准到第一张图像，更新结果中的 crop 和 transforms。
/// 统一负责缩放、矩阵检查、原图重采样与共同有效区域裁剪；失败或取消时抛出异常。
/// images 至少含两张同尺寸同类型图像，options 已校验，result 应为新建结果。
void alignImages(std::vector<cv::Mat>& images, const FusionOptions& options,
                 FusionResult& result, const ProgressCallback& progress);

/// 内部变换估计接口：每次融合创建一个对象，可以缓存参考图或参考特征。
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
std::unique_ptr<Estimator> makeEccEstimator(const cv::Mat& reference_gray, const FusionOptions& options);
/// SIFT + 描述子匹配 + RANSAC 单应性估计，参考特征仅提取一次。
std::unique_ptr<Estimator> makeHomographyEstimator(const cv::Mat& reference_gray, const FusionOptions& options);

} // 命名空间 mif::detail::registration
