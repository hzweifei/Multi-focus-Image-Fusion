#include "fixtures.hpp"
#include <mif/fusion.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
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
void checkCoverage(const mif::FusionResult& result, cv::Size original_size) {
    const cv::Rect full(0, 0, original_size.width, original_size.height);
    require((result.crop & full) == result.crop && result.crop.area() > 0,
            "Crop is empty or outside the reference image");
    require(result.image.size() == result.crop.size(), "Output does not follow the reported crop");
    require(result.focus_indices.size() == result.crop.size() && result.focus_indices.type() == CV_32SC1,
            "Focus indices do not follow the reported crop");
    const cv::Mat valid(original_size, CV_32F, cv::Scalar(1));
    for (const auto& transform : result.transforms) {
        cv::Mat coverage;
        cv::warpPerspective(valid, coverage, transform, original_size,
                            cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT);
        double minimum;
        cv::minMaxLoc(coverage(result.crop), &minimum);
        require(minimum >= 0.9999, "Crop contains pixels mixed with an invalid source border");
    }
}

/// 从公开入口执行测试，并确认配准、裁剪及融合均未修改调用者输入。
void checkStack(const std::vector<cv::Mat>& images, const std::vector<cv::Mat>& truth,
                const mif::FusionOptions& options, double mean_limit, double maximum_limit,
                const std::string& label) {
    std::vector<cv::Mat> before;
    for (const auto& image : images) before.push_back(image.clone());
    const auto result = mif::fuse(images, options);
    require(result.image.type() == images.front().type(), label + ": output depth/channels changed");
    require(result.transforms.size() == images.size(), label + ": transform count changed");
    require(truth.size() == images.size(), "Invalid registration test fixture");
    for (size_t i = 0; i < images.size(); ++i) {
        require(cv::norm(images[i], before[i], cv::NORM_INF) == 0, label + ": source pixels changed");
        checkProjection(result.transforms[i], truth[i], images[i].size(),
                        i == 0 ? 1e-6 : mean_limit, i == 0 ? 1e-6 : maximum_limit,
                        label + " image " + std::to_string(i + 1));
    }
    checkCoverage(result, images.front().size());
    const double retained = static_cast<double>(result.crop.area()) /
                            static_cast<double>(images.front().total());
    require(retained > 0.70 && retained < 1.0, label + ": unexpectedly large or absent crop");
}

/// 左右互补的轻度失焦仍保留可匹配的轮廓；使用已知几何变换检查焦点变化下的配准。
/// 该用例验证几何精度，不以融合结果更接近第一张输入作为成功依据。
void checkComplementaryFocus(mif::Alignment alignment, const std::string& label) {
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
    mif::FusionOptions options;
    options.alignment = alignment;
    options.alignment_iterations = 300;
    options.alignment_epsilon = 1e-7;
    checkStack({reference, transformed(source_focus, truth)},
               {cv::Mat::eye(3, 3, CV_64F), truth}, options, 0.45, 0.90, label);
}

/// 完全相同的高位深图像无需几何重采样损失；检查数值恢复，防止 SIFT 的 8 位
/// 特征预处理错误地替换最终融合输入。只检查输出类型无法发现这种精度丢失。
void checkSixteenBitPrecision(mif::Alignment alignment) {
    cv::Mat image;
    registrationTexture(257, 383).convertTo(image, CV_16U, 65535.0);
    const cv::Mat before = image.clone();
    size_t low_bit_pixels = 0;
    for (int y = 0; y < image.rows; ++y)
        for (int x = 0; x < image.cols; ++x)
            if (image.at<unsigned short>(y, x) % 257 != 0) ++low_bit_pixels;
    require(low_bit_pixels > image.total() / 2, "Precision fixture lacks meaningful 16-bit values");
    mif::FusionOptions options;
    options.alignment = alignment;
    const auto result = mif::fuse({image, image}, options);
    require(result.image.type() == CV_16UC1, "Registration reduced the output bit depth");
    require(cv::norm(image, before, cv::NORM_INF) == 0, "16-bit source was modified");
    checkCoverage(result, image.size());
    require(result.crop.area() > 0.9 * static_cast<double>(image.total()),
            "Identical 16-bit inputs lost a large image area");
    require(cv::norm(result.image, image(result.crop), cv::NORM_INF) <= 2,
            "Registration lost low-order information in identical 16-bit inputs");
}

/// 配准失败必须有可定位到输入的序号；公开错误约定使用从 1 开始的编号。
void expectRegistrationFailure(const std::vector<cv::Mat>& images, mif::Alignment alignment,
                               int expected_index) {
    mif::FusionOptions options;
    options.alignment = alignment;
    try {
        mif::fuse(images, options);
    } catch (const std::runtime_error& error) {
        require(std::string(error.what()).find("image " + std::to_string(expected_index)) != std::string::npos,
                std::string("Missing one-based failing image index: ") + error.what());
        return;
    }
    throw std::runtime_error("Unreliable registration unexpectedly succeeded");
}

} // 匿名命名空间

/// SIFT 应能恢复比 ECC 小运动用例更大的位移；三帧共同裁剪也必须保持有效覆盖。
void testRegistrationHomography() {
    const cv::Mat reference = registrationTexture(769, 1051);
    const cv::Mat first = (cv::Mat_<double>(3, 3) <<
        1.012, 0.013, 28.4, -0.009, 0.993, -19.1, 1.3e-5, -2.0e-5, 1.0);
    const cv::Mat second = (cv::Mat_<double>(3, 3) <<
        0.994, -0.008, -22.5, 0.006, 1.008, 17.3, -1.4e-5, 9e-6, 1.0);
    mif::FusionOptions options;
    options.alignment = mif::Alignment::FeatureHomography;
    // 强制缩小奇数尺寸大图，使横纵实际缩放比例不同，覆盖原图坐标恢复路径。
    options.alignment_max_size = 601;
    checkStack({reference, transformed(reference, first), transformed(reference, second)},
               {cv::Mat::eye(3, 3, CV_64F), first, second}, options, 0.50, 1.00,
               "SIFT downscaled three-frame homography");
    checkComplementaryFocus(options.alignment, "SIFT complementary focus");
    checkSixteenBitPrecision(options.alignment);
}

/// ECC 从单位阵开始求解小幅透视变化，并检查缩小估计后的原图像素误差。
void testRegistrationEcc() {
    const cv::Mat reference = registrationTexture(769, 1051);
    const cv::Mat truth = (cv::Mat_<double>(3, 3) <<
        1.0018, 0.0014, 2.2, -0.0011, 0.9989, -1.4, 1.2e-6, -9e-7, 1.0);
    mif::FusionOptions options;
    options.alignment = mif::Alignment::EccHomography;
    options.alignment_max_size = 501;
    options.alignment_iterations = 300;
    options.alignment_epsilon = 1e-7;
    checkStack({reference, transformed(reference, truth)},
               {cv::Mat::eye(3, 3, CV_64F), truth}, options, 0.30, 0.70,
               "ECC downscaled homography");
    checkComplementaryFocus(options.alignment, "ECC complementary focus");
    checkSixteenBitPrecision(options.alignment);
}

/// 两条配准路径都应拒绝无信息输入；配置校验必须在禁用配准时仍然生效。
void testRegistrationFailure() {
    const cv::Mat reference = registrationTexture(193, 257);
    const cv::Mat flat(reference.size(), CV_32F, cv::Scalar(0.4));
    for (auto alignment : {mif::Alignment::FeatureHomography, mif::Alignment::EccHomography}) {
        expectRegistrationFailure({flat, reference}, alignment, 1);
        expectRegistrationFailure({reference, flat}, alignment, 2);
    }
    expectRegistrationFailure({reference, reference, flat}, mif::Alignment::EccHomography, 3);

    // 非常量的线性灰度坡面仍没有足够特征点，不能仅靠非零标准差宣称可配准。
    cv::Mat ramp(reference.size(), CV_32F);
    for (int y = 0; y < ramp.rows; ++y)
        for (int x = 0; x < ramp.cols; ++x)
            ramp.at<float>(y, x) = 0.2f + 0.6f * x / static_cast<float>(ramp.cols - 1);
    expectRegistrationFailure({ramp, reference}, mif::Alignment::FeatureHomography, 1);
    expectRegistrationFailure({reference, ramp}, mif::Alignment::FeatureHomography, 2);

    const cv::Mat small(17, 19, CV_8U, cv::Scalar(80));
    auto rejects = [&](const mif::FusionOptions& options) {
        require(options.alignment == mif::Alignment::None, "Invalid validation test fixture");
        try {
            mif::fuse({small, small}, options);
        } catch (const std::invalid_argument&) {
            return;
        }
        throw std::runtime_error("Invalid registration options were accepted while alignment was disabled");
    };
    for (int value : {63, 100001}) {
        mif::FusionOptions options;
        options.alignment_max_features = value;
        rejects(options);
    }
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (double value : {-0.1, 0.0, 1.0, nan, infinity}) {
        mif::FusionOptions options;
        options.alignment_match_ratio = value;
        rejects(options);
    }
    for (double value : {-1.0, 0.0, nan, infinity}) {
        mif::FusionOptions options;
        options.alignment_ransac_threshold = value;
        rejects(options);
    }
    for (double value : {-0.1, 0.0, 1.001, nan, infinity}) {
        mif::FusionOptions options;
        options.alignment_min_inlier_ratio = value;
        rejects(options);
    }
    // 区间中包含的端点应可通过验证，避免把公开约定误写成严格不等式。
    for (int value : {64, 100000}) {
        mif::FusionOptions options;
        options.alignment_max_features = value;
        options.alignment_min_inlier_ratio = 1.0;
        const auto result = mif::fuse({small, small}, options);
        require(result.image.size() == small.size(), "A valid registration option boundary was rejected");
    }

    // 使用新增特征参数之前的完整聚合初始化顺序，末尾 true 必须仍对应保留权重。
    // 新字段只能追加在旧字段之后，否则这类现有 C++ 调用可能静默改变含义。
    const mif::FusionOptions legacy{
        mif::FusionMethod::GuidedFilter, mif::FocusMeasure::ModifiedLaplacian, mif::Alignment::None,
        9, 15, 3, 0.01, 0.0001, 5, 150, 1e-5, 1200, true};
    require(legacy.keep_weight_maps && legacy.alignment_max_features == 4000,
            "Legacy aggregate initialization changed parameter meanings");
    require(mif::fuse({small, small}, legacy).weights.size() == 2,
            "Legacy aggregate initialization no longer retains weight maps");
}
