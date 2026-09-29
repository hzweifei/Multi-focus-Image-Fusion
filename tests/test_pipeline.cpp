#include "fixtures.hpp"
#include <mif/pipeline.hpp>
#include <utility>

namespace {

// 比较全部公开结果，防止组合流程偷偷换用另一套中间图像或丢失诊断数据。
void equalResults(const mif::FusionResult& a, const mif::FusionResult& b) {
    require(a.image.type() == b.image.type() && a.image.size() == b.image.size(), "Pipeline changed output format");
    require(cv::norm(a.image, b.image, cv::NORM_INF) == 0, "Pipeline differs from explicit two-stage fusion");
    require(cv::norm(a.focus_indices, b.focus_indices, cv::NORM_INF) == 0, "Pipeline changed focus indices");
    require(a.weights.size() == b.weights.size(), "Pipeline lost weight maps");
    for (size_t i = 0; i < a.weights.size(); ++i)
        require(cv::norm(a.weights[i], b.weights[i], cv::NORM_INF) == 0, "Pipeline changed weight maps");
}

} // 匿名命名空间

void testPipeline() {
    // 同一批次使用分数像素位移，覆盖整数重采样量化及浮点两种情况。
    const auto reference = texture(97, 129);
    cv::Mat shifted;
    const cv::Mat transform = (cv::Mat_<float>(2, 3) << 1, 0, 1.25, 0, 1, -1.5);
    cv::warpAffine(reference, shifted, transform, reference.size(), cv::INTER_LINEAR, cv::BORDER_REFLECT_101);
    // 算法与模型分别设置，组合入口需要保持各条配准路径的图像和矩阵格式。
    const std::vector<std::pair<mif::RegistrationMethod, mif::MotionModel>> registration_cases{
        {mif::RegistrationMethod::None, mif::MotionModel::Translation},
        {mif::RegistrationMethod::Ecc, mif::MotionModel::Translation},
        {mif::RegistrationMethod::Ecc, mif::MotionModel::Affine},
        {mif::RegistrationMethod::Ecc, mif::MotionModel::Homography},
        {mif::RegistrationMethod::Sift, mif::MotionModel::Translation}};
    for (const int depth : {CV_8U, CV_16U, CV_32F}) {
        cv::Mat first, second;
        const double scale = depth == CV_16U ? 257.0 : depth == CV_32F ? 1.0 / 255.0 : 1.0;
        reference.convertTo(first, depth, scale);
        shifted.convertTo(second, depth, scale);
        const std::vector<cv::Mat> images{first, second};
        for (const auto& [registration_method, motion_model] : registration_cases) {
            mif::RegistrationOptions registration;
            registration.method = registration_method;
            registration.motion_model = motion_model;
            const auto registered = mif::registerImages(images, registration);
            for (const auto method : {mif::FusionMethod::GuidedFilter, mif::FusionMethod::LaplacianPyramid}) {
                mif::FusionOptions fusion;
                fusion.method = method;
                fusion.keep_weight_maps = true;
                // 两种方法保存不同的非默认参数，组合入口必须传递完整快照。
                fusion.guided_filter.focus = {mif::FocusMeasure::Tenengrad, 5};
                fusion.guided_filter.base_radius = 7;
                fusion.guided_filter.detail_radius = 2;
                fusion.guided_filter.base_epsilon = 0.025;
                fusion.guided_filter.detail_epsilon = 0.0004;
                fusion.laplacian_pyramid.focus = {mif::FocusMeasure::ModifiedLaplacian, 11};
                fusion.laplacian_pyramid.detail_radius = 4;
                fusion.laplacian_pyramid.detail_epsilon = 0.0002;
                fusion.laplacian_pyramid.levels = 3;
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
                require(combined.crop == registered.crop, "Pipeline changed registration crop");
                require(combined.transforms.size() == registered.transforms.size(), "Pipeline lost transforms");
                for (size_t i = 0; i < combined.transforms.size(); ++i)
                    require(cv::norm(combined.transforms[i], registered.transforms[i], cv::NORM_INF) == 0,
                            "Pipeline changed registration transform");
                // None 流程与直接融合也必须一致，且纯融合不会出现配准阶段。
                if (registration_method == mif::RegistrationMethod::None) {
                    const auto direct = mif::fuse(images, fusion, [](int, const std::string& stage) {
                        require(stage != "align", "Pure fusion invoked registration");
                        return true;
                    });
                    equalResults(combined.fusion, direct);
                }
            }
        }
    }

    mif::RegistrationOptions registration;
    registration.method = mif::RegistrationMethod::Ecc;
    registration.motion_model = mif::MotionModel::Translation;
    const std::vector<cv::Mat> images{reference, shifted};
    // 组合层不能吞掉任一阶段的取消，也不能把调用者异常换成处理失败。
    struct CallbackFailure : std::runtime_error { using std::runtime_error::runtime_error; };
    for (const std::string stage_to_stop : {"align", "focus"}) {
        bool cancelled = false, propagated = false;
        try {
            mif::registerAndFuse(images, registration, {}, [&](int, const std::string& stage) {
                return stage != stage_to_stop;
            });
        } catch (const mif::Cancelled&) { cancelled = true; }
        require(cancelled, "Pipeline ignored cancellation in " + stage_to_stop);
        try {
            mif::registerAndFuse(images, registration, {}, [&](int, const std::string& stage) {
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
        mif::registerAndFuse({flat, flat}, registration, {}, [&](int, const std::string& stage) {
            fusion_started |= stage == "focus";
            return true;
        });
    } catch (const std::runtime_error&) { rejected = true; }
    require(rejected && !fusion_started, "Pipeline fused a failed registration");
}
