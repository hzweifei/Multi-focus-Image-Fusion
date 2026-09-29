#include "fixtures.hpp"
#include <mif/fusion.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <map>

// 核心算法回归测试入口；CTest 通过用例名分别运行，失败原因可独立定位。
void testFocusMeasure();
void testRegistrationAlignment();
void testPipeline();
void testRegistrationHomography();
void testRegistrationEcc();
void testRegistrationFailure();
void testDctFusion();
void testGfgfgfFusion();
void testDtcwtTransform();
void testDtcwtFusion();
void testDtcwtOptions();
void testGuidedFilterNumerics();
namespace {
const std::vector<mif::FusionMethod> methods{mif::FusionMethod::GuidedFilter, mif::FusionMethod::LaplacianPyramid};

// 互补清晰区域应被恢复：融合误差明显低于任一源图，并保持尺寸和顺序对称性。
void quality() {
    for (int channels : {1, 3}) {
        auto sharp = texture();
        if (channels == 3) cv::cvtColor(sharp, sharp, cv::COLOR_GRAY2BGR);
        const auto stack = focusStack(sharp);
        for (auto method : methods) {
            for (auto measure : {mif::FocusMeasure::ModifiedLaplacian, mif::FocusMeasure::Tenengrad}) {
                mif::FusionOptions o; o.method = method;
                o.guided_filter.focus.measure = measure;
                o.laplacian_pyramid.focus.measure = measure;
                const auto output = mif::fuse(stack, o);
                const double baseline = std::min(mae(stack[0], sharp), mae(stack[1], sharp));
                const double error = mae(output.image, sharp);
                std::cout << "MAE fused=" << error << " source=" << baseline << '\n';
                require(error < baseline * 0.65, "Fusion should recover complementary sharp regions");
                require(output.image.size() == sharp.size(), "Odd image dimensions changed");
                require(output.weights.empty(), "Weights retained without request");
                auto reversed = stack; std::reverse(reversed.begin(), reversed.end());
                require(mae(output.image, mif::fuse(reversed, o).image) < 0.01, "Fusion changed when source order changed");
            }
        }
    }
}

// 相同图像重复输入时保持像素、位深和通道，且不修改源数据。
// 同时覆盖浮点范围端点、小图，以及请求层数超过图像可建金字塔层数的情况。
void identity() {
    for (float value : {0.0f, 1.0f}) {
        const cv::Mat boundary(4, 5, CV_32FC3, cv::Scalar::all(value));
        require(cv::norm(mif::fuse({boundary, boundary}).image, boundary, cv::NORM_INF) < 1e-6,
                "Float range endpoint was rejected or changed");
    }
    for (int depth : {CV_8U, CV_16U, CV_32F}) {
        for (int channels : {1, 3}) {
            cv::Mat image;
            texture(37, 51).convertTo(image, depth, depth == CV_16U ? 257 : depth == CV_32F ? 1.0 / 255 : 1);
            if (channels == 3) cv::cvtColor(image, image, cv::COLOR_GRAY2BGR);
            const cv::Mat before = image.clone();
            for (auto method : methods) {
                mif::FusionOptions o; o.method = method; o.laplacian_pyramid.levels = 16;
                const auto result = mif::fuse({image, image, image}, o);
                require(result.image.type() == image.type(), "Depth/channels were not preserved");
                require(cv::norm(result.image, image, cv::NORM_INF) <= (depth == CV_32F ? 2e-6 : 1), "Identical inputs changed");
                require(cv::norm(image, before, cv::NORM_INF) == 0, "Input was modified");
                cv::Mat tiny(2, 3, image.type(), cv::Scalar::all(depth == CV_32F ? 0.4 : 100));
                require(mae(mif::fuse({tiny, tiny}, o).image, tiny) < 0.01, "Tiny image failed");
            }
        }
    }
}

// 核心必须拒绝非法图像与参数，并使用 invalid_argument 明确区分输入错误。
void validation() {
    const auto image = texture();
    auto rejects = [](const std::function<void()>& call) {
        try { call(); } catch (const std::invalid_argument&) { return; }
        throw std::runtime_error("Invalid input was accepted");
    };
    rejects([&] { mif::fuse({}); });
    rejects([&] { mif::fuse({image}); });
    rejects([&] { mif::fuse({image, cv::Mat()}); });
    rejects([&] { mif::fuse({image, texture(32, 32)}); });
    rejects([&] { mif::fuse({image, cv::Mat(image.size(), CV_16U)}); });
    rejects([&] { cv::Mat rgba(32, 32, CV_8UC4); mif::fuse({rgba, rgba}); });
    rejects([&] { cv::Mat bad(32, 32, CV_32F, cv::Scalar(std::numeric_limits<float>::quiet_NaN())); mif::fuse({bad, bad}); });
    rejects([&] { cv::Mat bad(32, 32, CV_32F, cv::Scalar(1.1)); mif::fuse({bad, bad}); });
    rejects([&] { cv::Mat bad(32, 32, CV_32F, cv::Scalar(std::nextafter(1.0f, 2.0f))); mif::fuse({bad, bad}); });
    rejects([&] { cv::Mat bad(32, 32, CV_32F, cv::Scalar(-0.1)); mif::fuse({bad, bad}); });
    rejects([&] { mif::FusionOptions o; o.guided_filter.focus.window = 8; mif::fuse({image, image}, o); });
    rejects([&] { mif::FusionOptions o; o.guided_filter.detail_epsilon = 0; mif::fuse({image, image}, o); });
    rejects([&] { mif::FusionOptions o; o.method = mif::FusionMethod::LaplacianPyramid;
                  o.laplacian_pyramid.levels = 0; mif::fuse({image, image}, o); });
    rejects([&] { mif::FusionOptions o; o.method = static_cast<mif::FusionMethod>(99); mif::fuse({image, image}, o); });
    // 各方法分别维护自己的约束，逐项检查可发现拆分时漏掉某个字段的校验。
    for (const auto method : methods) {
        auto reject_option = [&](const std::function<void(mif::FusionOptions&)>& change) {
            mif::FusionOptions options;
            options.method = method;
            change(options);
            rejects([&] { mif::fuse({image, image}, options); });
        };
        auto focus = [method](mif::FusionOptions& options) -> mif::FocusOptions& {
            return method == mif::FusionMethod::GuidedFilter ?
                   options.guided_filter.focus : options.laplacian_pyramid.focus;
        };
        reject_option([&](auto& options) { focus(options).measure = static_cast<mif::FocusMeasure>(99); });
        for (int value : {0, 2, 256})
            reject_option([&](auto& options) { focus(options).window = value; });
        for (int value : {0, 256}) {
            reject_option([&](auto& options) {
                if (method == mif::FusionMethod::GuidedFilter) options.guided_filter.detail_radius = value;
                else options.laplacian_pyramid.detail_radius = value;
            });
            if (method == mif::FusionMethod::GuidedFilter)
                reject_option([&](auto& options) { options.guided_filter.base_radius = value; });
        }
        for (double value : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity()}) {
            reject_option([&](auto& options) {
                if (method == mif::FusionMethod::GuidedFilter) options.guided_filter.detail_epsilon = value;
                else options.laplacian_pyramid.detail_epsilon = value;
            });
            if (method == mif::FusionMethod::GuidedFilter)
                reject_option([&](auto& options) { options.guided_filter.base_epsilon = value; });
        }
        if (method == mif::FusionMethod::LaplacianPyramid)
            for (int value : {0, 17})
                reject_option([&](auto& options) { options.laplacian_pyramid.levels = value; });
    }
}

// 两种方法拥有各自的参数和值语义；未选中的参数既不参与计算，也不参与校验。
void methodOptions() {
    const mif::FusionOptions defaults;
    require(defaults.method == mif::FusionMethod::GuidedFilter && !defaults.keep_weight_maps,
            "Fusion entry defaults changed");
    for (const auto& focus : {defaults.guided_filter.focus, defaults.laplacian_pyramid.focus})
        require(focus.measure == mif::FocusMeasure::ModifiedLaplacian && focus.window == 9,
                "A method's focus defaults changed");
    require(defaults.guided_filter.base_radius == 15 && defaults.guided_filter.detail_radius == 3 &&
            defaults.guided_filter.base_epsilon == 0.01 && defaults.guided_filter.detail_epsilon == 0.0001,
            "Guided-filter defaults changed");
    require(defaults.laplacian_pyramid.detail_radius == 3 &&
            defaults.laplacian_pyramid.detail_epsilon == 0.0001 && defaults.laplacian_pyramid.levels == 5,
            "Laplacian-pyramid defaults changed");
    auto independent = defaults;
    independent.guided_filter.focus.window = 3;
    independent.laplacian_pyramid.focus.measure = mif::FocusMeasure::Tenengrad;
    require(defaults.guided_filter.focus.window == 9 && independent.laplacian_pyramid.focus.window == 9 &&
            defaults.laplacian_pyramid.focus.measure == mif::FocusMeasure::ModifiedLaplacian &&
            independent.guided_filter.focus.measure == mif::FocusMeasure::ModifiedLaplacian,
            "Method configurations share mutable focus settings");

    auto equal = [](const mif::FusionResult& a, const mif::FusionResult& b) {
        require(a.image.type() == b.image.type() && a.image.size() == b.image.size() &&
                cv::norm(a.image, b.image, cv::NORM_INF) == 0, "Inactive settings changed fused pixels");
        require(cv::norm(a.focus_indices, b.focus_indices, cv::NORM_INF) == 0,
                "Inactive settings changed source indices");
        require(a.weights.size() == b.weights.size(), "Inactive settings changed diagnostic count");
        for (size_t i = 0; i < a.weights.size(); ++i)
            require(cv::norm(a.weights[i], b.weights[i], cv::NORM_INF) == 0,
                    "Inactive settings changed diagnostic weights");
    };
    const auto sharp = texture();
    const auto images = focusStack(sharp);
    const double source_error = std::min(mae(images[0], sharp), mae(images[1], sharp));
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (const auto method : methods) {
        mif::FusionOptions options;
        options.method = method;
        options.keep_weight_maps = true;
        const auto original = mif::fuse(images, options);
        // 两组配置故意使用不同清晰度、窗口和权重参数，验证入口读取对应的完整参数组。
        options.guided_filter.focus = {mif::FocusMeasure::Tenengrad, 5};
        options.guided_filter.base_radius = 7;
        options.guided_filter.detail_radius = 2;
        options.guided_filter.base_epsilon = 0.02;
        options.guided_filter.detail_epsilon = 0.002;
        options.laplacian_pyramid.focus = {mif::FocusMeasure::ModifiedLaplacian, 7};
        options.laplacian_pyramid.detail_radius = 4;
        options.laplacian_pyramid.detail_epsilon = 0.003;
        options.laplacian_pyramid.levels = 3;
        const auto configured = mif::fuse(images, options);
        require(cv::norm(configured.image, original.image, cv::NORM_INF) > 0,
                "Non-default method settings were ignored");
        require(mae(configured.image, sharp) < source_error,
                "Non-default method failed to recover complementary focus");
        require(configured.focus_indices.type() == CV_32SC1 && configured.weights.size() == images.size(),
                "Method did not produce its source diagnostics");
        const auto saved = options;
        if (method == mif::FusionMethod::GuidedFilter) {
            options.laplacian_pyramid.focus = {static_cast<mif::FocusMeasure>(99), 0};
            options.laplacian_pyramid.detail_radius = -1;
            options.laplacian_pyramid.detail_epsilon = nan;
            options.laplacian_pyramid.levels = 0;
        } else {
            options.guided_filter.focus = {static_cast<mif::FocusMeasure>(99), 0};
            options.guided_filter.base_radius = 0;
            options.guided_filter.detail_radius = -1;
            options.guided_filter.base_epsilon = nan;
            options.guided_filter.detail_epsilon = 0;
        }
        equal(configured, mif::fuse(images, options));
        // 同一无效参数组被选中后必须立即拒绝，不能先归一化输入或触发进度回调。
        options.method = method == mif::FusionMethod::GuidedFilter ?
                         mif::FusionMethod::LaplacianPyramid : mif::FusionMethod::GuidedFilter;
        bool rejected = false, reported = false;
        try {
            mif::fuse(images, options, [&](int, const std::string&) { reported = true; return true; });
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected && !reported, "Selected invalid method settings were not rejected before processing");
        // 切换并执行其他方法后，原配置的计算结果应保持一致。
        options = saved;
        options.method = method == mif::FusionMethod::GuidedFilter ?
                         mif::FusionMethod::LaplacianPyramid : mif::FusionMethod::GuidedFilter;
        mif::fuse(images, options);
        options.method = method;
        equal(configured, mif::fuse(images, options));
    }
}

// 常量图像没有清晰度差异，应等权平均；保留的权重须非负且逐像素和为 1。
void weights() {
    cv::Mat a(39, 57, CV_16U, cv::Scalar(10000)), b(39, 57, CV_16U, cv::Scalar(50000));
    for (auto method : methods) {
        mif::FusionOptions o; o.keep_weight_maps = true; o.method = method;
        const auto result = mif::fuse({a, b}, o);
        require(result.weights.size() == 2, "Missing weight maps");
        require(cv::norm(result.weights[0] + result.weights[1], cv::Mat(a.size(), CV_32F, cv::Scalar(1)), cv::NORM_INF) < 1e-5,
                "Weights do not sum to one");
        require(cv::checkRange(result.weights[0], true, nullptr, 0, 1.0001), "Invalid weight range");
        require(mae(result.image, cv::Mat(a.size(), CV_16U, cv::Scalar(30000))) <= 1, "Flat image ties should average");
    }
}

// 通过公开接口验证两种方法的阶段回调：各处理阶段可取消，异常保持类型和消息，
// 完整执行时阶段名称正确、进度单调前进并最终到达 100。
void cancellation() {
    const auto images = focusStack(texture());
    // 自定义异常可区分“原样传播”与被包装成通用 runtime_error 的情况。
    struct CallbackFailure : std::runtime_error {
        using std::runtime_error::runtime_error;
    };
    for (const auto method : methods) {
        mif::FusionOptions options;
        options.method = method;
        const std::string blending_stage = method == mif::FusionMethod::GuidedFilter ? "blend" : "pyramid";
        const std::vector<std::string> cancellable_stages{"focus", "weights", blending_stage, "finish"};
        for (const auto& target_stage : cancellable_stages) {
            int last = -1;
            std::string last_stage;
            bool stopped = false;
            try {
                mif::fuse(images, options, [&](int percent, const std::string& stage) {
                    require(percent >= 0 && percent >= last && percent <= 100, "Progress decreased before cancellation");
                    last = percent;
                    last_stage = stage;
                    return stage != target_stage;
                });
            } catch (const mif::Cancelled&) {
                stopped = true;
            }
            require(stopped && last_stage == target_stage,
                    "Cancellation was ignored or requested stage was missing: " + target_stage);

            // 分别从公共阶段和方法内部抛出，确保拆分后的分派层没有吞掉或改写异常。
            bool propagated = false;
            try {
                mif::fuse(images, options, [&](int, const std::string& stage) {
                    if (stage == target_stage) throw CallbackFailure("Progress callback sentinel");
                    return true;
                });
            } catch (const CallbackFailure& error) {
                propagated = std::string(error.what()) == "Progress callback sentinel";
            }
            require(propagated, "Callback exception was changed or stage was missing: " + target_stage);
        }

        // 完整执行检查方法自己的融合阶段，能发现错误分派到另一种方法的回归。
        int last = -1;
        std::vector<std::string> observed_stages;
        mif::fuse(images, options, [&](int percent, const std::string& stage) {
            require(percent >= 0 && percent >= last && percent <= 100, "Progress is not monotonic");
            last = percent;
            if (observed_stages.empty() || observed_stages.back() != stage)
                observed_stages.push_back(stage);
            return true;
        });
        require(last == 100 && !observed_stages.empty() && observed_stages.back() == "done",
                "Missing completion progress or done stage");
        for (const auto& stage : cancellable_stages)
            require(std::find(observed_stages.begin(), observed_stages.end(), stage) != observed_stages.end(),
                    "Missing processing stage: " + stage);
        const std::string other_stage = method == mif::FusionMethod::GuidedFilter ? "pyramid" : "blend";
        require(std::find(observed_stages.begin(), observed_stages.end(), other_stage) == observed_stages.end(),
                "Fusion ran the wrong method's processing stage");
    }
}

// 第 257 张图拥有唯一纹理，获胜索引应为 256，确保索引没有被截断到 8 位。
void largeStack() {
    std::vector<cv::Mat> images(257, cv::Mat::zeros(13, 15, CV_8U));
    images.back() = texture(13, 15);
    mif::FusionOptions o; o.guided_filter.focus.window = 3; o.guided_filter.detail_radius = 1;
    const auto result = mif::fuse(images, o);
    double maximum;
    cv::minMaxLoc(result.focus_indices, nullptr, &maximum);
    require(result.focus_indices.type() == CV_32S && maximum == 256, "Source indices truncated to uint8");
}
}
int main(int argc, char** argv) {
    const std::map<std::string, std::function<void()>> tests{
        {"focus", testFocusMeasure}, {"quality", quality}, {"identity", identity},
        {"validation", validation}, {"method_options", methodOptions},
        {"weights", weights}, {"cancellation", cancellation},
        {"pipeline", testPipeline}, {"alignment", testRegistrationAlignment}, {"registration_homography", testRegistrationHomography},
        {"registration_ecc", testRegistrationEcc}, {"registration_failure", testRegistrationFailure},
        {"large_stack", largeStack}, {"dct", testDctFusion}, {"gfgfgf", testGfgfgfFusion},
        {"dtcwt_transform", testDtcwtTransform}, {"dtcwt_fusion", testDtcwtFusion}, {"dtcwt_options", testDtcwtOptions},
        {"guided_filter_numerics", testGuidedFilterNumerics}};
    try {
        if (argc != 2 || tests.count(argv[1]) == 0) throw std::runtime_error("Specify a test case");
        tests.at(argv[1])(); std::cout << "PASS " << argv[1] << '\n'; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

