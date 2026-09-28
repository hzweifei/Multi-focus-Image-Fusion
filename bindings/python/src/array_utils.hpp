#pragma once
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <opencv2/core.hpp>
namespace mif::python {
using InputArray = nanobind::ndarray<nanobind::numpy, nanobind::device::cpu, nanobind::c_contig, nanobind::ro>;
cv::Mat copyArray(const InputArray& array);
nanobind::ndarray<nanobind::numpy> toArray(const cv::Mat& image);
}

