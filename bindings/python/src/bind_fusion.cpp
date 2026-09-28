#include "array_utils.hpp"
#include <mif/fusion.hpp>
#include <nanobind/stl/vector.h>
namespace nb = nanobind;
using namespace nb::literals;

namespace {
mif::FusionResult run(const std::vector<mif::python::InputArray>& arrays, const mif::FusionOptions& options) {
    std::vector<cv::Mat> images;
    for (const auto& array : arrays) images.push_back(mif::python::copyArray(array));
    const mif::FusionOptions snapshot = options;
    nb::gil_scoped_release release;
    return mif::fuse(images, snapshot);
}
}
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
        .value("AFFINE", mif::Alignment::Affine);
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
        .def_rw("keep_weight_maps", &mif::FusionOptions::keep_weight_maps);
    module.def("fuse", [](const std::vector<mif::python::InputArray>& arrays, const mif::FusionOptions& options) {
        return mif::python::toArray(run(arrays, options).image);
    }, "images"_a, "options"_a);
    module.def("fuse_detailed", [](const std::vector<mif::python::InputArray>& arrays, const mif::FusionOptions& options) {
        const auto result = run(arrays, options);
        nb::dict output;
        output["image"] = mif::python::toArray(result.image);
        output["focus_indices"] = mif::python::toArray(result.focus_indices);
        output["crop"] = nb::make_tuple(result.crop.x, result.crop.y, result.crop.width, result.crop.height);
        nb::list weights, transforms;
        for (const auto& weight : result.weights) weights.append(mif::python::toArray(weight));
        for (const auto& transform : result.transforms) transforms.append(mif::python::toArray(transform));
        output["weights"] = weights; output["transforms"] = transforms;
        return output;
    }, "images"_a, "options"_a);
}

