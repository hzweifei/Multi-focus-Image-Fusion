#include "array_utils.hpp"
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>
namespace nb = nanobind;
namespace mif::python {
cv::Mat copyArray(const InputArray& array) {
    // OpenCV 使用 int 表示行列；先检查维数和范围，避免窄化转换后产生错误尺寸。
    if (array.ndim() != 2 && array.ndim() != 3)
        throw std::invalid_argument("Expected H x W or H x W x 3 arrays");
    if (array.shape(0) < 2 || array.shape(1) < 2 ||
        array.shape(0) > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        array.shape(1) > static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("Invalid image dimensions");
    if (array.ndim() == 3 && array.shape(2) != 3)
        throw std::invalid_argument("Color arrays must have three BGR channels");
    const int channels = array.ndim() == 2 ? 1 : 3;
    // 保留输入精度，尤其不能把显微图像常用的 16 位数据隐式转换成 8 位。
    int depth;
    if (array.dtype() == nb::dtype<uint8_t>()) depth = CV_8U;
    else if (array.dtype() == nb::dtype<uint16_t>()) depth = CV_16U;
    else if (array.dtype() == nb::dtype<float>()) depth = CV_32F;
    else throw std::invalid_argument("Expected uint8, uint16 or float32 arrays");
    // 此处仍持有 GIL；临时 Mat 只借用 NumPy 内存，clone 后才允许释放 GIL。
    // const_cast 仅用于适配 Mat 构造接口，整个过程中不修改原数组。
    const cv::Mat view(static_cast<int>(array.shape(0)), static_cast<int>(array.shape(1)),
                       CV_MAKETYPE(depth, channels), const_cast<void*>(array.data()));
    return view.clone();
}

nb::ndarray<nb::numpy> toArray(const cv::Mat& image) {
    // 连续矩阵通过 Mat 引用计数共享存储；ROI 等非连续矩阵先复制成连续布局。
    // unique_ptr 负责接管成功前的异常清理，避免后续构造 Python 对象失败时泄漏。
    auto storage = std::make_unique<cv::Mat>(image.isContinuous() ? image : image.clone());
    auto* mat = storage.get();
    std::vector<size_t> shape{static_cast<size_t>(mat->rows), static_cast<size_t>(mat->cols)};
    if (mat->channels() != 1) shape.push_back(static_cast<size_t>(mat->channels()));
    nb::dlpack::dtype dtype;
    // 核心输出只包含下面四种精度；CV_32S 用于保留超过 255 张图像的来源索引。
    switch (mat->depth()) {
    case CV_8U: dtype = nb::dtype<uint8_t>(); break;
    case CV_16U: dtype = nb::dtype<uint16_t>(); break;
    case CV_32S: dtype = nb::dtype<int32_t>(); break;
    default: dtype = nb::dtype<float>(); break;
    }
    // capsule 拥有 Mat 对象，NumPy 数组销毁时再释放引用，因此局部 FusionResult
    // 离开作用域后，Python 返回值仍然有效；用户也可独立修改返回的图像。
    nb::capsule owner(mat, [](void* pointer) noexcept { delete static_cast<cv::Mat*>(pointer); });
    storage.release();
    return nb::ndarray<nb::numpy>(mat->data, shape.size(), shape.data(), owner, nullptr, dtype);
}
} // 命名空间 mif::python

