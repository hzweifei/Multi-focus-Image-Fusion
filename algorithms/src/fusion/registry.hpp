#pragma once

#include <opencv2/core.hpp>
#include <vector>
#include <mif/fusion/options_base.hpp>
#include <mif/progress.hpp>
#include <mif/export.hpp>
#include <functional>
#include <typeindex>
#include <type_traits>
#include <stdexcept>

namespace mif::detail::fusion {

/// 各方法独立生成的结果；公共入口只整理位深和返回数据，不规定方法的中间步骤。
struct MethodResult {
    /// 必填：与输入尺寸、通道数相同的 CV_32F 图像。
    /// 分层重建可产生少量越界值，由公共入口裁到 [0, 1] 并恢复输入位深。
    cv::Mat image;
    /// 可选：方法提供的 CV_32SC1 来源索引图；无对应诊断时为空。
    /// 具体语义由各方法决定；DTCWT 的多尺度系数选择不伪造单一来源图。
    cv::Mat source_index_map;
    /// 可选：方法提供的逐图 CV_32FC1 权重；公共入口不依赖它来计算索引。
    /// 提供时须全分辨率、非负、逐像素归一化，顺序与原始输入一致。
    std::vector<cv::Mat> weight_maps;
};

/// 注册项只保存无状态处理函数，每次执行的可变状态由方法在调用内管理。
struct FusionMethodEntry {
    std::function<void(const FusionOptionsBase&)> validate;
    std::function<MethodResult(const std::vector<cv::Mat>&, const FusionOptionsBase&,
                               const ProgressCallback&)> run;
};

// 注册表在核心库内只有一份；不随 SDK 安装这个私有扩展头。
MIF_EXPORT void addFusionMethod(std::type_index type, FusionMethodEntry entry);
MIF_EXPORT FusionMethodEntry findFusionMethod(const FusionOptionsBase& options);

/// 用同一参数类型绑定校验与计算，避免方法标识与配置不匹配。
/// 同一类型重复注册会报错；注册的函数必须在整个使用期间有效。
template<class Options>
void registerFusionMethod(void (*validate)(const Options&),
                          MethodResult (*run)(const std::vector<cv::Mat>&, const Options&,
                                              const ProgressCallback&)) {
    static_assert(std::is_base_of_v<FusionOptionsBase, Options>);
    if (!validate || !run) throw std::invalid_argument("Fusion method requires validation and execution functions");
    addFusionMethod(typeid(Options), {
        [validate](const FusionOptionsBase& options) { validate(dynamic_cast<const Options&>(options)); },
        [run](const std::vector<cv::Mat>& images, const FusionOptionsBase& options,
              const ProgressCallback& progress) { return run(images, dynamic_cast<const Options&>(options), progress); }
    });
}

} // 命名空间 mif::detail::fusion
