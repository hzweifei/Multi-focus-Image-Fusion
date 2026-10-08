#include "fixtures.hpp"
#include "fusion/dtcwt/transform.hpp"
#include <mif/fusion.hpp>
#include <algorithm>
#include <array>
#include <cmath>

namespace {

namespace wavelet = mif::detail::fusion::dtcwt;

/// 可重复的非对称样本，不依赖随机数库版本，用于核对首层及后续 Q-shift 相位。
cv::Mat oracleImage() {
    cv::Mat image(7, 9, CV_64F);
    for (int y = 0; y < image.rows; ++y)
        for (int x = 0; x < image.cols; ++x)
            image.at<double>(y, x) = ((17 * y + 13 * x) % 31) / 31.0;
    return image;
}

/// 检查独立参考输出，避免仅凭“自己的正变换能被自己的逆变换还原”判断正确性。
/// 数值由 Python dtcwt 0.14.0 的 near_sym_a / qshift_a、三层变换生成，未使用其程序代码。
void checkReferenceCoefficients() {
    const auto pyramid = wavelet::forward(oracleImage(), 3);
    const double expected[3][6][2] = {
        {{-0.25201676708279991, 0.15588120112908505},
         {-0.13195117613213247, -0.052825566548591968},
         {-0.25361753301025569, -0.051794757082765867},
         {0.032426711319274784, 0.17040621715276733},
         {0.11831411678067766, -0.063865090785483997},
         {-0.012309197538858077, -0.29747363158764945}},
        {{-0.028576343961284362, -0.13101640709729157},
         {0.021498643122622035, 0.086601971326322219},
         {0.25080251356641903, -0.14449112225621677},
         {0.25782052452438575, -0.271960769434478},
         {-0.040577110964570701, 0.13329173847835957},
         {-0.25710474322535248, 0.0082644776745443432}},
        {{0.0039710503648282894, -0.018148274871753883},
         {0.045758190808309993, 0.11246910644855734},
         {0.11181631089770276, -0.034045530351516959},
         {0.19849977933574453, -0.23215332814261036},
         {-0.11216012486967948, 0.087046027751799593},
         {-0.095801904892067941, 0.085746956318315401}}};
    const double expected_lowpass[8] = {
        1.5897864277993197, 1.7996619325460754, 1.9127812067030083, 1.8302714759268861,
        1.6838677764208134, 1.9552810302349535, 1.9789686203338328, 1.73111321953068};
    require(pyramid.highpass.size() == 3 && pyramid.lowpass.size() == cv::Size(4, 2),
            "DTCWT reference pyramid dimensions differ");
    for (int level = 0; level < 3; ++level) {
        for (int direction = 0; direction < 6; ++direction) {
            const auto actual = pyramid.highpass[level][direction].at<cv::Vec2d>(0, 0);
            for (int part = 0; part < 2; ++part)
                require(std::abs(actual[part] - expected[level][direction][part]) < 2e-12,
                        "DTCWT direction coefficient differs from independent reference");
        }
    }
    for (int index = 0; index < 8; ++index)
        require(std::abs(pyramid.lowpass.at<double>(index / 4, index % 4) - expected_lowpass[index]) < 2e-12,
                "DTCWT lowpass differs from independent reference");
}

/// 用六种斜向正弦纹理检查方向选择性；纹理法线角度与滤波器边缘方向相差 90 度。
/// 去掉边界后比较第二层的能量，六种纹理应分别选出六个不同的复方向子带。
void checkDirectionSelectivity() {
    const std::array<int, 6> expected = {2, 1, 0, 5, 4, 3};
    for (int direction = 0; direction < 6; ++direction) {
        const double angle = (15.0 + 30.0 * direction) * CV_PI / 180.0;
        cv::Mat grating(256, 256, CV_32F);
        for (int y = 0; y < grating.rows; ++y)
            for (int x = 0; x < grating.cols; ++x)
                grating.at<float>(y, x) = static_cast<float>(
                    std::sin(1.4 * (std::cos(angle) * x + std::sin(angle) * y)));
        const auto pyramid = wavelet::forward(grating, 2);
        std::array<double, 6> energy{};
        for (int band = 0; band < 6; ++band) {
            const auto& coefficients = pyramid.highpass[1][band];
            const cv::Rect interior(8, 8, coefficients.cols - 16, coefficients.rows - 16);
            energy[band] = cv::norm(coefficients(interior), cv::NORM_L2SQR);
        }
        const int winner = static_cast<int>(std::max_element(energy.begin(), energy.end()) - energy.begin());
        require(winner == expected[direction], "DTCWT directional response is incorrect");
        const double strongest = energy[winner];
        energy[winner] = 0;
        require(strongest > 3.0 * *std::max_element(energy.begin(), energy.end()),
                "DTCWT does not separate its six directional bands");
    }
}

/// 无论是否请求权重，都不把多尺度复系数选择伪装成全分辨率来源诊断。
void checkOutput(const mif::FusionResult& result, const cv::Mat& input) {
    require(result.image.size() == input.size() && result.image.type() == input.type(),
            "DTCWT output size or depth changed");
    require(cv::checkRange(result.image), "DTCWT output contains nonfinite values");
    require(result.source_index_map.empty() && result.weight_maps.empty(),
            "DTCWT unexpectedly returned spatial diagnostics");
}

} // 匿名命名空间

void testDtcwtTransform() {
    cv::RNG random(41087);
    // 同时覆盖首层奇数补边、后续两端补边、窄图及大于实际有效层数的请求。
    for (cv::Size size : {cv::Size(1, 1), {1, 9}, {2, 3}, {3, 2}, {5, 7}, {16, 12}, {193, 129}, {192, 128}}) {
        cv::Mat input(size, CV_64F);
        random.fill(input, cv::RNG::UNIFORM, -1.0, 1.0);
        const cv::Mat original = input.clone();
        for (int levels : {1, 2, 4, 16}) {
            const auto pyramid = wavelet::forward(input, levels);
            const cv::Mat lowpass_before = pyramid.lowpass.clone();
            const auto restored = wavelet::inverse(pyramid);
            require(restored.size() == size && restored.type() == CV_64FC1,
                    "DTCWT inverse did not restore the original extent");
            require(cv::norm(input, restored, cv::NORM_INF) < 3e-12,
                    "DTCWT forward/inverse round trip failed");
            require(cv::norm(input, original, cv::NORM_INF) == 0,
                    "DTCWT transform modified its input");
            require(cv::norm(pyramid.lowpass, lowpass_before, cv::NORM_INF) == 0,
                    "DTCWT inverse modified the supplied lowpass coefficients");
            require(!pyramid.highpass.empty() && pyramid.highpass.size() <= static_cast<std::size_t>(levels),
                    "DTCWT effective level count is invalid");
            for (const auto& bands : pyramid.highpass)
                for (const auto& band : bands)
                    require(!band.empty() && band.type() == CV_64FC2 && cv::checkRange(band),
                            "DTCWT did not produce six finite complex bands");
        }
    }
    checkReferenceCoefficients();
    checkDirectionSelectivity();
}

void testDtcwtFusion() {
    mif::DtcwtFusionOptions options;
    options.include_weight_maps = true;
    cv::RNG random(74471);
    // 16 位样本含非 257 倍数数值，能暴露内部误用 8 位图导致的量化损失。
    for (int type : {CV_8UC1, CV_16UC1, CV_32FC1, CV_8UC3, CV_16UC3, CV_32FC3}) {
        cv::Mat image(37, 53, type);
        const double maximum = image.depth() == CV_8U ? 255 : image.depth() == CV_16U ? 65535 : 1;
        random.fill(image, cv::RNG::UNIFORM, maximum * 0.05, maximum * 0.95);
        const auto before = image.clone();
        const auto result = mif::fuse({image, image, image}, options);
        checkOutput(result, image);
        require(cv::norm(image, result.image, cv::NORM_INF) < (image.depth() == CV_32F ? 2e-7 : 0.5),
                "DTCWT identical input reconstruction lost information");
        require(cv::norm(image, before, cv::NORM_INF) == 0, "DTCWT fusion modified its input");
    }

    const cv::Mat sharp = texture();
    auto stack = focusStack(sharp);
    const auto originals = std::vector<cv::Mat>{stack[0].clone(), stack[1].clone()};
    const auto result = mif::fuse(stack, options);
    checkOutput(result, sharp);
    const double baseline = std::min(mae(stack[0], sharp), mae(stack[1], sharp));
    require(mae(result.image, sharp) < baseline * 0.65, "DTCWT did not recover complementary focus detail");
    for (std::size_t frame = 0; frame < stack.size(); ++frame)
        require(cv::norm(stack[frame], originals[frame], cv::NORM_INF) == 0, "DTCWT changed a source frame");

    // 三帧各负责一段清晰区域，验证后续输入确实参与高频融合。
    cv::Mat blurred;
    cv::GaussianBlur(sharp, blurred, {0, 0}, 2.8);
    std::vector<cv::Mat> three;
    for (int frame = 0; frame < 3; ++frame) {
        three.push_back(blurred.clone());
        const int left = sharp.cols * frame / 3;
        const int right = sharp.cols * (frame + 1) / 3;
        const cv::Rect region(left, 0, right - left, sharp.rows);
        sharp(region).copyTo(three.back()(region));
    }
    const auto multiple = mif::fuse(three, options);
    for (int frame = 0; frame < 3; ++frame)
        require(mae(multiple.image, sharp) < 0.65 * mae(three[frame], sharp),
                "DTCWT did not combine all three focused regions");

    // 常量没有高频，结果必须是全部输入低频的算术均值，而非顺序二元均值。
    options.max_levels = 16;
    options.activity_window_size = 31;
    const std::vector<cv::Mat> constants = {
        cv::Mat(3, 5, CV_32F, cv::Scalar(0.1)), cv::Mat(3, 5, CV_32F, cv::Scalar(0.4)),
        cv::Mat(3, 5, CV_32F, cv::Scalar(0.7))};
    const auto constant = mif::fuse(constants, options);
    require(cv::norm(constant.image, cv::Mat(3, 5, CV_32F, cv::Scalar(0.4)), cv::NORM_INF) < 1e-7,
            "DTCWT lowpass does not average every input equally");
}

void testDtcwtOptions() {
    mif::DtcwtFusionOptions options;
    require(options.max_levels == 4 && options.activity_window_size == 3, "DTCWT defaults changed");
    const auto images = focusStack(texture(65, 97));
    const auto rejects = [&](const mif::DtcwtFusionOptions& invalid) {
        bool rejected = false;
        try { (void)mif::fuse(images, invalid); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "DTCWT accepted invalid selected parameters");
    };
    for (int levels : {0, 17}) {
        auto invalid = options;
        invalid.max_levels = levels;
        rejects(invalid);
    }
    for (int window : {0, 2, 32, 33}) {
        auto invalid = options;
        invalid.activity_window_size = window;
        rejects(invalid);
    }
    // 各参数对象独立；双树复小波的非法参数不能影响另一次默认引导滤波调用。
    options.max_levels = 2;
    options.activity_window_size = 1;
    checkOutput(mif::fuse(images, options), images.front());
    options = {};
    options.max_levels = 0;
    require(!mif::fuse(images).image.empty(), "Independent default fusion failed");
    rejects(options);

    options = {};
    int last = -1;
    bool saw_wavelet = false;
    (void)mif::fuse(images, options, [&](int percent, const std::string& stage) {
        require(percent >= last && percent <= 100, "DTCWT progress regressed");
        last = percent;
        saw_wavelet = saw_wavelet || stage == "dtcwt";
        return true;
    });
    require(last == 100 && saw_wavelet, "DTCWT progress did not finish");
    bool cancelled = false;
    try {
        (void)mif::fuse(images, options, [](int percent, const std::string& stage) {
            return stage != "dtcwt" || percent < 40;
        });
    } catch (const mif::Cancelled&) { cancelled = true; }
    require(cancelled, "DTCWT ignored cancellation during the transform");
    struct CallerFailure {};
    bool propagated = false;
    try {
        (void)mif::fuse(images, options, [](int, const std::string& stage) {
            if (stage == "dtcwt") throw CallerFailure{};
            return true;
        });
    } catch (const CallerFailure&) { propagated = true; }
    require(propagated, "DTCWT replaced a caller callback exception");
}
