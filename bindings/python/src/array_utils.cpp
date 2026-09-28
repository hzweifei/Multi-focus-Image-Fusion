#include "array_utils.hpp"
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>
namespace nb = nanobind;
namespace mif::python {
cv::Mat copyArray(const InputArray& array) {
    if (array.ndim() != 2 && array.ndim() != 3)
        throw std::invalid_argument("Expected H x W or H x W x 3 arrays");
    if (array.shape(0) < 2 || array.shape(1) < 2 ||
        array.shape(0) > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        array.shape(1) > static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("Invalid image dimensions");
    if (array.ndim() == 3 && array.shape(2) != 3)
        throw std::invalid_argument("Color arrays must have three BGR channels");
    const int channels = array.ndim() == 2 ? 1 : 3;
    int depth;
    if (array.dtype() == nb::dtype<uint8_t>()) depth = CV_8U;
    else if (array.dtype() == nb::dtype<uint16_t>()) depth = CV_16U;
    else if (array.dtype() == nb::dtype<float>()) depth = CV_32F;
    else throw std::invalid_argument("Expected uint8, uint16 or float32 arrays");
    // Clone before releasing the GIL: the caller can safely reuse its arrays.
    const cv::Mat view(static_cast<int>(array.shape(0)), static_cast<int>(array.shape(1)),
                       CV_MAKETYPE(depth, channels), const_cast<void*>(array.data()));
    return view.clone();
}

nb::ndarray<nb::numpy> toArray(const cv::Mat& image) {
    auto storage = std::make_unique<cv::Mat>(image.isContinuous() ? image : image.clone());
    auto* mat = storage.get();
    std::vector<size_t> shape{static_cast<size_t>(mat->rows), static_cast<size_t>(mat->cols)};
    if (mat->channels() != 1) shape.push_back(static_cast<size_t>(mat->channels()));
    nb::dlpack::dtype dtype;
    switch (mat->depth()) {
    case CV_8U: dtype = nb::dtype<uint8_t>(); break;
    case CV_16U: dtype = nb::dtype<uint16_t>(); break;
    case CV_32S: dtype = nb::dtype<int32_t>(); break;
    default: dtype = nb::dtype<float>(); break;
    }
    nb::capsule owner(mat, [](void* pointer) noexcept { delete static_cast<cv::Mat*>(pointer); });
    storage.release();
    return nb::ndarray<nb::numpy>(mat->data, shape.size(), shape.data(), owner, nullptr, dtype);
}
}

