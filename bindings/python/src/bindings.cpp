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

// 各入口仅复制自己使用的配置，包括融合配置中的全部嵌套值，再释放 GIL。异常退出时 RAII 先恢复 GIL，
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
    // DTCWT 在复系数域按尺度/方向选择来源，空诊断显式映射为 None。
    if (result.focus_indices.empty()) output["focus_indices"] = nb::none();
    else output["focus_indices"] = mif::python::toArray(result.focus_indices);
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
        .value("LAPLACIAN_PYRAMID", mif::FusionMethod::LaplacianPyramid)
        .value("DCT", mif::FusionMethod::Dct)
        .value("DTCWT", mif::FusionMethod::Dtcwt)
        .value("GFGFGF", mif::FusionMethod::Gfgfgf);
    nb::enum_<mif::FocusMeasure>(module, "FocusMeasure")
        .value("MODIFIED_LAPLACIAN", mif::FocusMeasure::ModifiedLaplacian)
        .value("TENENGRAD", mif::FocusMeasure::Tenengrad);
    nb::enum_<mif::RegistrationMethod>(module, "RegistrationMethod", "配准算法，与 ECC 的几何运动模型独立选择。")
        .value("NONE", mif::RegistrationMethod::None)
        .value("ECC", mif::RegistrationMethod::Ecc)
        .value("SIFT", mif::RegistrationMethod::Sift);
    nb::enum_<mif::MotionModel>(module, "MotionModel", "ECC 运动模型；SIFT 固定求解单应性，不使用此选择。")
        .value("TRANSLATION", mif::MotionModel::Translation)
        .value("AFFINE", mif::MotionModel::Affine)
        .value("HOMOGRAPHY", mif::MotionModel::Homography);

    nb::class_<mif::FocusOptions>(module, "FocusOptions", "清晰度指标及统计窗口，供双尺度引导滤波和拉普拉斯金字塔分别配置。")
        .def(nb::init<>())
        .def_rw("measure", &mif::FocusOptions::measure)
        .def_rw("window", &mif::FocusOptions::window);
    // def_rw 按值替换结构体；reference_internal 则让读取返回内部成员引用并保留父对象。
    // 因此 options.guided_filter.focus.window = 7 会直接更新原配置，暂存子对象也不会悬空。
    nb::class_<mif::GuidedFilterOptions>(module, "GuidedFilterOptions", "引导滤波融合独立参数。")
        .def(nb::init<>())
        .def_rw("focus", &mif::GuidedFilterOptions::focus, nb::rv_policy::reference_internal)
        .def_rw("base_radius", &mif::GuidedFilterOptions::base_radius)
        .def_rw("detail_radius", &mif::GuidedFilterOptions::detail_radius)
        .def_rw("base_epsilon", &mif::GuidedFilterOptions::base_epsilon)
        .def_rw("detail_epsilon", &mif::GuidedFilterOptions::detail_epsilon);
    nb::class_<mif::LaplacianPyramidOptions>(module, "LaplacianPyramidOptions", "拉普拉斯金字塔融合独立参数。")
        .def(nb::init<>())
        .def_rw("focus", &mif::LaplacianPyramidOptions::focus, nb::rv_policy::reference_internal)
        .def_rw("detail_radius", &mif::LaplacianPyramidOptions::detail_radius)
        .def_rw("detail_epsilon", &mif::LaplacianPyramidOptions::detail_epsilon)
        .def_rw("levels", &mif::LaplacianPyramidOptions::levels);
    nb::class_<mif::DctOptions>(module, "DctOptions", "块方差融合参数；沿用参考项目 DCT 标识。")
        .def(nb::init<>())
        .def_rw("block_size", &mif::DctOptions::block_size)
        .def_rw("consistency_window", &mif::DctOptions::consistency_window);
    nb::class_<mif::DtcwtOptions>(module, "DtcwtOptions", "双树复小波融合参数；六方向复系数域融合。")
        .def(nb::init<>())
        .def_rw("levels", &mif::DtcwtOptions::levels)
        .def_rw("activity_window", &mif::DtcwtOptions::activity_window);
    nb::class_<mif::GfgfgfOptions>(module, "GfgfgfOptions", "类高斯四邻域聚焦度量、Sobel 平局处理与两次快速引导滤波参数。")
        .def(nb::init<>())
        .def_rw("difference_window", &mif::GfgfgfOptions::difference_window)
        .def_rw("selection_ratio", &mif::GfgfgfOptions::selection_ratio,
            "可选 Scharr 全局筛帧比例，范围 [0,1]；默认 0，保留全部输入。")
        .def_rw("difference_threshold", &mif::GfgfgfOptions::difference_threshold,
            "GFG 梯度阈值，范围 [0,1]；弱响应使用均值残差，默认 0.005。")
        .def_rw("guided_radius", &mif::GfgfgfOptions::guided_radius)
        .def_rw("guided_epsilon", &mif::GfgfgfOptions::guided_epsilon)
        .def_rw("guided_subsample", &mif::GfgfgfOptions::guided_subsample,
            "快速引导滤波下采样倍数，范围 [1,16]，默认 4；1 使用完整分辨率。");
    // 各种方法分别保存配置，只校验选中方法；旧扁平字段不提供转发别名。
    nb::class_<mif::FusionOptions>(module, "FusionOptions", "选择融合方法并分别保存各方法参数，不包含配准配置。")
        .def(nb::init<>())
        .def_rw("method", &mif::FusionOptions::method)
        .def_rw("guided_filter", &mif::FusionOptions::guided_filter, nb::rv_policy::reference_internal)
        .def_rw("laplacian_pyramid", &mif::FusionOptions::laplacian_pyramid, nb::rv_policy::reference_internal)
        .def_rw("dct", &mif::FusionOptions::dct, nb::rv_policy::reference_internal)
        .def_rw("dtcwt", &mif::FusionOptions::dtcwt, nb::rv_policy::reference_internal)
        .def_rw("gfgfgf", &mif::FusionOptions::gfgfgf, nb::rv_policy::reference_internal)
        .def_rw("keep_weight_maps", &mif::FusionOptions::keep_weight_maps);
    nb::class_<mif::RegistrationOptions>(module, "RegistrationOptions", "独立配准参数，以第一张输入为参考。")
        .def(nb::init<>())
        .def_rw("method", &mif::RegistrationOptions::method,
            "配准算法 NONE/ECC/SIFT，默认 NONE。")
        .def_rw("motion_model", &mif::RegistrationOptions::motion_model,
            "仅 ECC 使用的运动模型，默认 TRANSLATION；SIFT 固定使用单应性。")
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
