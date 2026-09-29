#include <mif/fusion.hpp>
#include <mif/registration.hpp>
#include <mif/pipeline.hpp>
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
    if (pipeline.crop != registered.crop || pipeline.transforms.size() != 2 ||
        cv::norm(pipeline.fusion.image, result.image, cv::NORM_INF) != 0)
        return 2;
    // 使用新参数布局实际跨越 DLL 边界，验证不同模型的分派和返回矩阵格式。
    cv::Mat texture(129, 193, CV_8U);
    cv::RNG random(20260929);
    random.fill(texture, cv::RNG::UNIFORM, 20, 235);
    mif::RegistrationOptions registration;
    registration.method = mif::RegistrationMethod::Ecc;
    for (const auto model : {mif::MotionModel::Translation, mif::MotionModel::Affine,
                            mif::MotionModel::Homography}) {
        registration.motion_model = model;
        const auto aligned = mif::registerImages({texture, texture}, registration);
        const int expected_rows = model == mif::MotionModel::Homography ? 3 : 2;
        if (aligned.images.size() != 2 || aligned.transforms[1].rows != expected_rows ||
            cv::norm(aligned.images[1], texture(aligned.crop), cv::NORM_INF) > 1)
            return 3;
    }
    // 五种嵌套配置分别穿过 DLL 边界；保留诊断可检查新增字段后的结构布局。
    mif::FusionOptions fusion;
    fusion.keep_weight_maps = true;
    fusion.guided_filter.focus = {mif::FocusMeasure::Tenengrad, 5};
    fusion.guided_filter.base_radius = 7;
    fusion.guided_filter.detail_radius = 2;
    fusion.guided_filter.base_epsilon = 0.02;
    fusion.laplacian_pyramid.focus.window = 7;
    fusion.laplacian_pyramid.detail_epsilon = 0.0003;
    fusion.laplacian_pyramid.levels = 3;
    fusion.dct.block_size = 4;
    fusion.dct.consistency_window = 3;
    fusion.dtcwt.levels = 3;
    fusion.dtcwt.activity_window = 3;
    fusion.gfgfgf.selection_ratio = 0;
    fusion.gfgfgf.guided_radius = 3;
    for (const auto method : {mif::FusionMethod::GuidedFilter, mif::FusionMethod::LaplacianPyramid,
                              mif::FusionMethod::Dct, mif::FusionMethod::Dtcwt, mif::FusionMethod::Gfgfgf}) {
        fusion.method = method;
        const auto fused = mif::fuse({texture, texture}, fusion);
        const bool diagnostics_ok = method == mif::FusionMethod::Dtcwt
            ? fused.weights.empty() && fused.focus_indices.empty()
            : fused.weights.size() == 2 && fused.focus_indices.type() == CV_32SC1;
        if (!diagnostics_ok || cv::norm(fused.image, texture, cv::NORM_INF) > 1)
            return 4;
    }
    std::cout << "Installed SDK: headers, import library and runtime OK\n";
    return 0;
}
