#include <mif/fusion.hpp>
#include <mif/registration.hpp>
#include <mif/pipeline.hpp>
#include <iostream>

// 由独立 CMake 工程链接已交付的 SDK，验证公开头文件、导入库与运行库可配套使用。
// 两张常量 16 位图没有清晰度差异，结果应保留位深并接近等权平均值。
int main() {
    const cv::Mat near(24, 32, CV_16U, cv::Scalar(10000));
    const cv::Mat far(24, 32, CV_16U, cv::Scalar(50000));
    const auto registered = mif::registerImages({near, far});
    const auto result = mif::fuse(registered.images);
    if (result.image.type() != CV_16U ||
        cv::norm(result.image, cv::Mat(near.size(), CV_16U, cv::Scalar(30000)), cv::NORM_INF) > 1)
        return 1;
    const auto pipeline = mif::registerAndFuse({near, far});
    if (pipeline.crop != registered.crop || pipeline.transforms.size() != 2 ||
        cv::norm(pipeline.fusion.image, result.image, cv::NORM_INF) != 0)
        return 2;
    std::cout << "Installed SDK: headers, import library and runtime OK\n";
    return 0;
}
