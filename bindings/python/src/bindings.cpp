#include "array_utils.hpp"
#include <mif/fusion.hpp>
#include <mif/pipeline.hpp>
#include <mif/registration.hpp>
#include <nanobind/stl/vector.h>

// 模块入口、参数类和阶段接口集中在此文件；配准、融合的计算仍由各自核心模块负责。
namespace nb = nanobind;
using namespace nb::literals;

namespace {

// 在持有 GIL 时取得独立输入副本，确保耗时阶段不再引用调用者的 NumPy 缓冲区。
std::vector<cv::Mat> copyInputs(const std::vector<mif::python::InputArray>& arrays) {
    std::vector<cv::Mat> images;
    images.reserve(arrays.size());
    for (const auto& array : arrays) images.push_back(mif::python::copyArray(array));
    return images;
}

// 各入口仅复制自己使用的配置，再释放 GIL。异常退出时 RAII 先恢复 GIL，
// nanobind 随后把 C++ 异常转换成 Python 异常；结果包装也在恢复 GIL 后进行。
mif::FusionResult runFusion(const std::vector<mif::python::InputArray>& arrays,
                            const mif::FusionOptions& options) {
    auto images = copyInputs(arrays);
    const mif::FusionOptions snapshot = options;
    nb::gil_scoped_release release;
    return mif::fuse(images, snapshot);
}

mif::RegistrationResult runRegistration(const std::vector<mif::python::InputArray>& arrays,
                                        const mif::RegistrationOptions& options) {
    auto images = copyInputs(arrays);
    const mif::RegistrationOptions snapshot = options;
    nb::gil_scoped_release release;
    return mif::registerImages(images, snapshot);
}

mif::PipelineResult runPipeline(const std::vector<mif::python::InputArray>& arrays,
                                const mif::RegistrationOptions& registration_options,
                                const mif::FusionOptions& fusion_options) {
    auto images = copyInputs(arrays);
    const mif::RegistrationOptions registration_snapshot = registration_options;
    const mif::FusionOptions fusion_snapshot = fusion_options;
    nb::gil_scoped_release release;
    return mif::registerAndFuse(images, registration_snapshot, fusion_snapshot);
}

// 每个数组通过 capsule 持有 cv::Mat 存储，不依赖局部结果对象或 Python 列表的寿命。
nb::list arrayList(const std::vector<cv::Mat>& images) {
    nb::list output;
    for (const auto& image : images) output.append(mif::python::toArray(image));
    return output;
}

// 纯融合只返回本阶段的数据，不生成未经配准的 crop 或单位变换等占位元数据。
nb::dict fusionDictionary(const mif::FusionResult& result) {
    nb::dict output;
    output["image"] = mif::python::toArray(result.image);
    output["focus_indices"] = mif::python::toArray(result.focus_indices);
    output["weights"] = arrayList(result.weights);
    return output;
}

// 配准和组合流程共用坐标约定：crop 属于参考图坐标系，矩阵方向为参考→源图。
void addRegistrationMetadata(nb::dict& output, const cv::Rect& crop,
                             const std::vector<cv::Mat>& transforms) {
    output["crop"] = nb::make_tuple(crop.x, crop.y, crop.width, crop.height);
    output["transforms"] = arrayList(transforms);
}

void bindOptions(nb::module_& module) {
    nb::enum_<mif::FusionMethod>(module, "FusionMethod")
        .value("GUIDED_FILTER", mif::FusionMethod::GuidedFilter)
        .value("LAPLACIAN_PYRAMID", mif::FusionMethod::LaplacianPyramid);
    nb::enum_<mif::FocusMeasure>(module, "FocusMeasure")
        .value("MODIFIED_LAPLACIAN", mif::FocusMeasure::ModifiedLaplacian)
        .value("TENENGRAD", mif::FocusMeasure::Tenengrad);
    nb::enum_<mif::Alignment>(module, "Alignment")
        .value("NONE", mif::Alignment::None)
        .value("TRANSLATION", mif::Alignment::Translation)
        .value("AFFINE", mif::Alignment::Affine)
        .value("FEATURE_HOMOGRAPHY", mif::Alignment::FeatureHomography)
        .value("ECC_HOMOGRAPHY", mif::Alignment::EccHomography);

    // 融合配置只包含融合所需字段；旧 alignment_* 属性不再转发或隐式生效。
    nb::class_<mif::FusionOptions>(module, "FusionOptions", "纯融合参数，不包含配准配置。")
        .def(nb::init<>())
        .def_rw("method", &mif::FusionOptions::method)
        .def_rw("focus_measure", &mif::FusionOptions::focus_measure)
        .def_rw("focus_window", &mif::FusionOptions::focus_window)
        .def_rw("base_radius", &mif::FusionOptions::base_radius)
        .def_rw("detail_radius", &mif::FusionOptions::detail_radius)
        .def_rw("base_epsilon", &mif::FusionOptions::base_epsilon)
        .def_rw("detail_epsilon", &mif::FusionOptions::detail_epsilon)
        .def_rw("pyramid_levels", &mif::FusionOptions::pyramid_levels)
        .def_rw("keep_weight_maps", &mif::FusionOptions::keep_weight_maps);
    nb::class_<mif::RegistrationOptions>(module, "RegistrationOptions", "独立配准参数，以第一张输入为参考。")
        .def(nb::init<>())
        .def_rw("method", &mif::RegistrationOptions::method)
        .def_rw("iterations", &mif::RegistrationOptions::iterations)
        .def_rw("epsilon", &mif::RegistrationOptions::epsilon)
        .def_rw("max_size", &mif::RegistrationOptions::max_size)
        .def_rw("max_features", &mif::RegistrationOptions::max_features,
            "SIFT 特征点上限，范围 [64, 100000]，默认 4000。")
        .def_rw("match_ratio", &mif::RegistrationOptions::match_ratio,
            "最近邻/次近邻描述子距离比阈值，范围 (0, 1)，默认 0.75。")
        .def_rw("ransac_threshold", &mif::RegistrationOptions::ransac_threshold,
            "RANSAC 重投影误差阈值，单位为工作分辨率像素，有限正数，默认 3.0。")
        .def_rw("min_inlier_ratio", &mif::RegistrationOptions::min_inlier_ratio,
            "RANSAC 内点占有效匹配的最小比例，范围 (0, 1]，默认 0.25。");
}

void bindOperations(nb::module_& module) {
    module.def("fuse", [](const std::vector<mif::python::InputArray>& arrays,
                           const mif::FusionOptions& options) {
        return mif::python::toArray(runFusion(arrays, options).image);
    }, "images"_a, "options"_a);
    module.def("fuse_detailed", [](const std::vector<mif::python::InputArray>& arrays,
                                    const mif::FusionOptions& options) {
        return fusionDictionary(runFusion(arrays, options));
    }, "images"_a, "options"_a);
    module.def("register_images", [](const std::vector<mif::python::InputArray>& arrays,
                                      const mif::RegistrationOptions& options) {
        const auto result = runRegistration(arrays, options);
        nb::dict output;
        output["images"] = arrayList(result.images);
        addRegistrationMetadata(output, result.crop, result.transforms);
        return output;
    }, "images"_a, "options"_a);
    module.def("register_and_fuse", [](const std::vector<mif::python::InputArray>& arrays,
                                        const mif::RegistrationOptions& registration_options,
                                        const mif::FusionOptions& fusion_options) {
        const auto result = runPipeline(arrays, registration_options, fusion_options);
        nb::dict output = fusionDictionary(result.fusion);
        addRegistrationMetadata(output, result.crop, result.transforms);
        return output;
    }, "images"_a, "registration_options"_a, "fusion_options"_a);
}

} // 匿名命名空间

// 扩展名保持为 _mif，纯 Python 包负责默认参数和非连续数组整理。
NB_MODULE(_mif, module) {
    module.doc() = "使用 C++ 和 OpenCV 实现的独立图像配准、传统多聚焦融合与组合流程";
    bindOptions(module);
    bindOperations(module);
}
