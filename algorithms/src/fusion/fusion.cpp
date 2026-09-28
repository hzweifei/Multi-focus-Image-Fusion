#include "fusion/fusion.hpp"
#include <stdexcept>

namespace mif::detail::fusion {

MethodResult run(const std::vector<cv::Mat>& images,
                 const FusionOptions& options, const ProgressCallback& progress) {
    // 方法只在这里集中分派，各自文件负责权重生成和重建，便于独立阅读与维护。
    switch (options.method) {
    case FusionMethod::GuidedFilter:
        return guidedFilterFusion(images, options, progress);
    case FusionMethod::LaplacianPyramid:
        return laplacianPyramidFusion(images, options, progress);
    default:
        throw std::invalid_argument("Unknown fusion method");
    }
}

} // 命名空间 mif::detail::fusion
