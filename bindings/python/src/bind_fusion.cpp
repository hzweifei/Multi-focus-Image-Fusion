#include "array_utils.hpp"
#include <mif/fusion.hpp>
#include <nanobind/stl/vector.h>

// 统一定义 Python 模块入口、枚举、参数对象和融合函数，避免额外的入口转发文件。
namespace nb = nanobind;
using namespace nb::literals;

namespace {
// 先在 GIL 保护下复制输入和参数，再释放 GIL 执行耗时的 C++ 融合。
// 异常沿调用栈返回时 release 会先恢复 GIL，由 nanobind 转换为 Python 异常。
mif::FusionResult run(const std::vector<mif::python::InputArray>& arrays, const mif::FusionOptions& options) {
    std::vector<cv::Mat> images;
    for (const auto& array : arrays) images.push_back(mif::python::copyArray(array));
    const mif::FusionOptions snapshot = options;
    nb::gil_scoped_release release;
    return mif::fuse(images, snapshot);
}

// Python 枚举名称采用大写约定，与核心的 C++ 枚举一一对应。
void bindFusion(nb::module_& module) {
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
    // 直接暴露参数字段；默认值和有效范围以核心 FusionOptions 定义为准。
    // run 中复制参数快照，避免释放 GIL 后其他 Python 线程改动本次任务参数。
    nb::class_<mif::FusionOptions>(module, "FusionOptions")
        .def(nb::init<>())
        .def_rw("method", &mif::FusionOptions::method)
        .def_rw("focus_measure", &mif::FusionOptions::focus_measure)
        .def_rw("alignment", &mif::FusionOptions::alignment)
        .def_rw("focus_window", &mif::FusionOptions::focus_window)
        .def_rw("base_radius", &mif::FusionOptions::base_radius)
        .def_rw("detail_radius", &mif::FusionOptions::detail_radius)
        .def_rw("base_epsilon", &mif::FusionOptions::base_epsilon)
        .def_rw("detail_epsilon", &mif::FusionOptions::detail_epsilon)
        .def_rw("pyramid_levels", &mif::FusionOptions::pyramid_levels)
        .def_rw("alignment_iterations", &mif::FusionOptions::alignment_iterations)
        .def_rw("alignment_epsilon", &mif::FusionOptions::alignment_epsilon)
        .def_rw("alignment_max_size", &mif::FusionOptions::alignment_max_size)
        // 特征匹配参数保留核心的数值范围检查；界面与 Python 使用同一套默认值。
        .def_rw("alignment_max_features", &mif::FusionOptions::alignment_max_features,
            "SIFT 特征点上限，范围 [64, 100000]，默认 4000。")
        .def_rw("alignment_match_ratio", &mif::FusionOptions::alignment_match_ratio,
            "最近邻/次近邻描述子距离比阈值，范围 (0, 1)，默认 0.75。")
        .def_rw("alignment_ransac_threshold", &mif::FusionOptions::alignment_ransac_threshold,
            "RANSAC 重投影误差阈值，单位为工作分辨率像素，有限正数，默认 3.0。")
        .def_rw("alignment_min_inlier_ratio", &mif::FusionOptions::alignment_min_inlier_ratio,
            "RANSAC 内点占有效匹配的最小比例，范围 (0, 1]，默认 0.25。")
        .def_rw("keep_weight_maps", &mif::FusionOptions::keep_weight_maps);
    // 简单接口仅返回融合图像；临时结果销毁后，toArray 的 capsule 继续持有像素。
    module.def("fuse", [](const std::vector<mif::python::InputArray>& arrays, const mif::FusionOptions& options) {
        return mif::python::toArray(run(arrays, options).image);
    }, "images"_a, "options"_a);
    // 详细接口额外返回来源索引、共同裁剪区域、配准矩阵及按需保留的权重。
    // 组装字典时 run 已恢复 GIL，所有 Python 对象均在 GIL 保护下创建。
    module.def("fuse_detailed", [](const std::vector<mif::python::InputArray>& arrays, const mif::FusionOptions& options) {
        const auto result = run(arrays, options);
        nb::dict output;
        output["image"] = mif::python::toArray(result.image);
        output["focus_indices"] = mif::python::toArray(result.focus_indices);
        // crop 使用第一张输入图像坐标；变换矩阵将参考坐标映射到各源图像坐标。
        // 未配准/平移/仿射保持 2×3；两种单应性模式返回 3×3，需做齐次坐标除法。
        output["crop"] = nb::make_tuple(result.crop.x, result.crop.y, result.crop.width, result.crop.height);
        nb::list weights, transforms;
        for (const auto& weight : result.weights) weights.append(mif::python::toArray(weight));
        for (const auto& transform : result.transforms) transforms.append(mif::python::toArray(transform));
        output["weights"] = weights; output["transforms"] = transforms;
        return output;
    }, "images"_a, "options"_a);
}
} // 匿名命名空间

// 扩展名保持为 _mif，由上层 mif 包提供默认参数和 NumPy 输入整理。
NB_MODULE(_mif, module) {
    module.doc() = "使用 C++ 和 OpenCV 实现的传统多聚焦图像融合";
    bindFusion(module);
}

