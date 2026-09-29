#include "fixtures.hpp"
#include <mif/registration.hpp>
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
void checkCoverage(const mif::RegistrationResult& result, const std::vector<cv::Mat>& originals) {
    const cv::Size original_size = originals.front().size();
    const cv::Rect full(0, 0, original_size.width, original_size.height);
    require((result.crop & full) == result.crop && result.crop.area() > 0,
            "Crop is empty or outside the reference image");
    require(result.images.size() == originals.size() && result.transforms.size() == originals.size(),
            "Registration result does not contain every input image");
    const cv::Mat valid(original_size, CV_32F, cv::Scalar(1));
    for (size_t i = 0; i < originals.size(); ++i) {
        const auto& transform = result.transforms[i];
        const auto& image = result.images[i];
        require(image.size() == result.crop.size(), "Registered image does not follow the reported crop");
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
        cv::minMaxLoc(coverage(result.crop), &minimum);
        require(minimum >= 0.9999, "Crop contains pixels mixed with an invalid source border");
        // 检查每张返回图的像素确实对应其变换与裁剪信息；只验证矩阵或融合结果会漏掉错帧。
        cv::Mat expected;
        resampled(result.crop).convertTo(expected, originals[i].type(), range);
        require(cv::norm(image, expected, cv::NORM_INF) <= (image.depth() == CV_32F ? 1e-6 : 1.0),
                "Registered pixels do not match their transform and crop metadata");
    }
}

/// 从独立配准入口执行测试，并确认配准与裁剪均未修改调用者输入。
void checkStack(const std::vector<cv::Mat>& images, const std::vector<cv::Mat>& truth,
                const mif::RegistrationOptions& options, double mean_limit, double maximum_limit,
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
    const double retained = static_cast<double>(result.crop.area()) /
                            static_cast<double>(images.front().total());
    require(retained > 0.70 && retained < 1.0, label + ": unexpectedly large or absent crop");
}

/// 左右互补的轻度失焦仍保留可匹配的轮廓；使用已知几何变换检查焦点变化下的配准。
/// 该用例验证几何精度，不以融合结果更接近第一张输入作为成功依据。
void checkComplementaryFocus(mif::RegistrationMethod method, const std::string& label) {
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
    mif::RegistrationOptions options;
    options.method = method;
    options.motion_model = mif::MotionModel::Homography;
    options.iterations = 300;
    options.epsilon = 1e-7;
    checkStack({reference, transformed(source_focus, truth)},
               {cv::Mat::eye(3, 3, CV_64F), truth}, options, 0.45, 0.90, label);
}

/// 完全相同的高位深图像无需几何重采样损失；检查数值恢复，防止 SIFT 的 8 位
/// 特征预处理错误地替换原始像素。只检查输出类型无法发现这种精度丢失。
void checkSixteenBitPrecision(mif::RegistrationMethod method) {
    cv::Mat image;
    registrationTexture(257, 383).convertTo(image, CV_16U, 65535.0);
    const cv::Mat before = image.clone();
    size_t low_bit_pixels = 0;
    for (int y = 0; y < image.rows; ++y)
        for (int x = 0; x < image.cols; ++x)
            if (image.at<unsigned short>(y, x) % 257 != 0) ++low_bit_pixels;
    require(low_bit_pixels > image.total() / 2, "Precision fixture lacks meaningful 16-bit values");
    mif::RegistrationOptions options;
    options.method = method;
    options.motion_model = mif::MotionModel::Homography;
    const auto result = mif::registerImages({image, image}, options);
    require(cv::norm(image, before, cv::NORM_INF) == 0, "16-bit source was modified");
    checkCoverage(result, {image, image});
    require(result.crop.area() > 0.9 * static_cast<double>(image.total()),
            "Identical 16-bit inputs lost a large image area");
    for (const auto& registered : result.images) {
        require(registered.type() == CV_16UC1, "Registration reduced the output bit depth");
        require(cv::norm(registered, image(result.crop), cv::NORM_INF) <= 2,
                "Registration lost low-order information in identical 16-bit inputs");
    }
}

/// 配准失败必须有可定位到输入的序号；公开错误约定使用从 1 开始的编号。
void expectRegistrationFailure(const std::vector<cv::Mat>& images, mif::RegistrationMethod method,
                               int expected_index, mif::MotionModel model = mif::MotionModel::Homography) {
    mif::RegistrationOptions options;
    options.method = method;
    options.motion_model = model;
    try {
        mif::registerImages(images, options);
    } catch (const std::runtime_error& error) {
        require(std::string(error.what()).find("image " + std::to_string(expected_index)) != std::string::npos,
                std::string("Missing one-based failing image index: ") + error.what());
        return;
    }
    throw std::runtime_error("Unreliable registration unexpectedly succeeded");
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
        mif::RegistrationOptions options;
        options.method = mif::RegistrationMethod::Ecc;
        options.motion_model = model;
        const auto result = mif::registerImages({image, shifted}, options);
        require(result.crop.area() < image.rows * image.cols, "Alignment did not crop invalid borders");
        require(result.crop.width > image.cols - 15 && result.crop.height > image.rows - 15,
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
        require(cv::norm(result.images.front(), image(result.crop), cv::NORM_INF) == 0,
                "The reference image changed during registration");
        // 已知变换生成源图后再对齐会有两次线性插值，允许纹理平滑带来的灰度误差。
        // 配准本身的几何精度由上面的原图网格误差独立约束。
        require(mae(result.images[1], image(result.crop)) < 18,
                "Registered source differs excessively from the reference");
    }
    const cv::Mat flat(32, 32, CV_8U, cv::Scalar(40));
    expectRegistrationFailure({flat, flat}, mif::RegistrationMethod::Ecc, 1,
                              mif::MotionModel::Translation);
}

/// SIFT 应能恢复比 ECC 小运动用例更大的位移；三帧共同裁剪也必须保持有效覆盖。
void testRegistrationHomography() {
    const cv::Mat reference = registrationTexture(769, 1051);
    const cv::Mat first = (cv::Mat_<double>(3, 3) <<
        1.012, 0.013, 28.4, -0.009, 0.993, -19.1, 1.3e-5, -2.0e-5, 1.0);
    const cv::Mat second = (cv::Mat_<double>(3, 3) <<
        0.994, -0.008, -22.5, 0.006, 1.008, 17.3, -1.4e-5, 9e-6, 1.0);
    mif::RegistrationOptions options;
    options.method = mif::RegistrationMethod::Sift;
    // 强制缩小奇数尺寸大图，使横纵实际缩放比例不同，覆盖原图坐标恢复路径。
    options.max_size = 601;
    const std::vector<cv::Mat> images{reference, transformed(reference, first), transformed(reference, second)};
    const std::vector<cv::Mat> truth{cv::Mat::eye(3, 3, CV_64F), first, second};
    // SIFT 固定估计单应性，不能因合法但未使用的 ECC 模型设置而降为平移或仿射。
    // 每种设置都检查三帧的 3×3 输出、原图网格误差和共同裁剪后的逐像素覆盖。
    for (auto model : {mif::MotionModel::Translation, mif::MotionModel::Affine, mif::MotionModel::Homography}) {
        options.motion_model = model;
        checkStack(images, truth, options, 0.50, 1.00,
                   "SIFT downscaled three-frame homography, model " + std::to_string(static_cast<int>(model)));
    }
    checkComplementaryFocus(options.method, "SIFT complementary focus");
    checkSixteenBitPrecision(options.method);
}

/// ECC 从单位阵开始求解小幅透视变化，并检查缩小估计后的原图像素误差。
void testRegistrationEcc() {
    const cv::Mat reference = registrationTexture(769, 1051);
    const cv::Mat truth = (cv::Mat_<double>(3, 3) <<
        1.0018, 0.0014, 2.2, -0.0011, 0.9989, -1.4, 1.2e-6, -9e-7, 1.0);
    mif::RegistrationOptions options;
    options.method = mif::RegistrationMethod::Ecc;
    options.motion_model = mif::MotionModel::Homography;
    options.max_size = 501;
    options.iterations = 300;
    options.epsilon = 1e-7;
    checkStack({reference, transformed(reference, truth)},
               {cv::Mat::eye(3, 3, CV_64F), truth}, options, 0.30, 0.70,
               "ECC downscaled homography");
    checkComplementaryFocus(options.method, "ECC complementary focus");
    checkSixteenBitPrecision(options.method);
}

/// 两条配准路径都应拒绝无信息输入；配置校验必须在禁用配准时仍然生效。
void testRegistrationFailure() {
    const cv::Mat reference = registrationTexture(193, 257);
    const cv::Mat flat(reference.size(), CV_32F, cv::Scalar(0.4));
    for (auto method : {mif::RegistrationMethod::Sift, mif::RegistrationMethod::Ecc}) {
        expectRegistrationFailure({flat, reference}, method, 1);
        expectRegistrationFailure({reference, flat}, method, 2);
    }
    expectRegistrationFailure({reference, reference, flat}, mif::RegistrationMethod::Ecc, 3);

    // 非常量的线性灰度坡面仍没有足够特征点，不能仅靠非零标准差宣称可配准。
    cv::Mat ramp(reference.size(), CV_32F);
    for (int y = 0; y < ramp.rows; ++y)
        for (int x = 0; x < ramp.cols; ++x)
            ramp.at<float>(y, x) = 0.2f + 0.6f * x / static_cast<float>(ramp.cols - 1);
    expectRegistrationFailure({ramp, reference}, mif::RegistrationMethod::Sift, 1);
    expectRegistrationFailure({reference, ramp}, mif::RegistrationMethod::Sift, 2);

    const cv::Mat small(17, 19, CV_8U, cv::Scalar(80));
    auto rejects = [&](const mif::RegistrationOptions& options) {
        try {
            mif::registerImages({small, small}, options);
        } catch (const std::invalid_argument&) {
            return;
        }
        throw std::runtime_error("Invalid registration options were accepted");
    };
    for (int value : {63, 100001}) {
        mif::RegistrationOptions options;
        options.max_features = value;
        rejects(options);
    }
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (int value : {0, 10001}) {
        mif::RegistrationOptions options;
        options.iterations = value;
        rejects(options);
    }
    for (int value : {15, 8193}) {
        mif::RegistrationOptions options;
        options.max_size = value;
        rejects(options);
    }
    for (double value : {-1.0, 0.0, nan, infinity}) {
        mif::RegistrationOptions options;
        options.epsilon = value;
        rejects(options);
    }
    for (double value : {-0.1, 0.0, 1.0, nan, infinity}) {
        mif::RegistrationOptions options;
        options.match_ratio = value;
        rejects(options);
    }
    for (double value : {-1.0, 0.0, nan, infinity}) {
        mif::RegistrationOptions options;
        options.ransac_threshold = value;
        rejects(options);
    }
    for (double value : {-0.1, 0.0, 1.001, nan, infinity}) {
        mif::RegistrationOptions options;
        options.min_inlier_ratio = value;
        rejects(options);
    }
    // 区间中包含的端点应可通过验证，避免把公开约定误写成严格不等式。
    for (int value : {64, 100000}) {
        mif::RegistrationOptions options;
        options.max_features = value;
        options.min_inlier_ratio = 1.0;
        const auto result = mif::registerImages({small, small}, options);
        require(result.images.size() == 2 && result.images.front().size() == small.size(),
                "A valid registration option boundary was rejected");
    }
    for (int value : {-1, 3, 99}) {
        mif::RegistrationOptions unknown;
        unknown.method = static_cast<mif::RegistrationMethod>(value);
        rejects(unknown);
    }
    // 运动模型是独立枚举；None 和 SIFT 虽不使用它，仍须在求解前拒绝非法值。
    for (auto method : {mif::RegistrationMethod::None, mif::RegistrationMethod::Ecc,
                        mif::RegistrationMethod::Sift}) {
        for (int value : {-1, 3, 99}) {
            mif::RegistrationOptions unknown;
            unknown.method = method;
            unknown.motion_model = static_cast<mif::MotionModel>(value);
            rejects(unknown);
        }
    }
    // 关闭配准时任何合法模型都不执行求解，也不能改变单位变换的 2×3 输出约定。
    for (auto model : {mif::MotionModel::Translation, mif::MotionModel::Affine, mif::MotionModel::Homography}) {
        mif::RegistrationOptions disabled;
        disabled.motion_model = model;
        const auto result = mif::registerImages({small, small}, disabled);
        require(result.crop == cv::Rect(0, 0, small.cols, small.rows),
                "An unused motion model changed the None crop");
        require(result.transforms.size() == 2, "None registration omitted transform metadata");
        for (const auto& transform : result.transforms) {
            require(transform.size() == cv::Size(3, 2) && transform.type() == CV_32FC1,
                    "None registration must always return 2 x 3 float32 matrices");
            require(cv::norm(transform, cv::Mat::eye(2, 3, CV_32F), cv::NORM_INF) == 0,
                    "An unused motion model changed a None transform");
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
    auto unchanged = mif::registerImages({roi, roi}, {}, [&](int percent, const std::string& stage) {
        require(percent >= last && percent >= 0 && percent <= 100, "Registration progress is not monotonic");
        if (last == -1) require(percent == 0, "Registration progress must start at zero");
        last = percent;
        if (stage == "align") saw_alignment = true;
        return true;
    });
    require(last == 100 && !saw_alignment, "None registration reported an unexpected processing stage");
    require(unchanged.crop == cv::Rect(0, 0, roi.cols, roi.rows), "None registration cropped the input");
    checkCoverage(unchanged, {roi, roi});
    require(unchanged.images[0].data != unchanged.images[1].data, "None outputs share writable image data");
    unchanged.images[0].setTo(cv::Scalar::all(0));
    require(cv::norm(roi, snapshot, cv::NORM_INF) == 0 &&
            cv::norm(unchanged.images[1], snapshot, cv::NORM_INF) == 0,
            "Modifying a registered image changed its source or another result");

    // 配准自己的进度必须从零到完成保持单调，取消不能被重包装为普通求解失败。
    mif::RegistrationOptions ecc;
    ecc.method = mif::RegistrationMethod::Ecc;
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
