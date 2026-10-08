#include <mif/pipeline.hpp>
#include <utility>

namespace mif {

PipelineResult registerAndFuse(const std::vector<cv::Mat>& images,
                               const RegistrationOptionsBase& registration_options,
                               const FusionOptionsBase& fusion_options,
                               const ProgressCallback& progress) {
    // 组合层只协调两个公开入口；校验、计算和结果类型分别由对应模块维护。
    ProgressCallback registration_progress, fusion_progress;
    if (progress) {
        registration_progress = [&progress](int percent, const std::string& stage) {
            // 配准完成只是总流程的中点，不能提前向调用者报告 done。
            return progress(percent * 40 / 100, stage == "done" ? "align" : stage);
        };
        fusion_progress = [&progress](int percent, const std::string& stage) {
            return progress(40 + percent * 60 / 100, stage);
        };
    }
    auto registered = registerImages(images, registration_options, registration_progress);
    PipelineResult result;
    result.fusion = fuse(registered.images, fusion_options, fusion_progress);
    result.crop_region = registered.crop_region;
    result.transforms = std::move(registered.transforms);
    // registered.images 在本函数返回时释放，不让组合结果额外持有整批中间图像。
    return result;
}

} // 命名空间 mif
