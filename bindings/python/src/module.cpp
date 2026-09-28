#include <nanobind/nanobind.h>
void bindFusion(nanobind::module_& module);
NB_MODULE(_mif, module) {
    module.doc() = "Traditional multi-focus fusion using C++ and OpenCV";
    bindFusion(module);
}

