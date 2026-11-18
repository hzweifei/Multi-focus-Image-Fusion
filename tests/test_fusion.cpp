#include "fixtures.hpp"
#include <mif/mif.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <type_traits>
#include <future>
#include "fusion/registry.hpp"

// 核心算法回归测试入口；CTest 通过用例名分别运行，失败原因可独立定位。
void testFocusMeasure();
void testRegistrationAlignment();
void testPipeline();
void testRegistrationHomography();
void testRegistrationEcc();
void testRegistrationFailure();
void testRegistrationRegistry();
void testDctFusion();
void testGfgfgfFusion();
void testDtcwtTransform();
void testDtcwtFusion();
void testDtcwtOptions();
void testGuidedFilterNumerics();
void testGfgfgfPriority();
void testFastGuidedFilter();
namespace {
template<class Action>
void forEachFocusOptions(Action action) {
    action(mif::GuidedFilterFusionOptions{});
    action(mif::LaplacianPyramidFusionOptions{});
}

// 互补清晰区域应被恢复：融合误差明显低于任一源图，并保持尺寸和顺序对称性。
void quality() {
    for (int channels : {1, 3}) {
        auto sharp = texture();
        if (channels == 3) cv::cvtColor(sharp, sharp, cv::COLOR_GRAY2BGR);
        const auto stack = focusStack(sharp);
        forEachFocusOptions([&](auto o) {
            for (auto measure : {mif::FocusMeasure::ModifiedLaplacian, mif::FocusMeasure::Tenengrad,
                                 mif::FocusMeasure::ModifiedTenengrad}) {
                o.focus.measure = measure;
                const auto output = mif::fuse(stack, o);
                const double baseline = std::min(mae(stack[0], sharp), mae(stack[1], sharp));
                const double error = mae(output.image, sharp);
                std::cout << "MAE fused=" << error << " source=" << baseline << '\n';
                require(error < baseline * 0.65, "Fusion should recover complementary sharp regions");
                require(output.image.size() == sharp.size(), "Odd image dimensions changed");
                require(output.weight_maps.empty(), "Weights retained without request");
                auto reversed = stack; std::reverse(reversed.begin(), reversed.end());
                require(mae(output.image, mif::fuse(reversed, o).image) < 0.01, "Fusion changed when source order changed");
            }
        });
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
            forEachFocusOptions([&](auto o) {
                if constexpr (std::is_same_v<decltype(o), mif::LaplacianPyramidFusionOptions>) o.max_levels = 16;
                const auto result = mif::fuse({image, image, image}, o);
                require(result.image.type() == image.type(), "Depth/channels were not preserved");
                require(cv::norm(result.image, image, cv::NORM_INF) <= (depth == CV_32F ? 2e-6 : 1), "Identical inputs changed");
                require(cv::norm(image, before, cv::NORM_INF) == 0, "Input was modified");
                cv::Mat tiny(2, 3, image.type(), cv::Scalar::all(depth == CV_32F ? 0.4 : 100));
                require(mae(mif::fuse({tiny, tiny}, o).image, tiny) < 0.01, "Tiny image failed");
            });
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
    forEachFocusOptions([&](auto defaults) {
        auto reject_option = [&](auto change) {
            auto options = defaults;
            change(options);
            bool reported = false;
            rejects([&] { mif::fuse({image, image}, options, [&](int, const std::string&) {
                reported = true; return true;
            }); });
            require(!reported, "Invalid options reached image processing");
        };
        reject_option([](auto& o) { o.focus.measure = static_cast<mif::FocusMeasure>(99); });
        for (int value : {0, 2, 256}) reject_option([&](auto& o) { o.focus.window_size = value; });
        for (int value : {0, 256}) {
            reject_option([&](auto& o) { o.detail_radius = value; });
            if constexpr (std::is_same_v<decltype(defaults), mif::GuidedFilterFusionOptions>)
                reject_option([&](auto& o) { o.base_radius = value; });
        }
        for (double value : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity()}) {
            reject_option([&](auto& o) { o.detail_epsilon = value; });
            if constexpr (std::is_same_v<decltype(defaults), mif::GuidedFilterFusionOptions>)
                reject_option([&](auto& o) { o.base_epsilon = value; });
        }
        if constexpr (std::is_same_v<decltype(defaults), mif::LaplacianPyramidFusionOptions>)
            for (int value : {0, 17}) reject_option([&](auto& o) { o.max_levels = value; });
    });
}

// 参数类型直接选择算法；复制与多态快照必须保留配置，且互不共享可变字段。
void methodOptions() {
    const mif::GuidedFilterFusionOptions guided_defaults;
    const mif::LaplacianPyramidFusionOptions pyramid_defaults;
    require(!guided_defaults.include_weight_maps && guided_defaults.base_radius == 15 &&
            guided_defaults.base_epsilon == 0.01 && guided_defaults.detail_radius == 3 &&
            guided_defaults.detail_epsilon == 0.0001, "Guided defaults changed");
    require(pyramid_defaults.max_levels == 5 && pyramid_defaults.detail_radius == 3 &&
            pyramid_defaults.detail_epsilon == 0.0001, "Pyramid defaults changed");
    const auto sharp = texture();
    const auto images = focusStack(sharp);
    require(cv::norm(mif::fuse(images).image, mif::fuse(images, guided_defaults).image, cv::NORM_INF) == 0,
            "Default entry did not select guided filter");
    forEachFocusOptions([&](auto options) {
        using Options = decltype(options);
        require(options.focus.window_size == 7 && options.focus.measure == mif::FocusMeasure::ModifiedTenengrad,
                "Focus defaults changed");
        const auto original = mif::fuse(images, options);
        options.include_weight_maps = true;
        options.focus = {mif::FocusMeasure::Tenengrad, 5};
        options.detail_radius = 2;
        options.detail_epsilon = 0.002;
        if constexpr (std::is_same_v<Options, mif::GuidedFilterFusionOptions>) {
            options.base_radius = 7;
            options.base_epsilon = 0.02;
        } else {
            options.max_levels = 3;
        }
        const auto configured = mif::fuse(images, options);
        require(cv::norm(configured.image, original.image, cv::NORM_INF) > 0, "Method options ignored");
        require(mae(configured.image, sharp) < std::min(mae(images[0], sharp), mae(images[1], sharp)),
                "Configured method lost complementary focus");
        require(configured.source_index_map.type() == CV_32SC1 && configured.weight_maps.size() == images.size(),
                "Source diagnostics missing");
        auto snapshot = options.clone();
        require(dynamic_cast<Options*>(snapshot.get()) != nullptr, "Parameter snapshot sliced its dynamic type");
        options.focus.window_size = 0;
        const auto copied = mif::fuse(images, *snapshot);
        require(cv::norm(configured.image, copied.image, cv::NORM_INF) == 0 &&
                cv::norm(configured.source_index_map, copied.source_index_map, cv::NORM_INF) == 0,
                "Snapshot changed after editing source configuration");
        for (size_t i = 0; i < copied.weight_maps.size(); ++i)
            require(cv::norm(configured.weight_maps[i], copied.weight_maps[i], cv::NORM_INF) == 0,
                    "Snapshot lost diagnostic settings");
    });
}

// 用公开的两组权重和 double 逐像素重建验证 GFF 的分层公式。
// 不复写评分、决策或引导滤波；能发现复用缓冲时覆盖基础层、用错权重或通道的问题。
void checkGuidedLayerReconstruction() {
    for (int depth : {CV_8U, CV_16U, CV_32F}) {
        for (int channels : {1, 3}) {
            cv::Mat sharp = texture(49, 67);
            if (channels == 3) {
                cv::Mat flipped, inverted;
                cv::flip(sharp, flipped, 1);
                cv::subtract(cv::Scalar::all(255), sharp, inverted);
                cv::merge(std::vector<cv::Mat>{sharp, flipped, inverted}, sharp);
            }
            const double range = depth == CV_8U ? 255.0 : depth == CV_16U ? 65535.0 : 1.0;
            std::vector<cv::Mat> images, originals, normalized, bases;
            const mif::GuidedFilterFusionOptions defaults;
            for (int i = 0; i < 5; ++i) {
                cv::Mat storage;
                cv::GaussianBlur(sharp, storage, {0, 0}, 1.4 + 0.3 * i);
                const int left = i * sharp.cols / 5, right = (i + 1) * sharp.cols / 5;
                const cv::Rect stripe(left, 0, right - left, sharp.rows);
                sharp(stripe).copyTo(storage(stripe));
                storage.convertTo(storage, depth, range / 255.0);
                images.push_back(storage(cv::Rect(2, 2, 63, 45)));
                require(!images.back().isContinuous(), "Reconstruction fixture must use strided inputs");
                originals.push_back(images.back().clone());
                cv::Mat frame, base;
                images.back().convertTo(frame, CV_32F, 1.0 / range);
                cv::blur(frame, base, {2 * defaults.base_radius + 1, 2 * defaults.base_radius + 1});
                normalized.push_back(frame);
                bases.push_back(base);
            }
            auto options = defaults;
            options.include_weight_maps = true;
            const auto actual = mif::fuse(images, options);
            // 公开诊断返回细节权重；将细节参数设置为基础参数，即可取得原调用的基础权重。
            auto base_options = options;
            base_options.detail_radius = options.base_radius;
            base_options.detail_epsilon = options.base_epsilon;
            const auto base_result = mif::fuse(images, base_options);
            require(actual.weight_maps.size() == images.size() && base_result.weight_maps.size() == images.size(),
                    "Reconstruction diagnostics are missing");
            require(actual.source_index_map.type() == CV_32SC1 && actual.source_index_map.size() == images.front().size(),
                    "GFF source index map has an invalid shape or type");
            double weight_difference = 0;
            for (std::size_t i = 0; i < images.size(); ++i)
                weight_difference = std::max(weight_difference,
                    cv::norm(actual.weight_maps[i], base_result.weight_maps[i], cv::NORM_INF));
            require(weight_difference > 1e-4, "Reconstruction fixture must distinguish base/detail weights");

            cv::Mat expected(images.front().size(), CV_MAKETYPE(CV_64F, channels));
            for (int y = 0; y < expected.rows; ++y) {
                for (int x = 0; x < expected.cols; ++x) {
                    int owner = 0;
                    for (std::size_t i = 1; i < images.size(); ++i)
                        if (actual.weight_maps[i].at<float>(y, x) > actual.weight_maps[owner].at<float>(y, x))
                            owner = static_cast<int>(i);
                    require(actual.source_index_map.at<int>(y, x) == owner,
                            "GFF source indices do not describe the dominant detail weights");
                    for (int c = 0; c < channels; ++c) {
                        double value = 0;
                        for (std::size_t i = 0; i < images.size(); ++i) {
                            const double source = normalized[i].ptr<float>(y)[x * channels + c];
                            const double base = bases[i].ptr<float>(y)[x * channels + c];
                            value += base * base_result.weight_maps[i].at<float>(y, x)
                                + (source - base) * actual.weight_maps[i].at<float>(y, x);
                        }
                        expected.ptr<double>(y)[x * channels + c] = std::clamp(value, 0.0, 1.0);
                    }
                }
            }
            expected.convertTo(expected, images.front().type(), range);
            require(cv::norm(actual.image, expected, cv::NORM_INF) <= (depth == CV_32F ? 2e-6 : 1),
                    "GFF output disagrees with independent two-layer reconstruction");
            for (std::size_t i = 0; i < images.size(); ++i)
                require(cv::norm(images[i], originals[i], cv::NORM_INF) == 0,
                        "GFF buffer reuse modified a strided input");
        }
    }
}

// 常量输入等权平均，公开诊断权重逐像素归一化。
void weights() {
    cv::Mat a(39, 57, CV_16U, cv::Scalar(10000)), b(39, 57, CV_16U, cv::Scalar(50000));
    forEachFocusOptions([&](auto o) {
        o.include_weight_maps = true;
        const auto result = mif::fuse({a, b}, o);
        require(result.weight_maps.size() == 2, "Missing weight maps");
        require(cv::norm(result.weight_maps[0] + result.weight_maps[1], cv::Mat(a.size(), CV_32F, cv::Scalar(1)), cv::NORM_INF) < 1e-5,
                "Weights do not sum to one");
        require(cv::checkRange(result.weight_maps[0], true, nullptr, 0, 1.0001), "Invalid weight range");
        require(mae(result.image, cv::Mat(a.size(), CV_16U, cv::Scalar(30000))) <= 1, "Flat image ties should average");
    });
    checkGuidedLayerReconstruction();
}

// 通过公开接口验证两种方法的阶段回调：各处理阶段可取消，异常保持类型和消息，
// 完整执行时阶段名称正确、进度单调前进并最终到达 100。
void cancellation() {
    const auto images = focusStack(texture());
    // 自定义异常可区分“原样传播”与被包装成通用 runtime_error 的情况。
    struct CallbackFailure : std::runtime_error {
        using std::runtime_error::runtime_error;
    };
    forEachFocusOptions([&](auto options) {
        const std::string blending_stage = std::is_same_v<decltype(options), mif::GuidedFilterFusionOptions> ? "blend" : "pyramid";
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
        const std::string other_stage = std::is_same_v<decltype(options), mif::GuidedFilterFusionOptions> ? "pyramid" : "blend";
        require(std::find(observed_stages.begin(), observed_stages.end(), other_stage) == observed_stages.end(),
                "Fusion ran the wrong method's processing stage");
    });
}

// 第 257 张图拥有唯一纹理，获胜索引应为 256，确保索引没有被截断到 8 位。
void largeStack() {
    std::vector<cv::Mat> images(257, cv::Mat::zeros(13, 15, CV_8U));
    images.back() = texture(13, 15);
    mif::GuidedFilterFusionOptions o; o.focus.window_size = 3; o.detail_radius = 1;
    const auto result = mif::fuse(images, o);
    double maximum;
    cv::minMaxLoc(result.source_index_map, nullptr, &maximum);
    require(result.source_index_map.type() == CV_32S && maximum == 256, "Source indices truncated to uint8");
}
// 独立进程中同时首次调用两条入口，验证内置注册只执行一次且不持锁运行算法。
void registryConcurrency() {
    std::promise<void> ready;
    const auto start = ready.get_future().share();
    std::vector<std::future<void>> tasks;
    for (int i = 0; i < 8; ++i) {
        tasks.push_back(std::async(std::launch::async, [start] {
            start.wait();
            const cv::Mat image(8, 9, CV_16U, cv::Scalar(12345));
            const auto registered = mif::registerImages({image, image});
            const auto fused = mif::fuse(registered.images);
            require(cv::norm(fused.image, image, cv::NORM_INF) <= 1,
                    "Concurrent first invocation lost method registration or data");
        }));
    }
    ready.set_value();
    for (auto& task : tasks) task.get();
}

struct TestFusionOptions final : mif::FusionOptionsBase {
    float gain = 1;
    std::unique_ptr<mif::FusionOptionsBase> clone() const override {
        return std::make_unique<TestFusionOptions>(*this);
    }
};
void validateTestOptions(const TestFusionOptions& options) {
    if (options.gain <= 0) throw std::invalid_argument("Test gain must be positive");
}
mif::detail::fusion::MethodResult runTestMethod(const std::vector<cv::Mat>& images,
    const TestFusionOptions& options, const mif::ProgressCallback&) {
    mif::detail::fusion::MethodResult result;
    result.image = images.front() * options.gain;
    return result;
}
void registry() {
    const cv::Mat image(8, 9, CV_16UC3, cv::Scalar(10000, 20000, 40000));
    TestFusionOptions options;
    bool rejected = false;
    try { mif::fuse({image, image}, options); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Unknown fusion configuration accepted");
    mif::detail::fusion::registerFusionMethod(validateTestOptions, runTestMethod);
    options.gain = 2;
    const auto result = mif::fuse({image, image}, options);
    require(result.image.type() == image.type() && result.image.at<cv::Vec<unsigned short, 3>>(0, 0)[2] == 65535 &&
            result.image.at<cv::Vec<unsigned short, 3>>(0, 0)[0] == 20000,
            "Registered method bypassed normalization, clamping or depth restoration");
    rejected = false;
    try { mif::detail::fusion::registerFusionMethod(validateTestOptions, runTestMethod); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Duplicate fusion registration accepted");
    options.gain = 0;
    rejected = false;
    bool reported = false;
    try { mif::fuse({image, image}, options, [&](int, const std::string&) { reported = true; return true; }); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && !reported, "Custom validation did not run before processing");
}

}
int main(int argc, char** argv) {
    const std::map<std::string, std::function<void()>> tests{
        {"registry", registry}, {"registry_concurrency", registryConcurrency}, {"registration_registry", testRegistrationRegistry}, {"focus", testFocusMeasure}, {"quality", quality}, {"identity", identity},
        {"validation", validation}, {"method_options", methodOptions},
        {"weights", weights}, {"cancellation", cancellation},
        {"pipeline", testPipeline}, {"alignment", testRegistrationAlignment}, {"registration_homography", testRegistrationHomography},
        {"registration_ecc", testRegistrationEcc}, {"registration_failure", testRegistrationFailure},
        {"large_stack", largeStack}, {"dct", testDctFusion}, {"gfgfgf", testGfgfgfFusion},
        {"dtcwt_transform", testDtcwtTransform}, {"dtcwt_fusion", testDtcwtFusion}, {"dtcwt_options", testDtcwtOptions},
        {"guided_filter_numerics", testGuidedFilterNumerics},
        {"gfgfgf_priority", testGfgfgfPriority}, {"fast_guided_filter", testFastGuidedFilter}};
    try {
        if (argc != 2 || tests.count(argv[1]) == 0) throw std::runtime_error("Specify a test case");
        tests.at(argv[1])(); std::cout << "PASS " << argv[1] << '\n'; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

