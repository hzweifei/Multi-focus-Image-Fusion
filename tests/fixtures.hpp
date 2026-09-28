#pragma once
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <string>
#include <vector>
inline void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
inline cv::Mat texture(int rows = 129, int cols = 193) {
    cv::Mat noise(rows, cols, CV_8U);
    cv::RNG random(20260928);
    random.fill(noise, cv::RNG::UNIFORM, 20, 235);
    cv::GaussianBlur(noise, noise, {3, 3}, 0.6);
    cv::putText(noise, "FOCUS", {14, rows / 2}, cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(245), 2);
    return noise;
}
inline std::vector<cv::Mat> focusStack(const cv::Mat& sharp) {
    cv::Mat blurred;
    cv::GaussianBlur(sharp, blurred, {0, 0}, 2.8);
    cv::Mat a = blurred.clone(), b = blurred.clone();
    const cv::Rect left(0, 0, sharp.cols / 2, sharp.rows);
    const cv::Rect right(sharp.cols / 2, 0, sharp.cols - sharp.cols / 2, sharp.rows);
    sharp(left).copyTo(a(left)); sharp(right).copyTo(b(right));
    return {a, b};
}
inline double mae(const cv::Mat& a, const cv::Mat& b) {
    return cv::norm(a, b, cv::NORM_L1) / (a.total() * a.channels());
}

