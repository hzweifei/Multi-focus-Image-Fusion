#include "array_utils.hpp"
#include <mif/mif.hpp>
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

// 各入口在释放 GIL 前克隆具体参数，避免基类按值复制丢失方法参数，也避免其他 Python 线程修改配置。
// 异常退出时 RAII 先恢复 GIL，
// nanobind 随后把 C++ 异常转换成 Python 异常；结果包装也在恢复 GIL 后进行。
mif::FusionResult runFusion(const std::vector<mif::python::InputArray>& arrays,
                            const mif::FusionOptionsBase& options) {
    auto images = copyInputs(arrays);
    const auto snapshot = options.clone();
    nb::gil_scoped_release release;
    return mif::fuse(images, *snapshot);
}

mif::RegistrationResult runRegistration(const std::vector<mif::python::InputArray>& arrays,
                                        const mif::RegistrationOptionsBase& options) {
    auto images = copyInputs(arrays);
    const auto snapshot = options.clone();
    nb::gil_scoped_release release;
    return mif::registerImages(images, *snapshot);
}

mif::PipelineResult runPipeline(const std::vector<mif::python::InputArray>& arrays,
                                const mif::RegistrationOptionsBase& registration_options,
                                const mif::FusionOptionsBase& fusion_options) {
    auto images = copyInputs(arrays);
    const auto registration_snapshot = registration_options.clone();
    const auto fusion_snapshot = fusion_options.clone();
    nb::gil_scoped_release release;
    return mif::registerAndFuse(images, *registration_snapshot, *fusion_snapshot);
}

// 每个数组通过 capsule 持有 cv::Mat 存储，不依赖局部结果对象或 Python 列表的寿命。
nb::list arrayList(const std::vector<cv::Mat>& images) {
    nb::list output;
    for (const auto& image : images) output.append(mif::python::toArray(image));
    return output;
}

// 纯融合只返回本阶段的数据，配准坐标信息由独立配准入口提供。
nb::dict fusionDictionary(const mif::FusionResult& result) {
    nb::dict output;
    output["image"] = mif::python::toArray(result.image);
    // DTCWT 在复系数域按尺度/方向选择来源，空诊断显式映射为 None。
    if (result.source_index_map.empty()) output["source_index_map"] = nb::none();
    else output["source_index_map"] = mif::python::toArray(result.source_index_map);
    output["weight_maps"] = arrayList(result.weight_maps);
    return output;
}

// 配准和组合流程共用坐标约定：crop_region 属于参考图坐标系，矩阵方向为参考→源图。
void addRegistrationMetadata(nb::dict& output, const cv::Rect& crop_region,
                             const std::vector<cv::Mat>& transforms) {
    output["crop_region"] = nb::make_tuple(crop_region.x, crop_region.y, crop_region.width, crop_region.height);
    output["transforms"] = arrayList(transforms);
}

void bindOptions(nb::module_& module) {
    nb::enum_<mif::FocusMeasure>(module, "FocusMeasure")
        .value("MODIFIED_LAPLACIAN", mif::FocusMeasure::ModifiedLaplacian)
        .value("TENENGRAD", mif::FocusMeasure::Tenengrad);
    nb::enum_<mif::MotionModel>(module, "MotionModel", "ECC 运动模型；SIFT 固定求解单应性，不使用此选择。")
        .value("TRANSLATION", mif::MotionModel::Translation)
        .value("AFFINE", mif::MotionModel::Affine)
        .value("HOMOGRAPHY", mif::MotionModel::Homography);

    // 基类只暴露阶段共有字段；调用者实例化下面的具体参数类，类型本身决定算法。
    nb::class_<mif::FusionOptionsBase>(module, "FusionOptionsBase", "融合参数公共基类；请使用具体方法的参数类。")
        .def_rw("include_weight_maps", &mif::FusionOptionsBase::include_weight_maps);
    nb::class_<mif::RegistrationOptionsBase>(module, "RegistrationOptionsBase", "配准参数公共基类；请使用具体方法的参数类。")
        .def_rw("max_working_dimension", &mif::RegistrationOptionsBase::max_working_dimension);
    nb::class_<mif::FocusMeasureOptions>(module, "FocusMeasureOptions", "清晰度指标及统计窗口，供双尺度引导滤波和拉普拉斯金字塔分别配置。")
        .def(nb::init<>())
        .def_rw("measure", &mif::FocusMeasureOptions::measure)
        .def_rw("window_size", &mif::FocusMeasureOptions::window_size);
    // def_rw 按值替换结构体；reference_internal 则让读取返回内部成员引用并保留父对象。
    // 因此 options.focus.window_size = 7 会直接更新原配置，暂存子对象也不会悬空。
    nb::class_<mif::GuidedFilterFusionOptions, mif::FusionOptionsBase>(module, "GuidedFilterFusionOptions", "引导滤波融合参数。")
        .def(nb::init<>())
        .def_rw("focus", &mif::GuidedFilterFusionOptions::focus, nb::rv_policy::reference_internal)
        .def_rw("base_radius", &mif::GuidedFilterFusionOptions::base_radius)
        .def_rw("detail_radius", &mif::GuidedFilterFusionOptions::detail_radius)
        .def_rw("base_epsilon", &mif::GuidedFilterFusionOptions::base_epsilon)
        .def_rw("detail_epsilon", &mif::GuidedFilterFusionOptions::detail_epsilon);
    nb::class_<mif::LaplacianPyramidFusionOptions, mif::FusionOptionsBase>(module, "LaplacianPyramidFusionOptions", "拉普拉斯金字塔融合参数。")
        .def(nb::init<>())
        .def_rw("focus", &mif::LaplacianPyramidFusionOptions::focus, nb::rv_policy::reference_internal)
        .def_rw("detail_radius", &mif::LaplacianPyramidFusionOptions::detail_radius)
        .def_rw("detail_epsilon", &mif::LaplacianPyramidFusionOptions::detail_epsilon)
        .def_rw("max_levels", &mif::LaplacianPyramidFusionOptions::max_levels);
    nb::class_<mif::BlockVarianceFusionOptions, mif::FusionOptionsBase>(module, "BlockVarianceFusionOptions", "按块方差选图的融合参数，对应参考项目的 DCT 方法。")
        .def(nb::init<>())
        .def_rw("block_size", &mif::BlockVarianceFusionOptions::block_size)
        .def_rw("consistency_window_size", &mif::BlockVarianceFusionOptions::consistency_window_size);
    nb::class_<mif::DtcwtFusionOptions, mif::FusionOptionsBase>(module, "DtcwtFusionOptions", "双树复小波融合参数；六方向复系数域融合。")
        .def(nb::init<>())
        .def_rw("max_levels", &mif::DtcwtFusionOptions::max_levels)
        .def_rw("activity_window_size", &mif::DtcwtFusionOptions::activity_window_size);
    nb::class_<mif::GfgFgfFusionOptions, mif::FusionOptionsBase>(module, "GfgFgfFusionOptions", "类高斯四邻域聚焦度量、Sobel 平局处理与两次快速引导滤波参数。")
        .def(nb::init<>())
        .def_rw("local_mean_window_size", &mif::GfgFgfFusionOptions::local_mean_window_size)
        .def_rw("selection_ratio", &mif::GfgFgfFusionOptions::selection_ratio,
            "可选 Scharr 全局筛帧比例，范围 [0,1]；默认 0，保留全部输入。")
        .def_rw("gfg_threshold", &mif::GfgFgfFusionOptions::gfg_threshold,
            "GFG 梯度阈值，范围 [0,1]；弱响应使用均值残差，默认 0.005。")
        .def_rw("guided_radius", &mif::GfgFgfFusionOptions::guided_radius)
        .def_rw("guided_epsilon", &mif::GfgFgfFusionOptions::guided_epsilon)
        .def_rw("guided_subsample_factor", &mif::GfgFgfFusionOptions::guided_subsample_factor,
            "快速引导滤波下采样倍数，范围 [1,16]，默认 4；1 使用完整分辨率。");
    nb::class_<mif::NoRegistrationOptions, mif::RegistrationOptionsBase>(module, "NoRegistrationOptions", "保持原始坐标，返回独立图像副本。")
        .def(nb::init<>());
    nb::class_<mif::EccRegistrationOptions, mif::RegistrationOptionsBase>(module, "EccRegistrationOptions", "ECC 配准参数，以第一张输入为参考。")
        .def(nb::init<>())
        .def_rw("motion_model", &mif::EccRegistrationOptions::motion_model,
            "ECC 运动模型，默认 TRANSLATION。")
        .def_rw("max_iterations", &mif::EccRegistrationOptions::max_iterations)
        .def_rw("convergence_tolerance", &mif::EccRegistrationOptions::convergence_tolerance);
    nb::class_<mif::SiftRegistrationOptions, mif::RegistrationOptionsBase>(module, "SiftRegistrationOptions", "SIFT 特征匹配与单应性配准参数。")
        .def(nb::init<>())
        .def_rw("max_features", &mif::SiftRegistrationOptions::max_features,
            "SIFT 特征点上限，范围 [64, 100000]，默认 4000。")
        .def_rw("match_ratio_threshold", &mif::SiftRegistrationOptions::match_ratio_threshold,
            "最近邻/次近邻描述子距离比阈值，范围 (0, 1)，默认 0.75。")
        .def_rw("ransac_reprojection_threshold", &mif::SiftRegistrationOptions::ransac_reprojection_threshold,
            "RANSAC 重投影误差阈值，单位为工作分辨率像素，有限正数，默认 3.0。")
        .def_rw("min_inlier_ratio", &mif::SiftRegistrationOptions::min_inlier_ratio,
            "RANSAC 内点占有效匹配的最小比例，范围 (0, 1]，默认 0.25。");
}

void bindOperations(nb::module_& module) {
    module.def("fuse", [](const std::vector<mif::python::InputArray>& arrays,
                           const mif::FusionOptionsBase& options) {
        return mif::python::toArray(runFusion(arrays, options).image);
    }, "images"_a, "options"_a);
    module.def("fuse_detailed", [](const std::vector<mif::python::InputArray>& arrays,
                                    const mif::FusionOptionsBase& options) {
        return fusionDictionary(runFusion(arrays, options));
    }, "images"_a, "options"_a);
    module.def("register_images", [](const std::vector<mif::python::InputArray>& arrays,
                                      const mif::RegistrationOptionsBase& options) {
        const auto result = runRegistration(arrays, options);
        nb::dict output;
        output["images"] = arrayList(result.images);
        addRegistrationMetadata(output, result.crop_region, result.transforms);
        return output;
    }, "images"_a, "options"_a);
    module.def("register_and_fuse", [](const std::vector<mif::python::InputArray>& arrays,
                                        const mif::RegistrationOptionsBase& registration_options,
                                        const mif::FusionOptionsBase& fusion_options) {
        const auto result = runPipeline(arrays, registration_options, fusion_options);
        nb::dict output = fusionDictionary(result.fusion);
        addRegistrationMetadata(output, result.crop_region, result.transforms);
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
