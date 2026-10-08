#include "fixtures.hpp"
#include "registration/registry.hpp"
#include <mif/registration.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

namespace {

/// 使用固定种子生成不同大小、位置和灰度的结构，避免周期纹理导致多解。
/// 保留低幅度浮点噪声，既模拟实际图像，也使后续 16 位测试含有真实的低位信息。
cv::Mat registrationTexture(int rows, int cols) {
    cv::RNG random(20261003);
    cv::Mat image(rows, cols, CV_32F, cv::Scalar(0.42));
    const double scale = std::max(0.6, static_cast<double>(std::min(rows, cols)) / 400.0);
    for (int i = 0; i < 220; ++i) {
        const cv::Point center(random.uniform(12, cols - 12), random.uniform(12, rows - 12));
        const double gray = random.uniform(0.10, 0.92);
        const int thickness = i % 3 == 0 ? std::max(1, cvRound(2 * scale)) : cv::FILLED;
        if (i % 2 == 0) {
            cv::circle(image, center, std::max(2, cvRound(random.uniform(4, 20) * scale)),
                       cv::Scalar(gray), thickness);
        } else {
            const cv::Point corner(center.x + cvRound(random.uniform(8, 45) * scale),
                                   center.y + cvRound(random.uniform(5, 24) * scale));
            cv::rectangle(image, center, corner, cv::Scalar(gray), thickness);
        }
    }
    for (int i = 0; i < 18; ++i) {
        const cv::Point origin(random.uniform(10, std::max(11, cols - 100)),
                               random.uniform(25, rows - 10));
        cv::putText(image, "P" + std::to_string(i * 17 + 3), origin,
                    cv::FONT_HERSHEY_SIMPLEX, 0.5 * scale,
                    cv::Scalar(i % 2 == 0 ? 0.88 : 0.12), std::max(1, cvRound(scale)));
    }
    cv::Mat noise(image.size(), CV_32F);
    random.fill(noise, cv::RNG::NORMAL, 0.0, 0.004);
    image += noise;
    cv::max(image, 0.04, image);
    cv::min(image, 0.96, image);
    return image;
}

/// H 定义为参考坐标到源图坐标；正向生成源图，便于检验返回矩阵方向。
/// 用反射边界生成画布外内容，避免合成图的黑框干扰估计；有效区域另用全 1 掩码验证。
cv::Mat transformed(const cv::Mat& reference, const cv::Mat& homography) {
    cv::Mat source;
    cv::warpPerspective(reference, source, homography, reference.size(),
                        cv::INTER_LINEAR, cv::BORDER_REFLECT_101);
    return source;
}

/// 在原始分辨率上检查分布于整幅图的网格点，避免仅验证某个矩阵元素或融合图误差。
/// 平均误差约束整体准确度，最大误差可发现边缘透视项及缩放换算错误。
void checkProjection(const cv::Mat& actual, const cv::Mat& expected, cv::Size size,
                     double mean_limit, double maximum_limit, const std::string& label) {
    require(actual.size() == cv::Size(3, 3) && actual.type() == CV_32FC1,
            label + ": expected a 3 x 3 float32 transform");
    require(cv::checkRange(actual), label + ": non-finite transform");
    std::vector<cv::Point2d> grid;
    for (int y = 0; y < 6; ++y)
        for (int x = 0; x < 6; ++x)
            grid.emplace_back((0.08 + 0.84 * x / 5.0) * (size.width - 1),
                              (0.08 + 0.84 * y / 5.0) * (size.height - 1));
    std::vector<cv::Point2d> actual_points, expected_points;
    cv::perspectiveTransform(grid, actual_points, actual);
    cv::perspectiveTransform(grid, expected_points, expected);
    double total = 0.0, maximum = 0.0;
    for (size_t i = 0; i < grid.size(); ++i) {
        const double error = cv::norm(actual_points[i] - expected_points[i]);
        total += error;
        maximum = std::max(maximum, error);
    }
    const double mean = total / static_cast<double>(grid.size());
    std::cout << label << " grid mean=" << mean << " max=" << maximum << " pixels\n";
    require(mean < mean_limit, label + ": excessive mean projection error");
    require(maximum < maximum_limit, label + ": excessive maximum projection error");
}

/// 验证返回的裁剪区域在每张源图中均有完整插值覆盖，而非只看输出像素是否恰好非黑。
void checkCoverage(const mif::RegistrationResult& result, const std::vector<cv::Mat>& originals) {
    const cv::Size original_size = originals.front().size();
    const cv::Rect full(0, 0, original_size.width, original_size.height);
    require((result.crop_region & full) == result.crop_region && result.crop_region.area() > 0,
            "Crop is empty or outside the reference image");
    require(result.images.size() == originals.size() && result.transforms.size() == originals.size(),
            "Registration result does not contain every input image");
    const cv::Mat valid(original_size, CV_32F, cv::Scalar(1));
    for (size_t i = 0; i < originals.size(); ++i) {
        const auto& transform = result.transforms[i];
        const auto& image = result.images[i];
        require(image.size() == result.crop_region.size(), "Registered image does not follow the reported crop");
        require(image.type() == originals[i].type(), "Registration changed an image depth or channel count");
        require(image.data != originals[i].data, "Registered image still borrows the input buffer");
        const double range = originals[i].depth() == CV_8U ? 255.0 :
                             originals[i].depth() == CV_16U ? 65535.0 : 1.0;
        cv::Mat normalized, resampled, coverage;
        originals[i].convertTo(normalized, CV_32F, 1.0 / range);
        if (transform.rows == 3) {
            cv::warpPerspective(valid, coverage, transform, original_size,
                                cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT);
            cv::warpPerspective(normalized, resampled, transform, original_size,
                                cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT);
        } else {
            require(transform.size() == cv::Size(3, 2), "Unexpected registration matrix dimensions");
            cv::warpAffine(valid, coverage, transform, original_size,
                           cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT);
            cv::warpAffine(normalized, resampled, transform, original_size,
                           cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT);
        }
        double minimum;
        cv::minMaxLoc(coverage(result.crop_region), &minimum);
        require(minimum >= 0.9999, "Crop contains pixels mixed with an invalid source border");
        // 检查每张返回图的像素确实对应其变换与裁剪信息；只验证矩阵或融合结果会漏掉错帧。
        cv::Mat expected;
        resampled(result.crop_region).convertTo(expected, originals[i].type(), range);
        require(cv::norm(image, expected, cv::NORM_INF) <= (image.depth() == CV_32F ? 1e-6 : 1.0),
                "Registered pixels do not match their transform and crop metadata");
    }
}

/// 从独立配准入口执行测试，并确认配准与裁剪均未修改调用者输入。
void checkStack(const std::vector<cv::Mat>& images, const std::vector<cv::Mat>& truth,
                const mif::RegistrationOptionsBase& options, double mean_limit, double maximum_limit,
                const std::string& label) {
    std::vector<cv::Mat> before;
    for (const auto& image : images) before.push_back(image.clone());
    const auto result = mif::registerImages(images, options);
    require(result.transforms.size() == images.size(), label + ": transform count changed");
    require(truth.size() == images.size(), "Invalid registration test fixture");
    for (size_t i = 0; i < images.size(); ++i) {
        require(cv::norm(images[i], before[i], cv::NORM_INF) == 0, label + ": source pixels changed");
        checkProjection(result.transforms[i], truth[i], images[i].size(),
                        i == 0 ? 1e-6 : mean_limit, i == 0 ? 1e-6 : maximum_limit,
                        label + " image " + std::to_string(i + 1));
    }
    checkCoverage(result, images);
    const double retained = static_cast<double>(result.crop_region.area()) /
                            static_cast<double>(images.front().total());
    require(retained > 0.70 && retained < 1.0, label + ": unexpectedly large or absent crop");
}

/// 左右互补的轻度失焦仍保留可匹配的轮廓；使用已知几何变换检查焦点变化下的配准。
/// 该用例验证几何精度，不以融合结果更接近第一张输入作为成功依据。
void checkComplementaryFocus(const mif::RegistrationOptionsBase& options, const std::string& label) {
    const cv::Mat sharp = registrationTexture(385, 577);
    cv::Mat blurred;
    cv::GaussianBlur(sharp, blurred, {0, 0}, 1.15);
    cv::Mat reference = blurred.clone(), source_focus = blurred.clone();
    const cv::Rect left(0, 0, sharp.cols / 2, sharp.rows);
    const cv::Rect right(sharp.cols / 2, 0, sharp.cols - sharp.cols / 2, sharp.rows);
    sharp(left).copyTo(reference(left));
    sharp(right).copyTo(source_focus(right));
    const cv::Mat truth = (cv::Mat_<double>(3, 3) <<
        1.0008, 0.0007, 1.4, -0.0006, 0.9996, -1.1, 7e-7, -6e-7, 1.0);
    checkStack({reference, transformed(source_focus, truth)},
               {cv::Mat::eye(3, 3, CV_64F), truth}, options, 0.45, 0.90, label);
}

/// 完全相同的高位深图像无需几何重采样损失；检查数值恢复，防止 SIFT 的 8 位
/// 特征预处理错误地替换原始像素。只检查输出类型无法发现这种精度丢失。
void checkSixteenBitPrecision(const mif::RegistrationOptionsBase& options) {
    cv::Mat image;
    registrationTexture(257, 383).convertTo(image, CV_16U, 65535.0);
    const cv::Mat before = image.clone();
    size_t low_bit_pixels = 0;
    for (int y = 0; y < image.rows; ++y)
        for (int x = 0; x < image.cols; ++x)
            if (image.at<unsigned short>(y, x) % 257 != 0) ++low_bit_pixels;
    require(low_bit_pixels > image.total() / 2, "Precision fixture lacks meaningful 16-bit values");
    const auto result = mif::registerImages({image, image}, options);
    require(cv::norm(image, before, cv::NORM_INF) == 0, "16-bit source was modified");
    checkCoverage(result, {image, image});
    require(result.crop_region.area() > 0.9 * static_cast<double>(image.total()),
            "Identical 16-bit inputs lost a large image area");
    for (const auto& registered : result.images) {
        require(registered.type() == CV_16UC1, "Registration reduced the output bit depth");
        require(cv::norm(registered, image(result.crop_region), cv::NORM_INF) <= 2,
                "Registration lost low-order information in identical 16-bit inputs");
    }
}

/// 配准失败必须有可定位到输入的序号；公开错误约定使用从 1 开始的编号。
void expectRegistrationFailure(const std::vector<cv::Mat>& images,
                               const mif::RegistrationOptionsBase& options, int expected_index) {
    try {
        mif::registerImages(images, options);
    } catch (const std::runtime_error& error) {
        require(std::string(error.what()).find("image " + std::to_string(expected_index)) != std::string::npos,
                std::string("Missing one-based failing image index: ") + error.what());
        return;
    }
    throw std::runtime_error("Unreliable registration unexpectedly succeeded");
}

/// 测试扩展只声明自己的参数与实现，不修改公开入口的算法清单或分派代码。
struct FixedRegistrationOptions final : mif::RegistrationOptionsBase {
    double shift_x = 3.0;
    double shift_y = -2.0;
    bool projective = false;
    bool wrong_model = false;
    bool missing_estimator = false;

    std::unique_ptr<mif::RegistrationOptionsBase> clone() const override {
        return std::make_unique<FixedRegistrationOptions>(*this);
    }
};

/// 可复制但没有注册的参数，验证入口不会静默把未知类型当成跳过配准。
struct UnknownRegistrationOptions final : mif::RegistrationOptionsBase {
    std::unique_ptr<mif::RegistrationOptionsBase> clone() const override {
        return std::make_unique<UnknownRegistrationOptions>(*this);
    }
};

cv::Mat fixedTransform(const FixedRegistrationOptions& options) {
    return (cv::Mat_<double>(3, 3) << 1, 0, options.shift_x, 0, 1, options.shift_y,
            options.projective || options.wrong_model ? 1e-5 : 0.0, 0, 1);
}

class FixedEstimator final : public mif::detail::registration::Estimator {
public:
    FixedEstimator(cv::Size size, const FixedRegistrationOptions& options)
        : size_(size), transform_(fixedTransform(options)), projective_(options.projective) {}

    cv::Mat estimate(const cv::Mat& source) override {
        require(source.type() == CV_32FC1 && source.size() == size_,
                "An extension did not receive normalized working grayscale images");
        require(cv::checkRange(source, true, nullptr, 0.0, 1.000001),
                "An extension received a source outside the normalized range");
        return transform_.clone();
    }

    bool isProjective() const noexcept override { return projective_; }

private:
    cv::Size size_;
    cv::Mat transform_;
    bool projective_;
};

void validateFixedRegistration(const FixedRegistrationOptions& options) {
    if (!std::isfinite(options.shift_x) || !std::isfinite(options.shift_y))
        throw std::invalid_argument("Fixed registration shifts must be finite");
}

std::unique_ptr<mif::detail::registration::Estimator> makeFixedEstimator(
    const cv::Mat& reference, const FixedRegistrationOptions& options) {
    require(reference.type() == CV_32FC1 && cv::checkRange(reference, true, nullptr, 0.0, 1.000001),
            "An extension did not receive a normalized grayscale reference");
    if (options.missing_estimator) return {};
    return std::make_unique<FixedEstimator>(reference.size(), options);
}

} // 匿名命名空间

/// ECC 的平移与仿射模型使用同一方法入口，返回 2×3 元数据；逐图验证变换与像素。
void testRegistrationAlignment() {
    const auto image = texture(161, 241);
    for (auto model : {mif::MotionModel::Translation, mif::MotionModel::Affine}) {
        cv::Mat warp = (cv::Mat_<float>(2, 3) << 1, 0, 2.25, 0, 1, -1.75);
        if (model == mif::MotionModel::Affine) {
            warp.at<float>(0, 0) = 1.005f;
            warp.at<float>(0, 1) = 0.003f;
        }
        cv::Mat shifted;
        cv::warpAffine(image, shifted, warp, image.size(), cv::INTER_LINEAR, cv::BORDER_REFLECT_101);
        mif::EccRegistrationOptions options;
        options.motion_model = model;
        const auto result = mif::registerImages({image, shifted}, options);
        require(result.crop_region.area() < image.rows * image.cols, "Alignment did not crop invalid borders");
        require(result.crop_region.width > image.cols - 15 && result.crop_region.height > image.rows - 15,
                "Alignment crop is excessive");
        require(result.transforms.size() == 2 && result.transforms[1].size() == cv::Size(3, 2) &&
                result.transforms[1].type() == CV_32FC1, "Translation/affine must return 2 x 3 float32 matrices");
        require(std::abs(result.transforms[1].at<float>(0, 2) - 2.25f) < 0.65f,
                "Wrong transform convention or horizontal translation");
        require(std::abs(result.transforms[1].at<float>(1, 2) + 1.75f) < 0.65f,
                "Wrong vertical translation");
        cv::Mat actual = cv::Mat::eye(3, 3, CV_32F), expected = cv::Mat::eye(3, 3, CV_32F);
        result.transforms[1].copyTo(actual.rowRange(0, 2));
        warp.copyTo(expected.rowRange(0, 2));
        checkProjection(actual, expected, image.size(), 0.4, 0.9,
                        model == mif::MotionModel::Translation ? "ECC translation" : "ECC affine");
        checkCoverage(result, {image, shifted});
        require(cv::norm(result.images.front(), image(result.crop_region), cv::NORM_INF) == 0,
                "The reference image changed during registration");
        // 已知变换生成源图后再对齐会有两次线性插值，允许纹理平滑带来的灰度误差。
        // 配准本身的几何精度由上面的原图网格误差独立约束。
        require(mae(result.images[1], image(result.crop_region)) < 18,
                "Registered source differs excessively from the reference");
    }
    const cv::Mat flat(32, 32, CV_8U, cv::Scalar(40));
    expectRegistrationFailure({flat, flat}, mif::EccRegistrationOptions{}, 1);
}

/// SIFT 应能恢复比 ECC 小运动用例更大的位移；三帧共同裁剪也必须保持有效覆盖。
void testRegistrationHomography() {
    const cv::Mat reference = registrationTexture(769, 1051);
    const cv::Mat first = (cv::Mat_<double>(3, 3) <<
        1.012, 0.013, 28.4, -0.009, 0.993, -19.1, 1.3e-5, -2.0e-5, 1.0);
    const cv::Mat second = (cv::Mat_<double>(3, 3) <<
        0.994, -0.008, -22.5, 0.006, 1.008, 17.3, -1.4e-5, 9e-6, 1.0);
    mif::SiftRegistrationOptions options;
    // 强制缩小奇数尺寸大图，使横纵实际缩放比例不同，覆盖原图坐标恢复路径。
    options.max_working_dimension = 601;
    const std::vector<cv::Mat> images{reference, transformed(reference, first), transformed(reference, second)};
    const std::vector<cv::Mat> truth{cv::Mat::eye(3, 3, CV_64F), first, second};
    // SIFT 参数不再带有 ECC 模型；估计器自身报告单应性，三帧都须返回 3×3。
    checkStack(images, truth, options, 0.50, 1.00, "SIFT downscaled three-frame homography");
    checkComplementaryFocus(mif::SiftRegistrationOptions{}, "SIFT complementary focus");
    checkSixteenBitPrecision(mif::SiftRegistrationOptions{});
}

/// ECC 从单位阵开始求解小幅透视变化，并检查缩小估计后的原图像素误差。
void testRegistrationEcc() {
    const cv::Mat reference = registrationTexture(769, 1051);
    const cv::Mat truth = (cv::Mat_<double>(3, 3) <<
        1.0018, 0.0014, 2.2, -0.0011, 0.9989, -1.4, 1.2e-6, -9e-7, 1.0);
    mif::EccRegistrationOptions options;
    options.motion_model = mif::MotionModel::Homography;
    options.max_working_dimension = 501;
    options.max_iterations = 300;
    options.convergence_tolerance = 1e-7;
    checkStack({reference, transformed(reference, truth)},
               {cv::Mat::eye(3, 3, CV_64F), truth}, options, 0.30, 0.70,
               "ECC downscaled homography");
    options.max_working_dimension = 1200;
    checkComplementaryFocus(options, "ECC complementary focus");
    mif::EccRegistrationOptions precision;
    precision.motion_model = mif::MotionModel::Homography;
    checkSixteenBitPrecision(precision);
}

/// 各方法只校验自身参数；公共工作尺寸在跳过配准时也必须合法。
void testRegistrationFailure() {
    const cv::Mat reference = registrationTexture(193, 257);
    const cv::Mat flat(reference.size(), CV_32F, cv::Scalar(0.4));
    mif::SiftRegistrationOptions sift;
    mif::EccRegistrationOptions ecc;
    ecc.motion_model = mif::MotionModel::Homography;
    for (const auto* options : std::vector<const mif::RegistrationOptionsBase*>{&sift, &ecc}) {
        expectRegistrationFailure({flat, reference}, *options, 1);
        expectRegistrationFailure({reference, flat}, *options, 2);
    }
    expectRegistrationFailure({reference, reference, flat}, ecc, 3);

    // 非常量的线性灰度坡面仍没有足够特征点，不能仅靠非零标准差宣称可配准。
    cv::Mat ramp(reference.size(), CV_32F);
    for (int y = 0; y < ramp.rows; ++y)
        for (int x = 0; x < ramp.cols; ++x)
            ramp.at<float>(y, x) = 0.2f + 0.6f * x / static_cast<float>(ramp.cols - 1);
    expectRegistrationFailure({ramp, reference}, sift, 1);
    expectRegistrationFailure({reference, ramp}, sift, 2);

    const cv::Mat small(17, 19, CV_8U, cv::Scalar(80));
    auto rejects = [&](const mif::RegistrationOptionsBase& options) {
        try {
            mif::registerImages({small, small}, options);
        } catch (const std::invalid_argument&) {
            return;
        }
        throw std::runtime_error("Invalid registration options were accepted");
    };
    for (int value : {63, 100001}) {
        mif::SiftRegistrationOptions options;
        options.max_features = value;
        rejects(options);
    }
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (int value : {0, 10001}) {
        mif::EccRegistrationOptions options;
        options.max_iterations = value;
        rejects(options);
    }
    for (int value : {15, 8193}) {
        // 三类参数共享同一个尺寸约束，包括不执行几何求解的复制路径。
        const mif::NoRegistrationOptions disabled;
        for (const auto* source : std::vector<const mif::RegistrationOptionsBase*>{&disabled, &sift, &ecc}) {
            auto options = source->clone();
            options->max_working_dimension = value;
            rejects(*options);
        }
    }
    for (double value : {-1.0, 0.0, nan, infinity}) {
        mif::EccRegistrationOptions options;
        options.convergence_tolerance = value;
        rejects(options);
    }
    for (double value : {-0.1, 0.0, 1.0, nan, infinity}) {
        mif::SiftRegistrationOptions options;
        options.match_ratio_threshold = value;
        rejects(options);
    }
    for (double value : {-1.0, 0.0, nan, infinity}) {
        mif::SiftRegistrationOptions options;
        options.ransac_reprojection_threshold = value;
        rejects(options);
    }
    for (double value : {-0.1, 0.0, 1.001, nan, infinity}) {
        mif::SiftRegistrationOptions options;
        options.min_inlier_ratio = value;
        rejects(options);
    }
    // 区间中包含的端点应可通过验证，避免把公开约定误写成严格不等式。
    for (int value : {64, 100000}) {
        mif::SiftRegistrationOptions options;
        options.max_features = value;
        options.min_inlier_ratio = 1.0;
        const auto result = mif::registerImages({reference, reference}, options);
        require(result.images.size() == 2 && result.images.front().total() > reference.total() * 0.9,
                "A valid registration option boundary was rejected");
    }
    for (int value : {1, 10000}) {
        mif::EccRegistrationOptions options;
        options.max_iterations = value;
        const auto result = mif::registerImages({reference, reference}, options);
        require(result.images.size() == 2, "A valid ECC iteration boundary was rejected");
    }
    // 只有 ECC 参数提供运动模型；非法值必须在进入求解器之前被拒绝。
    for (int value : {-1, 3, 99}) {
        mif::EccRegistrationOptions unknown;
        unknown.motion_model = static_cast<mif::MotionModel>(value);
        rejects(unknown);
    }
    // 复制路径不进入工作图缩小流程，合法尺寸端点也不改变 2×3 单位变换。
    for (int value : {16, 8192}) {
        mif::NoRegistrationOptions disabled;
        disabled.max_working_dimension = value;
        const auto result = mif::registerImages({small, small}, disabled);
        require(result.crop_region == cv::Rect(0, 0, small.cols, small.rows),
                "Copy-only registration changed the crop");
        require(result.transforms.size() == 2, "None registration omitted transform metadata");
        for (const auto& transform : result.transforms) {
            require(transform.size() == cv::Size(3, 2) && transform.type() == CV_32FC1,
                    "None registration must always return 2 x 3 float32 matrices");
            require(cv::norm(transform, cv::Mat::eye(2, 3, CV_32F), cv::NORM_INF) == 0,
                    "Copy-only registration changed an identity transform");
        }
        checkCoverage(result, {small, small});
    }

    // 独立配准入口自身负责输入校验，调用者无需先调用融合函数来检查图像。
    auto rejects_images = [](const std::vector<cv::Mat>& images) {
        try {
            mif::registerImages(images);
        } catch (const std::invalid_argument&) {
            return;
        }
        throw std::runtime_error("Standalone registration accepted invalid image input");
    };
    rejects_images({});
    rejects_images({small});
    rejects_images({small, cv::Mat()});
    rejects_images({small, cv::Mat(18, 19, CV_8U)});
    rejects_images({small, cv::Mat(small.size(), CV_16U)});
    cv::Mat nonfinite(small.size(), CV_32F, cv::Scalar(nan));
    rejects_images({nonfinite, nonfinite});

    // None 返回独立的逐图副本，并保留非连续彩色 ROI 的类型与原始像素。
    // 对重复引用同一输入的两帧，也不允许结果之间共享可写像素缓冲。
    cv::Mat storage(35, 41, CV_16UC3, cv::Scalar(10001, 20002, 30003));
    const cv::Mat roi = storage(cv::Rect(3, 4, 29, 23));
    const cv::Mat snapshot = roi.clone();
    int last = -1;
    bool saw_alignment = false;
    auto unchanged = mif::registerImages({roi, roi}, mif::NoRegistrationOptions{}, [&](int percent, const std::string& stage) {
        require(percent >= last && percent >= 0 && percent <= 100, "Registration progress is not monotonic");
        if (last == -1) require(percent == 0, "Registration progress must start at zero");
        last = percent;
        if (stage == "align") saw_alignment = true;
        return true;
    });
    require(last == 100 && !saw_alignment, "None registration reported an unexpected processing stage");
    require(unchanged.crop_region == cv::Rect(0, 0, roi.cols, roi.rows), "None registration cropped the input");
    checkCoverage(unchanged, {roi, roi});
    require(unchanged.images[0].data != unchanged.images[1].data, "None outputs share writable image data");
    unchanged.images[0].setTo(cv::Scalar::all(0));
    require(cv::norm(roi, snapshot, cv::NORM_INF) == 0 &&
            cv::norm(unchanged.images[1], snapshot, cv::NORM_INF) == 0,
            "Modifying a registered image changed its source or another result");

    // 配准自己的进度必须从零到完成保持单调，取消不能被重包装为普通求解失败。
    ecc.motion_model = mif::MotionModel::Translation;
    last = -1;
    mif::registerImages({reference, reference}, ecc, [&](int percent, const std::string&) {
        require(percent >= last && percent >= 0 && percent <= 100, "ECC progress is not monotonic");
        if (last == -1) require(percent == 0, "ECC progress must start at zero");
        last = percent;
        return true;
    });
    require(last == 100, "Standalone registration never reached completion");
    for (const std::string stage_to_cancel : {std::string("prepare"), std::string("align")}) {
        bool cancelled = false;
        try {
            mif::registerImages({reference, reference}, ecc,
                [&](int, const std::string& stage) { return stage != stage_to_cancel; });
        } catch (const mif::Cancelled&) {
            cancelled = true;
        }
        require(cancelled, "Standalone registration ignored cancellation or changed its exception type");
    }
    struct CallbackFailure : std::logic_error { using std::logic_error::logic_error; };
    bool callback_preserved = false;
    try {
        mif::registerImages({reference, reference}, ecc, [](int, const std::string& stage) {
            if (stage == "align") throw CallbackFailure("callback probe");
            return true;
        });
    } catch (const CallbackFailure&) {
        callback_preserved = true;
    }
    require(callback_preserved, "Registration replaced the caller's progress exception");

}

/// 跨可执行文件和核心 DLL 共享同一个注册表；新参数类型走完整公共采样与裁剪流程。
void testRegistrationRegistry() {
    static std::once_flag once;
    std::call_once(once, [] {
        mif::detail::registration::registerRegistrationMethod<FixedRegistrationOptions>(
            validateFixedRegistration, makeFixedEstimator);
    });
    const cv::Mat reference = texture(83, 117);
    for (bool projective : {false, true}) {
        FixedRegistrationOptions options;
        options.projective = projective;
        const auto truth = fixedTransform(options);
        const auto source = transformed(reference, truth);
        const auto snapshot = options.clone();
        // 基类引用必须保留实际参数类型，且克隆后修改原参数不能改变已排队的任务。
        options.shift_x = 31;
        const auto result = mif::registerImages({reference, source}, *snapshot);
        require(result.transforms[1].rows == (projective ? 3 : 2),
                "The pipeline ignored an extension estimator's transform model");
        cv::Mat actual = cv::Mat::eye(3, 3, CV_32F);
        result.transforms[1].copyTo(actual.rowRange(0, result.transforms[1].rows));
        checkProjection(actual, truth, reference.size(), 1e-5, 1e-4, "Registered custom method");
        checkCoverage(result, {reference, source});
        require(result.crop_region.area() < reference.total(), "Custom registration bypassed common cropping");
    }

    // 注册项复制后在锁外执行；再次登记同一参数必须失败，不能替换已有方法。
    bool duplicate_rejected = false;
    try {
        mif::detail::registration::registerRegistrationMethod<FixedRegistrationOptions>(
            validateFixedRegistration, makeFixedEstimator);
    } catch (const std::invalid_argument&) {
        duplicate_rejected = true;
    }
    require(duplicate_rejected, "A duplicate registration silently replaced an existing method");

    auto rejects = [&](const mif::RegistrationOptionsBase& options, const std::string& message) {
        try {
            mif::registerImages({reference, reference}, options);
        } catch (const std::invalid_argument& error) {
            require(std::string(error.what()).find(message) != std::string::npos,
                    "Registration rejected an extension for the wrong reason");
            return;
        }
        throw std::runtime_error("Invalid or unknown extension parameters were accepted");
    };
    const auto unknown = UnknownRegistrationOptions{}.clone();
    rejects(*unknown, "Unregistered");
    FixedRegistrationOptions invalid;
    invalid.shift_x = std::numeric_limits<double>::quiet_NaN();
    rejects(invalid, "shifts must be finite");
    invalid.shift_x = 3;
    invalid.max_working_dimension = 15;
    rejects(invalid, "working dimension");

    FixedRegistrationOptions broken;
    broken.wrong_model = true;
    expectRegistrationFailure({reference, reference}, broken, 2);
    broken.wrong_model = false;
    broken.missing_estimator = true;
    expectRegistrationFailure({reference, reference}, broken, 1);

    // 三个内置参数都须保留动态类型、方法字段和继承来的工作尺寸。
    mif::EccRegistrationOptions ecc;
    ecc.max_working_dimension = 777;
    ecc.motion_model = mif::MotionModel::Affine;
    ecc.max_iterations = 321;
    ecc.convergence_tolerance = 7e-6;
    const auto ecc_copy = ecc.clone();
    ecc.max_iterations = 1;
    const auto* copied_ecc = dynamic_cast<const mif::EccRegistrationOptions*>(ecc_copy.get());
    require(copied_ecc && copied_ecc->max_working_dimension == 777 &&
            copied_ecc->motion_model == mif::MotionModel::Affine && copied_ecc->max_iterations == 321 &&
            copied_ecc->convergence_tolerance == 7e-6, "ECC parameter clone sliced or shared state");
    mif::SiftRegistrationOptions sift;
    sift.max_working_dimension = 555;
    sift.max_features = 733;
    sift.match_ratio_threshold = 0.63;
    sift.ransac_reprojection_threshold = 2.25;
    sift.min_inlier_ratio = 0.42;
    const auto sift_copy = sift.clone();
    sift.max_features = 999;
    const auto* copied_sift = dynamic_cast<const mif::SiftRegistrationOptions*>(sift_copy.get());
    require(copied_sift && copied_sift->max_working_dimension == 555 && copied_sift->max_features == 733 &&
            copied_sift->match_ratio_threshold == 0.63 && copied_sift->ransac_reprojection_threshold == 2.25 &&
            copied_sift->min_inlier_ratio == 0.42, "SIFT parameter clone sliced or shared state");
    mif::NoRegistrationOptions disabled;
    disabled.max_working_dimension = 16;
    const auto disabled_copy = disabled.clone();
    require(dynamic_cast<const mif::NoRegistrationOptions*>(disabled_copy.get()) &&
            disabled_copy->max_working_dimension == 16, "No-registration clone lost its dynamic type");

    // 默认入口仍可直接处理 2×3 图像；复制路径不受几何工作图和裁剪的最小尺寸限制。
    const cv::Mat tiny = (cv::Mat_<unsigned short>(2, 3) << 1, 257, 65534, 10003, 20007, 30011);
    const auto copied = mif::registerImages({tiny, tiny});
    require(copied.crop_region == cv::Rect(0, 0, 3, 2), "Default registration unexpectedly cropped a tiny image");
    for (size_t i = 0; i < copied.images.size(); ++i) {
        require(copied.images[i].data != tiny.data && cv::norm(copied.images[i], tiny, cv::NORM_INF) == 0,
                "Default registration copied through a lossy numeric conversion");
        require(copied.transforms[i].size() == cv::Size(3, 2), "Default registration changed its matrix shape");
    }
}
