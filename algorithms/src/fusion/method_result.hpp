#pragma once

#include <opencv2/core.hpp>
#include <vector>

namespace mif::detail::fusion {

/// 各方法独立生成的结果；公共入口只整理位深和返回数据，不规定方法的中间步骤。
struct MethodResult {
    /// 必填：与输入尺寸、通道数相同的 CV_32F 图像。
    /// 分层重建可产生少量越界值，由公共入口裁到 [0, 1] 并恢复输入位深。
    cv::Mat image;
    /// 可选：方法提供的 CV_32SC1 来源索引图；无对应诊断时为空。
    /// 具体语义由各方法决定；DTCWT 的多尺度系数选择不伪造单一来源图。
    cv::Mat focus_indices;
    /// 可选：方法提供的逐图 CV_32FC1 权重；公共入口不依赖它来计算索引。
    /// 提供时须全分辨率、非负、逐像素归一化，顺序与原始输入一致。
    std::vector<cv::Mat> weights;
};

} // 命名空间 mif::detail::fusion
