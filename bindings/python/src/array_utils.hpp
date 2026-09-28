#pragma once

// Python 与核心算法之间的数据边界：输入复制为独立图像，输出由 NumPy 持有存储。
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <opencv2/core.hpp>
namespace mif::python {
// 只接收 CPU 上按 C 顺序连续存储的 NumPy 数组；ro 允许只读输入。
// Python 包装层负责整理非连续切片，具体形状与精度由 copyArray 检查。
using InputArray = nanobind::ndarray<nanobind::numpy, nanobind::device::cpu, nanobind::c_contig, nanobind::ro>;

// 接收 H×W 灰度或 H×W×3 BGR 图像，支持 uint8、uint16、float32。
// 在持有 GIL 时复制，返回的 cv::Mat 不再引用输入数组；非法布局抛出 invalid_argument。
cv::Mat copyArray(const InputArray& array);

// 将核心返回的矩阵交给 NumPy 管理，保留精度与通道顺序。
// 支持图像、int32 来源索引，以及 float32 权重和变换矩阵；不执行颜色转换。
nanobind::ndarray<nanobind::numpy> toArray(const cv::Mat& image);
} // 命名空间 mif::python

