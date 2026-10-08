#include "fixtures.hpp"
#include <mif/fusion.hpp>
#include <functional>
#include <limits>
#include <type_traits>

namespace {

/// 统一把各方法所用的正则项设置为同一测试值，保持公开参数组独立。
template<class Options>
void setEpsilon(Options& options, double epsilon) {
    if constexpr (std::is_same_v<Options, mif::GuidedFilterFusionOptions>) {
        options.base_epsilon = epsilon;
        options.detail_epsilon = epsilon;
    } else if constexpr (std::is_same_v<Options, mif::LaplacianPyramidFusionOptions>) {
        options.detail_epsilon = epsilon;
    } else {
        options.guided_epsilon = epsilon;
    }
}

} // 匿名命名空间

/// OpenCV float32 协方差计算会舍去过小的正则项；平坦引导图曾出现除零和 NaN。
/// 在公开入口验证参数保护与最小合法值，而不是重新实现公式来测试公式本身。
void testGuidedFilterNumerics() {
    const cv::Mat constant(25, 37, CV_32F, cv::Scalar(0.5));
    auto check = [&](auto options) {
        options.include_weight_maps = true;
        for (double epsilon : {1e-100, 1e-8, 0.0, -1.0, std::numeric_limits<double>::max(),
                                std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            setEpsilon(options, epsilon);
            bool rejected = false, reported = false;
            try {
                mif::fuse({constant, constant}, options, [&](int, const std::string&) {
                    reported = true;
                    return true;
                });
            } catch (const std::invalid_argument&) { rejected = true; }
            require(rejected && !reported, "Unsafe guided epsilon was not rejected before processing");
        }
        for (int radius : {1, 3, 15, 255}) {
            setEpsilon(options, 1e-6);
            using Options = decltype(options);
            if constexpr (std::is_same_v<Options, mif::GuidedFilterFusionOptions>) {
                options.base_radius = radius;
                options.detail_radius = radius;
            } else if constexpr (std::is_same_v<Options, mif::LaplacianPyramidFusionOptions>) {
                options.detail_radius = radius;
            } else {
                options.guided_radius = radius;
                options.selection_ratio = 0;
            }
            const auto averaged = mif::fuse({constant * 0.4f, constant, constant * 1.6f}, options);
            require(cv::checkRange(averaged.image), "Minimum legal epsilon produced nonfinite output");
            require(cv::norm(averaged.image, constant, cv::NORM_INF) < 2e-5,
                    "Minimum legal epsilon changed the mean of textureless inputs");
            cv::Mat near_constant = constant.clone();
            near_constant(cv::Rect(0, 0, 18, 25)).setTo(0.50001f);
            cv::Mat local_flat = constant.clone();
            local_flat(cv::Rect(0, 0, 18, 25)).setTo(0.125f);
            local_flat(cv::Rect(18, 0, 19, 25)).setTo(0.875f);
            for (const auto& image : {near_constant, local_flat}) {
                const auto result = mif::fuse({image, image}, options);
                require(cv::checkRange(result.image) && cv::norm(result.image, image, cv::NORM_INF) < 2e-5,
                        "Guided filter corrupted repeated near-constant or locally flat inputs");
                cv::Mat total = cv::Mat::zeros(image.size(), CV_32F);
                for (const auto& weight : result.weight_maps) {
                    require(cv::checkRange(weight, true, nullptr, 0, 1.00001),
                            "Guided filter returned invalid diagnostic weights");
                    total += weight;
                }
                require(cv::norm(total, cv::Mat(image.size(), CV_32F, cv::Scalar(1)), cv::NORM_INF) < 1e-5,
                        "Guided filter weights lost normalization at the epsilon boundary");
            }
        }
        setEpsilon(options, std::numeric_limits<float>::max());
        require(cv::checkRange(mif::fuse({constant, constant}, options).image),
                "Largest representable guided epsilon failed");
    };
    check(mif::GuidedFilterFusionOptions{});
    check(mif::LaplacianPyramidFusionOptions{});
    check(mif::GfgFgfFusionOptions{});
    // GFF 两个正则项独立校验；不能只检查细节参数而漏掉基础参数。
    mif::GuidedFilterFusionOptions base_only;
    base_only.base_epsilon = 1e-8;
    bool rejected = false;
    try { mif::fuse({constant, constant}, base_only); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Unsafe base-layer epsilon was accepted");
}
