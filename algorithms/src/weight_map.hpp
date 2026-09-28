#pragma once
#include <opencv2/core.hpp>
#include <vector>
namespace mif::detail {
cv::Mat guidedFilter(const cv::Mat& guide, const cv::Mat& input, int radius, double epsilon);
std::vector<cv::Mat> decisionWeights(const std::vector<cv::Mat>& scores);
void normalizeWeights(std::vector<cv::Mat>& weights);
cv::Mat dominantIndices(const std::vector<cv::Mat>& weights);
cv::Mat expandWeight(const cv::Mat& weight, int channels);
}

