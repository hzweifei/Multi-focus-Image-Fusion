#include "focus_measure.hpp"
#include <opencv2/imgproc.hpp>

namespace mif::detail {

cv::Mat grayscale(const cv::Mat& image) {
    // 灰度图无需复制；此返回值只用于后续只读计算。
    if (image.channels() == 1) return image;
    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    return gray;
}

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

} // 命名空间 mif::detail

