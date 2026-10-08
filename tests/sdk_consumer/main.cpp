#include <mif/mif.hpp>
#include <iostream>

// 由独立 CMake 工程链接已交付的 SDK，验证公开头文件、导入库与运行库可配套使用。
// 两张常量 16 位图没有清晰度差异，结果应保留位深并接近等权平均值。
int main() {
    const cv::Mat near(24, 32, CV_16U, cv::Scalar(10000));
    const cv::Mat far(24, 32, CV_16U, cv::Scalar(50000));
    const auto registered = mif::registerImages({near, far});
    const auto result = mif::fuse(registered.images);
    if (result.image.type() != CV_16U ||
        cv::norm(result.image, cv::Mat(near.size(), CV_16U, cv::Scalar(30000)), cv::NORM_INF) > 1)
        return 1;
    const auto pipeline = mif::registerAndFuse({near, far});
    if (pipeline.crop_region != registered.crop_region || pipeline.transforms.size() != 2 ||
        cv::norm(pipeline.fusion.image, result.image, cv::NORM_INF) != 0)
        return 2;
    // 使用新参数布局实际跨越 DLL 边界，验证不同模型的分派和返回矩阵格式。
    cv::Mat texture(129, 193, CV_8U);
    cv::RNG random(20260929);
    random.fill(texture, cv::RNG::UNIFORM, 20, 235);
    mif::EccRegistrationOptions registration;
    for (const auto model : {mif::MotionModel::Translation, mif::MotionModel::Affine,
                            mif::MotionModel::Homography}) {
        registration.motion_model = model;
        const auto aligned = mif::registerImages({texture, texture}, registration);
        const int expected_rows = model == mif::MotionModel::Homography ? 3 : 2;
        if (aligned.images.size() != 2 || aligned.transforms[1].rows != expected_rows ||
            cv::norm(aligned.images[1], texture(aligned.crop_region), cv::NORM_INF) > 1)
            return 3;
    }
    // 五种参数动态类型跨越 DLL 边界，统一入口按类型选择已注册实现。
    mif::GuidedFilterFusionOptions guided;
    guided.focus = {mif::FocusMeasure::Tenengrad, 5};
    guided.base_radius = 7;
    guided.detail_radius = 2;
    guided.base_epsilon = 0.02;
    mif::LaplacianPyramidFusionOptions pyramid;
    pyramid.focus.window_size = 7;
    pyramid.detail_epsilon = 0.0003;
    pyramid.max_levels = 3;
    mif::BlockVarianceFusionOptions block;
    block.block_size = 4;
    block.consistency_window_size = 3;
    mif::DtcwtFusionOptions wavelet;
    wavelet.max_levels = 3;
    mif::GfgFgfFusionOptions gfg;
    gfg.guided_radius = 3;
    for (auto* options : std::vector<mif::FusionOptionsBase*>{&guided, &pyramid, &block, &wavelet, &gfg}) {
        options->include_weight_maps = true;
        const auto snapshot = options->clone();
        const auto fused = mif::fuse({texture, texture}, *snapshot);
        const bool diagnostics_ok = dynamic_cast<const mif::DtcwtFusionOptions*>(snapshot.get())
            ? fused.weight_maps.empty() && fused.source_index_map.empty()
            : fused.weight_maps.size() == 2 && fused.source_index_map.type() == CV_32SC1;
        if (!diagnostics_ok || cv::norm(fused.image, texture, cv::NORM_INF) > 1) return 4;
    }
    std::cout << "Installed SDK: headers, import library and runtime OK\n";
    return 0;
}
