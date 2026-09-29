#include "registration/registration.hpp"
#include <mif/registration_options.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>

namespace mif::detail::registration {
namespace {

/// 对工作分辨率灰度图做一次固定平滑，并拒绝缺乏灰度变化的图像。
/// 输入类型、尺寸及 [0, 1] 范围由公共配准管线保证；本函数不修改输入。
cv::Mat prepare(const cv::Mat& gray, const char* role) {
    cv::Mat smoothed;
    // 不同焦点会改变高频细节，适度平滑让 ECC 更多地依据共有结构估计运动。
    // 平滑不能消除所有失焦差异；明显不同的清晰区域仍可能使 ECC 求解失败。
    cv::GaussianBlur(gray, smoothed, {5, 5}, 1.2);
    cv::Scalar mean, deviation;
    cv::meanStdDev(smoothed, mean, deviation);
    if (!std::isfinite(deviation[0]) || deviation[0] < 1e-6)
        throw std::runtime_error(std::string("ECC cannot align a textureless ") + role + " image");
    return smoothed;
}

/// 将公开运动模型转换为 OpenCV 的 ECC 约束，不把未知枚举静默当作某种模型。
int motionType(MotionModel model) {
    switch (model) {
    case MotionModel::Translation: return cv::MOTION_TRANSLATION;
    case MotionModel::Affine: return cv::MOTION_AFFINE;
    case MotionModel::Homography: return cv::MOTION_HOMOGRAPHY;
    default: throw std::invalid_argument("The ECC estimator requires a valid motion model");
    }
}

/// 缓存已平滑的参考图，每次为一张源图独立求解，避免上一张图的结果影响下一张。
/// 返回矩阵采用参考坐标到源图坐标的方向，供公共管线反向采样。
class EccEstimator final : public Estimator {
public:
    EccEstimator(const cv::Mat& reference_gray, const RegistrationOptions& options)
        : motion_(motionType(options.motion_model)),
          reference_(prepare(reference_gray, "reference")),
          criteria_(cv::TermCriteria::COUNT | cv::TermCriteria::EPS,
                    options.iterations, options.epsilon) {}

    cv::Mat estimate(const cv::Mat& source_gray) override {
        // 参考图和源图均检查，避免让常量源图进入相关系数计算后出现无效数值。
        const cv::Mat source = prepare(source_gray, "source");
        const int rows = motion_ == cv::MOTION_HOMOGRAPHY ? 3 : 2;
        // OpenCV ECC 使用单精度矩阵迭代；统一从单位矩阵开始，没有隐式方法回退。
        cv::Mat warp = cv::Mat::eye(rows, 3, CV_32F);
        double correlation;
        try {
            // 两张图已在 prepare 中平滑，显式将 ECC 内部高斯窗口设为 1，避免重复平滑。
            // 不提供掩码：公共管线传入完整的工作图像，几何有效区域在求解后统一处理。
            correlation = cv::findTransformECC(reference_, source, warp, motion_, criteria_,
                                                cv::noArray(), 1);
        } catch (const cv::Exception& error) {
            // 保留 OpenCV 的失败原因；上层管线负责补充输入序号，不静默改用其他模型。
            throw std::runtime_error(std::string("ECC alignment failed: ") + error.what());
        }
        // 有返回矩阵并不代表得到有意义的相关性；非有限或非正分数一律视为失败。
        // 正分数只表示通过基本检查，矩阵几何合法性还需由公共管线验证。
        if (!std::isfinite(correlation) || correlation <= 0.0)
            throw std::runtime_error("ECC alignment returned a non-finite or non-positive correlation");

        // 对外统一为双精度 3×3 齐次矩阵。平移和仿射补齐末行 [0, 0, 1]；
        // 单应性保留完整三行，缩放回原始坐标及几何检查由公共管线完成。
        cv::Mat transform = cv::Mat::eye(3, 3, CV_64F);
        cv::Mat estimated_rows = transform.rowRange(0, rows);
        warp.convertTo(estimated_rows, CV_64F);
        return transform;
    }

private:
    int motion_;
    cv::Mat reference_;
    cv::TermCriteria criteria_;
};

} // 匿名命名空间

/// 创建 ECC 估计器并立即检查参考图；参考图由平滑后的独立缓冲区持有。
std::unique_ptr<Estimator> makeEccEstimator(const cv::Mat& reference_gray,
                                             const RegistrationOptions& options) {
    return std::make_unique<EccEstimator>(reference_gray, options);
}

} // 命名空间 mif::detail::registration
