#pragma once

#include <opencv2/core.hpp>

namespace mif::detail::fusion {

/// 官方内部采用 float32 统计；过小正则项会在平坦区域被舍去，造成除零。
/// 统一要求 epsilon 位于 [1e-6, FLT_MAX]，同时避免转成 float 时溢出。
/// 各使用该算子的方法在处理前调用。
void validateGuidedEpsilon(double epsilon);

/// 官方 ximgproc 灰度引导滤波的权重适配：用 guide 约束 input，输出裁到 [0, 1]。
/// 两个输入应为同尺寸 CV_32FC1，值域 [0, 1]；半径和正则项由调用方法校验。
/// 不修改输入；本算子只负责权重滤波，不执行图像分解或完整融合。
cv::Mat guidedFilter(const cv::Mat& guide, const cv::Mat& input, int radius, double epsilon);

/// 为同一引导图、半径和正则项预计算低分辨率统计，供多张响应图重复使用。
/// 引导图须为非空 CV_32FC1；对象浅持有其缓冲区，使用期间调用方不得修改该缓冲区。
/// 缓存只读，引导图和输入均不被修改；每次 filter 的临时数据及输出独立分配。
/// 建议在单帧内复用，用完即释放，避免为整个图像栈长期保存统计量。
class FastGuidedFilter {
public:
    /// 参数范围由方法入口校验；下采样尺寸、半径取整和反射边界与一次性接口相同。
    FastGuidedFilter(const cv::Mat& guide, int radius, double epsilon, int subsample);
    /// 输入须与引导图同尺寸、为 CV_32FC1；输出保持有符号响应，不裁剪或归一化。
    cv::Mat filter(const cv::Mat& input) const;

private:
    cv::Mat guide_;       ///< 原分辨率引导图，只读共享，恢复输出时保留原图细节。
    cv::Mat small_guide_; ///< 下采样后的 CV_64F 引导图。
    cv::Mat mean_guide_;  ///< 与输入响应无关的局部均值。
    cv::Mat denominator_; ///< 非负局部方差加 epsilon，重复滤波时不再计算。
    int factor_;
    int small_radius_;
};

/// 快速引导滤波：低分辨率拟合并平均线性系数，上采样系数后结合完整引导图。
/// 输入为同尺寸 CV_32FC1；使用双精度统计，输出 CV_32F，保留有符号响应。
/// 半径、正则项、下采样倍数由方法入口校验；此算子不裁剪或归一化权重。
/// 一次性调用创建临时预计算对象；同一引导图的多路输入可直接复用 FastGuidedFilter。
cv::Mat fastGuidedFilter(const cv::Mat& guide, const cv::Mat& input,
                         int radius, double epsilon, int subsample);

} // 命名空间 mif::detail::fusion
