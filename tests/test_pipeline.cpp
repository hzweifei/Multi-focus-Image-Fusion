#include "fixtures.hpp"
#include <mif/pipeline.hpp>
#include <utility>

namespace {

// 比较全部公开结果，防止组合流程偷偷换用另一套中间图像或丢失诊断数据。
void equalResults(const mif::FusionResult& a, const mif::FusionResult& b) {
    require(a.image.type() == b.image.type() && a.image.size() == b.image.size(), "Pipeline changed output format");
    require(cv::norm(a.image, b.image, cv::NORM_INF) == 0, "Pipeline differs from explicit two-stage fusion");
    require(cv::norm(a.source_index_map, b.source_index_map, cv::NORM_INF) == 0, "Pipeline changed focus indices");
    require(a.weight_maps.size() == b.weight_maps.size(), "Pipeline lost weight maps");
    for (size_t i = 0; i < a.weight_maps.size(); ++i)
        require(cv::norm(a.weight_maps[i], b.weight_maps[i], cv::NORM_INF) == 0, "Pipeline changed weight maps");
}

} // 匿名命名空间

void testPipeline() {
    // 同一批次使用分数像素位移，覆盖整数重采样量化及浮点两种情况。
    const auto reference = texture(97, 129);
    cv::Mat shifted;
    const cv::Mat transform = (cv::Mat_<float>(2, 3) << 1, 0, 1.25, 0, 1, -1.5);
    cv::warpAffine(reference, shifted, transform, reference.size(), cv::INTER_LINEAR, cv::BORDER_REFLECT_101);
    // 算法与模型分别设置，组合入口需要保持各条配准路径的图像和矩阵格式。
    std::vector<std::unique_ptr<mif::RegistrationOptionsBase>> registration_cases;
    registration_cases.push_back(std::make_unique<mif::NoRegistrationOptions>());
    for (const auto model : {mif::MotionModel::Translation, mif::MotionModel::Affine, mif::MotionModel::Homography}) {
        auto options = std::make_unique<mif::EccRegistrationOptions>();
        options->motion_model = model;
        registration_cases.push_back(std::move(options));
    }
    registration_cases.push_back(std::make_unique<mif::SiftRegistrationOptions>());
    mif::GuidedFilterFusionOptions guided;
    guided.include_weight_maps = true;
    guided.focus = {mif::FocusMeasure::Tenengrad, 5};
    guided.base_radius = 7;
    guided.detail_radius = 2;
    guided.base_epsilon = 0.025;
    guided.detail_epsilon = 0.0004;
    mif::LaplacianPyramidFusionOptions pyramid;
    pyramid.include_weight_maps = true;
    pyramid.focus = {mif::FocusMeasure::ModifiedLaplacian, 11};
    pyramid.detail_radius = 4;
    pyramid.detail_epsilon = 0.0002;
    pyramid.max_levels = 3;
    const std::vector<const mif::FusionOptionsBase*> fusion_cases{&guided, &pyramid};
    for (const int depth : {CV_8U, CV_16U, CV_32F}) {
        cv::Mat first, second;
        const double scale = depth == CV_16U ? 257.0 : depth == CV_32F ? 1.0 / 255.0 : 1.0;
        reference.convertTo(first, depth, scale);
        shifted.convertTo(second, depth, scale);
        const std::vector<cv::Mat> images{first, second};
        for (const auto& registration_case : registration_cases) {
            const auto& registration = *registration_case;
            const auto registered = mif::registerImages(images, registration);
            for (const auto* fusion_case : fusion_cases) {
                const auto& fusion = *fusion_case;
                const auto separate = mif::fuse(registered.images, fusion);
                int previous = -1, done_count = 0;
                const auto combined = mif::registerAndFuse(images, registration, fusion,
                    [&](int percent, const std::string& stage) {
                        require(percent >= previous && percent <= 100, "Pipeline progress decreased or exceeded 100");
                        if (previous == -1) require(percent == 0, "Pipeline did not start at zero");
                        if (stage == "done") { ++done_count; require(percent == 100, "Premature done in pipeline"); }
                        previous = percent;
                        return true;
                    });
                require(previous == 100 && done_count == 1, "Pipeline completion was lost or duplicated");
                equalResults(combined.fusion, separate);
                require(combined.crop_region == registered.crop_region, "Pipeline changed registration crop");
                require(combined.transforms.size() == registered.transforms.size(), "Pipeline lost transforms");
                for (size_t i = 0; i < combined.transforms.size(); ++i)
                    require(cv::norm(combined.transforms[i], registered.transforms[i], cv::NORM_INF) == 0,
                            "Pipeline changed registration transform");
                // None 流程与直接融合也必须一致，且纯融合不会出现配准阶段。
                if (dynamic_cast<const mif::NoRegistrationOptions*>(&registration) != nullptr) {
                    const auto direct = mif::fuse(images, fusion, [](int, const std::string& stage) {
                        require(stage != "align", "Pure fusion invoked registration");
                        return true;
                    });
                    equalResults(combined.fusion, direct);
                }
            }
        }
    }

    mif::EccRegistrationOptions registration;
    registration.motion_model = mif::MotionModel::Translation;
    const std::vector<cv::Mat> images{reference, shifted};
    // 组合层不能吞掉任一阶段的取消，也不能把调用者异常换成处理失败。
    struct CallbackFailure : std::runtime_error { using std::runtime_error::runtime_error; };
    for (const std::string stage_to_stop : {"align", "focus"}) {
        bool cancelled = false, propagated = false;
        try {
            mif::registerAndFuse(images, registration, mif::GuidedFilterFusionOptions{}, [&](int, const std::string& stage) {
                return stage != stage_to_stop;
            });
        } catch (const mif::Cancelled&) { cancelled = true; }
        require(cancelled, "Pipeline ignored cancellation in " + stage_to_stop);
        try {
            mif::registerAndFuse(images, registration, mif::GuidedFilterFusionOptions{}, [&](int, const std::string& stage) {
                if (stage == stage_to_stop) throw CallbackFailure("pipeline callback sentinel");
                return true;
            });
        } catch (const CallbackFailure& e) {
            propagated = std::string(e.what()) == "pipeline callback sentinel";
        }
        require(propagated, "Pipeline changed callback exception in " + stage_to_stop);
    }

    // 配准失败不会继续融合；纹理不足仍允许直接进行纯融合。
    const cv::Mat flat(32, 32, CV_8U, cv::Scalar(40));
    const auto fused_flat = mif::fuse({flat, flat});
    require(cv::norm(fused_flat.image, flat, cv::NORM_INF) <= 1, "Pure fusion depends on registration texture");
    bool rejected = false, fusion_started = false;
    try {
        mif::registerAndFuse({flat, flat}, registration, mif::GuidedFilterFusionOptions{}, [&](int, const std::string& stage) {
            fusion_started |= stage == "focus";
            return true;
        });
    } catch (const std::runtime_error&) { rejected = true; }
    require(rejected && !fusion_started, "Pipeline fused a failed registration");
}
