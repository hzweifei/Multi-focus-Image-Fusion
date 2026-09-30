#include "fixtures.hpp"
#include <mif/fusion.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace {

/// 新方法的公共结果约定：完整尺寸、原输入编号，以及有限、非负、逐像素归一的权重。
void checkDiagnostics(const mif::FusionResult& result, const cv::Size& size, size_t count) {
    require(result.image.size() == size, "Traditional fusion changed input dimensions");
    require(result.focus_indices.type() == CV_32S && result.focus_indices.size() == size,
            "Traditional fusion lost int32 source diagnostics");
    require(result.weights.size() == count, "Traditional fusion lost original input weight slots");
    double minimum, maximum;
    cv::minMaxLoc(result.focus_indices, &minimum, &maximum);
    require(minimum >= 0 && maximum < static_cast<double>(count), "Source diagnostics refer to invalid input indices");
    cv::Mat total = cv::Mat::zeros(size, CV_32F);
    for (const auto& weight : result.weights) {
        require(weight.type() == CV_32F && weight.size() == size &&
                cv::checkRange(weight, true, nullptr, 0, 1.00001), "Traditional fusion returned invalid weights");
        total += weight;
    }
    require(cv::norm(total, cv::Mat(size, CV_32F, cv::Scalar(1)), cv::NORM_INF) < 1e-5,
            "Traditional fusion weights are not normalized");
}

/// 同一图像的重复输入应保持不变；不同常量图像应等权平均，不能因为无纹理变黑或偏向首帧。
void checkIdentityAndFlat(mif::FusionMethod method) {
    for (int channels : {1, 3}) {
        auto sharp = texture(25, 37);
        if (channels == 3) {
            // 三通道使用不同像素，防止只保留评分通道的错误实现也通过恒等检查。
            cv::Mat inverse = 255 - sharp, darker;
            sharp.convertTo(darker, CV_8U, 0.5);
            cv::merge(std::vector<cv::Mat>{sharp, inverse, darker}, sharp);
        }
        for (int depth : {CV_8U, CV_16U, CV_32F}) {
            const double scale = depth == CV_16U ? 257.0 : depth == CV_32F ? 1.0 / 255 : 1.0;
            cv::Mat image;
            sharp.convertTo(image, depth, scale);
            mif::FusionOptions options;
            options.method = method;
            options.keep_weight_maps = true;
            const auto result = mif::fuse({image, image, image}, options);
            checkDiagnostics(result, image.size(), 3);
            require(result.image.type() == image.type(), "Traditional fusion changed input depth or channels");
            require(cv::norm(result.image, image, cv::NORM_INF) <= (depth == CV_32F ? 2e-6 : 1),
                    "Traditional fusion does not preserve repeated identical inputs");
            for (const auto& weight : result.weights)
                require(cv::norm(weight, cv::Mat(image.size(), CV_32F, cv::Scalar(1.0 / 3)), cv::NORM_INF) < 2e-6,
                        "Identical inputs did not receive symmetric weights");
            const double range = depth == CV_8U ? 255 : depth == CV_16U ? 65535 : 1;
            std::vector<cv::Mat> flat;
            for (double value : {0.2, 0.5, 0.8})
                flat.emplace_back(image.size(), image.type(), cv::Scalar::all(value * range));
            const auto averaged = mif::fuse(flat, options);
            checkDiagnostics(averaged, image.size(), flat.size());
            require(cv::norm(averaged.image, flat[1], cv::NORM_INF) <= (depth == CV_32F ? 2e-6 : 1),
                    "Textureless inputs were not averaged");
            const cv::Mat black = cv::Mat::zeros(image.size(), image.type());
            require(cv::norm(mif::fuse({black, black}, options).image, cv::NORM_INF) == 0,
                    "All-black inputs produced invalid output");
        }
    }
    // 公共入口允许 2 像素边长；块大小或梯度内区不应把这种输入变成空图。
    const cv::Mat tiny(2, 3, CV_32F, cv::Scalar(0.4));
    mif::FusionOptions options;
    options.method = method;
    require(cv::norm(mif::fuse({tiny, tiny}, options).image, tiny, cv::NORM_INF) < 2e-6,
            "Traditional fusion rejected or corrupted a tiny valid image");
}

/// 真实互补失焦图像应比任一源图更接近清晰参考；奇数尺寸和彩色路径均参与计算。
void checkQuality(mif::FusionMethod method) {
    for (int channels : {1, 3}) {
        auto sharp = texture(129, 193);
        if (channels == 3) cv::cvtColor(sharp, sharp, cv::COLOR_GRAY2BGR);
        const auto stack = focusStack(sharp);
        mif::FusionOptions options;
        options.method = method;
        options.keep_weight_maps = true;
        const auto result = mif::fuse(stack, options);
        checkDiagnostics(result, sharp.size(), stack.size());
        require(mae(result.image, sharp) < std::min(mae(stack[0], sharp), mae(stack[1], sharp)) * 0.85,
                "Traditional fusion failed to recover complementary focused regions");
    }
}

/// 257 帧中只有最后一帧有纹理，验证选帧和诊断没有经过 uint8 的中间截断。
void checkLargeStack(mif::FusionMethod method) {
    std::vector<cv::Mat> images(257, cv::Mat::zeros(17, 19, CV_8U));
    images.back() = cv::Mat(17, 19, CV_8U);
    // 独立生成每个完整/残缺块都有方差的纹理，同时保留可被 Scharr 检出的梯度。
    // 通用 texture() 的文字会覆盖右上 3×8 残块，使其合法并列，无法用于唯一来源断言。
    for (int y = 0; y < images.back().rows; ++y)
        for (int x = 0; x < images.back().cols; ++x)
            images.back().at<unsigned char>(y, x) = static_cast<unsigned char>(20 + (17 * x + 31 * y) % 215);
    mif::FusionOptions options;
    options.method = method;
    options.keep_weight_maps = true;
    const auto result = mif::fuse(images, options);
    checkDiagnostics(result, images.front().size(), images.size());
    require(cv::countNonZero(result.focus_indices != 256) == 0, "Traditional source index was truncated or renumbered");
    require(cv::norm(result.image, images.back(), cv::NORM_INF) <= 1, "Traditional fusion lost the only textured frame");
}

/// 公共入口必须保留方法内各阶段的取消点、单调进度，以及调用者异常的原始类型和消息。
void checkProgress(mif::FusionMethod method) {
    const auto images = focusStack(texture(25, 37));
    mif::FusionOptions options;
    options.method = method;
    struct CallbackError : std::runtime_error { using std::runtime_error::runtime_error; };
    for (const std::string target : {"focus", "weights", "blend", "finish"}) {
        bool cancelled = false;
        int previous = -1;
        std::string last_stage;
        try {
            mif::fuse(images, options, [&](int percent, const std::string& stage) {
                require(percent >= previous && percent <= 100, "Traditional progress moved backwards");
                previous = percent;
                last_stage = stage;
                return stage != target;
            });
        } catch (const mif::Cancelled&) { cancelled = true; }
        require(cancelled && last_stage == target, "Traditional fusion ignored a stage cancellation");
        bool propagated = false;
        try {
            mif::fuse(images, options, [&](int, const std::string& stage) {
                if (stage == target) throw CallbackError("traditional callback sentinel");
                return true;
            });
        } catch (const CallbackError& error) {
            propagated = std::string(error.what()) == "traditional callback sentinel";
        }
        require(propagated, "Traditional fusion changed the callback exception");
    }
    int previous = -1;
    std::string last_stage;
    mif::fuse(images, options, [&](int percent, const std::string& stage) {
        require(percent >= previous && percent <= 100, "Traditional full-run progress moved backwards");
        previous = percent;
        last_stage = stage;
        return true;
    });
    require(previous == 100 && last_stage == "done", "Traditional fusion did not report completion");
}

void expectInvalid(const std::vector<cv::Mat>& images, const mif::FusionOptions& options) {
    bool rejected = false;
    try { mif::fuse(images, options); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Invalid traditional fusion options were accepted");
}

} // 匿名命名空间

void testDctFusion() {
    checkIdentityAndFlat(mif::FusionMethod::Dct);
    checkQuality(mif::FusionMethod::Dct);
    checkLargeStack(mif::FusionMethod::Dct);
    checkProgress(mif::FusionMethod::Dct);

    // 三帧交替赢得不同块，右/下边缘不足整块仍须保留；不能整体 resize 后移动块边界。
    const cv::Size size(27, 19);
    std::vector<cv::Mat> images;
    for (int i = 0; i < 3; ++i) images.emplace_back(size, CV_32F, cv::Scalar(0.5));
    cv::Mat sharp(size, CV_32F), indices(size, CV_32S);
    for (int y = 0; y < size.height; ++y) {
        for (int x = 0; x < size.width; ++x) {
            const int source = (x / 8 + y / 8) % 3;
            const float value = (x + y) % 2 ? 0.8f : 0.2f;
            images[source].at<float>(y, x) = value;
            sharp.at<float>(y, x) = value;
            indices.at<int>(y, x) = source;
        }
    }
    mif::FusionOptions options;
    options.method = mif::FusionMethod::Dct;
    options.dct.consistency_window = 1;
    options.keep_weight_maps = true;
    const auto result = mif::fuse(images, options);
    checkDiagnostics(result, size, images.size());
    require(cv::norm(result.image, sharp, cv::NORM_INF) < 1e-6, "DCT lost partial edge blocks or selected the wrong block");
    require(cv::countNonZero(result.focus_indices != indices) == 0, "DCT block boundaries shifted at odd image dimensions");

    // 7×7 块网格中只有中心块由第二帧赢得，两次中值应消除这个孤立来源块。
    cv::Mat first(56, 56, CV_32F), second(56, 56, CV_32F, cv::Scalar(0.5));
    for (int y = 0; y < first.rows; ++y)
        for (int x = 0; x < first.cols; ++x) first.at<float>(y, x) = (x + y) % 2 ? 0.8f : 0.2f;
    const cv::Rect center(24, 24, 8, 8);
    first(center).copyTo(second(center));
    first(center).setTo(0.5);
    require(mif::fuse({first, second}, options).focus_indices.at<int>(28, 28) == 1,
            "DCT consistency fixture did not create an isolated source block");
    options.dct.consistency_window = 3;
    require(cv::countNonZero(mif::fuse({first, second}, options).focus_indices) == 0,
            "DCT consistency filtering did not suppress an isolated source block");

    for (int block : {1, 129}) {
        options.dct.block_size = block;
        expectInvalid(images, options);
    }
    options.dct.block_size = 8;
    for (int window : {0, 2, 33}) {
        options.dct.consistency_window = window;
        expectInvalid(images, options);
    }
}

void testGfgfgfFusion() {
    checkIdentityAndFlat(mif::FusionMethod::Gfgfgf);
    checkQuality(mif::FusionMethod::Gfgfgf);
    checkLargeStack(mif::FusionMethod::Gfgfgf);
    checkProgress(mif::FusionMethod::Gfgfgf);

    // 保留帧位于原输入 1 和 4，中间插入无纹理帧，诊断不能错误返回压缩后的 0/1。
    const auto sharp = texture(129, 193);
    const auto stack = focusStack(sharp);
    const cv::Mat flat(sharp.size(), sharp.type(), cv::Scalar(80));
    const std::vector<cv::Mat> images{flat, stack[0], flat, flat, stack[1]};
    mif::FusionOptions options;
    options.method = mif::FusionMethod::Gfgfgf;
    // 全局筛帧属于可选扩展，论文默认比较所有焦面；此处显式启用筛帧来验证索引映射。
    options.gfgfgf.selection_ratio = 0.15;
    options.keep_weight_maps = true;
    const auto result = mif::fuse(images, options);
    checkDiagnostics(result, sharp.size(), images.size());
    for (int excluded : {0, 2, 3})
        require(cv::norm(result.weights[excluded], cv::NORM_INF) == 0, "GFG-FGF restored a filtered-out frame's weights");
    require(cv::countNonZero((result.focus_indices != 1) & (result.focus_indices != 4)) == 0,
            "GFG-FGF indices were not mapped back to original inputs");
    require(cv::countNonZero(result.focus_indices == 1) > 0 && cv::countNonZero(result.focus_indices == 4) > 0,
            "GFG-FGF failed to use both complementary retained frames");
    require(mae(result.image, sharp) < std::min(mae(stack[0], sharp), mae(stack[1], sharp)) * 0.85,
            "GFG-FGF filtering lost complementary focused content");

    // 筛选比例 1 可以只留下唯一最清晰帧；只有一个候选仍必须正常融合并保持原索引。
    cv::Mat blurred;
    cv::GaussianBlur(sharp, blurred, {0, 0}, 3);
    options.gfgfgf.selection_ratio = 1;
    const auto single = mif::fuse({flat, blurred, sharp}, options);
    require(cv::countNonZero(single.focus_indices != 2) == 0 &&
            cv::norm(single.image, sharp, cv::NORM_INF) <= 1, "GFG-FGF mishandled its sole retained frame");

    // 高阈值应回退到均值残差，不能把全部聚焦信息置零后等权平均。
    options.gfgfgf.selection_ratio = 0;
    options.gfgfgf.difference_threshold = 1;
    const auto residual = mif::fuse({flat, sharp}, options);
    checkDiagnostics(residual, sharp.size(), 2);
    require(mae(residual.image, sharp) < mae(flat, sharp) * 0.25,
            "GFG-FGF discarded the mean residual below the gradient threshold");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (const auto& invalidate : std::vector<std::function<void(mif::GfgfgfOptions&)>>{
            [](auto& o) { o.difference_window = 2; }, [](auto& o) { o.difference_window = 257; },
            [](auto& o) { o.selection_ratio = -0.1; }, [](auto& o) { o.selection_ratio = 1.1; },
            [nan](auto& o) { o.selection_ratio = nan; }, [](auto& o) { o.difference_threshold = -0.1; },
            [](auto& o) { o.difference_threshold = 1.1; }, [nan](auto& o) { o.difference_threshold = nan; },
            [](auto& o) { o.guided_radius = 0; }, [](auto& o) { o.guided_radius = 256; },
            [](auto& o) { o.guided_subsample = 0; }, [](auto& o) { o.guided_subsample = 17; },
            [](auto& o) { o.guided_epsilon = 0; }, [nan](auto& o) { o.guided_epsilon = nan; }}) {
        options.gfgfgf = {};
        invalidate(options.gfgfgf);
        expectInvalid(images, options);
    }
}
