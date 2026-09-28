#include "fusion/focus_measure.hpp"
#include "fusion/weight_map.hpp"
#include "common/grayscale.hpp"
#include "common/progress.hpp"
#include <opencv2/imgproc.hpp>

namespace mif::detail::fusion {

cv::Mat focusMeasure(const cv::Mat& gray, FocusMeasure method, int window) {
    cv::Mat x, y, energy;
    if (method == FocusMeasure::Tenengrad) {
        // Tenengrad：E = Gx² + Gy²，以一阶梯度的平方和衡量局部边缘强度。
        cv::Sobel(gray, x, CV_32F, 1, 0, 3);
        cv::Sobel(gray, y, CV_32F, 0, 1, 3);
        energy = x.mul(x) + y.mul(y);
    } else {
        // 改进拉普拉斯：E = |Dxx| + |Dyy|。先分别取绝对值再求和，
        // 防止两个方向的二阶响应正负抵消。核的整体正负号不影响该指标。
        const cv::Mat kernel = (cv::Mat_<float>(1, 3) << -1, 2, -1);
        cv::filter2D(gray, x, CV_32F, kernel);
        cv::filter2D(gray, y, CV_32F, kernel.t());
        energy = cv::abs(x) + cv::abs(y);
    }
    // 使用局部均值而非单像素响应，减小噪声造成的零散决策。
    // 上述滤波使用 OpenCV 默认的反射边界，避免补零在图像边缘引入虚假高响应。
    cv::boxFilter(energy, energy, CV_32F, {window, window});
    return energy;
}

FocusMaps prepareFocusMaps(const std::vector<cv::Mat>& images,
                           const FusionOptions& options, const ProgressCallback& progress) {
    FocusMaps maps;
    std::vector<cv::Mat> scores;
    // 彩色图仅用灰度比较清晰度和引导权重；各方法的重建仍使用全部颜色通道。
    for (size_t i = 0; i < images.size(); ++i) {
        report(progress, 30 + static_cast<int>(20 * i / images.size()), "focus");
        maps.guides.push_back(grayscale(images[i]));
        scores.push_back(focusMeasure(maps.guides.back(), options.focus_measure, options.focus_window));
    }
    maps.decisions = decisionWeights(scores);
    // 生成决策后不再需要清晰度响应，及时释放其图像缓冲区，降低多图融合的峰值内存。
    scores.clear();
    return maps;
}

} // 命名空间 mif::detail::fusion

