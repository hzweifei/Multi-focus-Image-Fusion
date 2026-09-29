#pragma once

#include <mif/fusion/guided_filter_options.hpp>
#include <mif/fusion/laplacian_pyramid_options.hpp>
#include <mif/fusion/dct_options.hpp>
#include <mif/fusion/dtcwt_options.hpp>
#include <mif/fusion/gfgfgf_options.hpp>

namespace mif {

/// 传统融合方法；各方法独立决定评分、系数选择与重建方式，不使用 AI 模型。
enum class FusionMethod {
    GuidedFilter,     ///< 分解为基础层与细节层，分别加权后重建。
    LaplacianPyramid, ///< 对拉普拉斯金字塔的各尺度分量加权，再逐层重建。
    Dct,              ///< 沿用参考项目标识；实际按空间域块方差选帧，再做中值一致性检查。
    Dtcwt,            ///< 双树复小波：低频均值、六方向高频选取，再逆变换重建。
    Gfgfgf            ///< 梯度筛帧、局部差异响应与两阶段引导滤波加权融合。
};

/// 融合入口配置；只校验和使用 method 选中的参数组。
/// 未选中的配置会保留，但不会影响当前计算，也不会因其中的无效值而报错。
struct FusionOptions {
    /// 融合方法，默认使用基础层与细节层分开加权的引导滤波方案。
    FusionMethod method = FusionMethod::GuidedFilter;
    /// 引导滤波融合参数，仅 method 为 GuidedFilter 时生效。
    GuidedFilterOptions guided_filter;
    /// 拉普拉斯金字塔融合参数，仅 method 为 LaplacianPyramid 时生效。
    LaplacianPyramidOptions laplacian_pyramid;
    /// 块方差融合参数，仅 method 为 Dct 时生效。
    DctOptions dct;
    /// 双树复小波融合参数，仅 method 为 Dtcwt 时生效。
    DtcwtOptions dtcwt;
    /// GFG-FGF 融合参数，仅 method 为 Gfgfgf 时生效。
    GfgfgfOptions gfgfgf;
    /// 是否保留所选方法提供的权重诊断图；关闭可减少返回结果占用的内存。
    bool keep_weight_maps = false;
};

} // 命名空间 mif

