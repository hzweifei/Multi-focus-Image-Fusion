#pragma once

// 多个测试共享的合成输入和断言辅助函数，无需读取外部测试图片。
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <string>
#include <vector>

// 统一通过异常报告失败，让测试入口输出原因并返回非零退出码。
inline void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// 使用固定随机种子生成可复现纹理，叠加文字以同时覆盖细节和轮廓。
// 默认采用奇数尺寸，用于检查金字塔缩放和重建中的尺寸处理。
inline cv::Mat texture(int rows = 129, int cols = 193) {
    cv::Mat noise(rows, cols, CV_8U);
    cv::RNG random(20260928);
    random.fill(noise, cv::RNG::UNIFORM, 20, 235);
    cv::GaussianBlur(noise, noise, {3, 3}, 0.6);
    cv::putText(noise, "FOCUS", {14, rows / 2}, cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(245), 2);
    return noise;
}

// 构造左右清晰区域互补的两张图；sharp 同时作为融合结果的参考真值。
inline std::vector<cv::Mat> focusStack(const cv::Mat& sharp) {
    cv::Mat blurred;
    cv::GaussianBlur(sharp, blurred, {0, 0}, 2.8);
    cv::Mat a = blurred.clone(), b = blurred.clone();
    const cv::Rect left(0, 0, sharp.cols / 2, sharp.rows);
    const cv::Rect right(sharp.cols / 2, 0, sharp.cols - sharp.cols / 2, sharp.rows);
    sharp(left).copyTo(a(left)); sharp(right).copyTo(b(right));
    return {a, b};
}

// 按全部像素和通道计算平均绝对误差，便于比较原图和融合图与真值的距离。
inline double mae(const cv::Mat& a, const cv::Mat& b) {
    return cv::norm(a, b, cv::NORM_L1) / (a.total() * a.channels());
}

