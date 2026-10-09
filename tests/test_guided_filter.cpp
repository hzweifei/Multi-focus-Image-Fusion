#include "fixtures.hpp"
#include "fusion/common/parallel_frames.hpp"
#include <mif/fusion.hpp>
#include <atomic>
#include <functional>
#include <limits>
#include <thread>
#include <type_traits>
#include <utility>

namespace {

/// 统一把各方法所用的正则项设置为同一测试值，保持公开参数组独立。
template<class Options>
void setEpsilon(Options& options, double epsilon) {
    if constexpr (std::is_same_v<Options, mif::GuidedFilterFusionOptions>) {
        options.base_epsilon = epsilon;
        options.detail_epsilon = epsilon;
    } else if constexpr (std::is_same_v<Options, mif::LaplacianPyramidFusionOptions>) {
        options.detail_epsilon = epsilon;
    } else {
        options.guided_epsilon = epsilon;
    }
}

/// 只在测试调用之间设置线程数；生产算法不得改变 OpenCV 的全局线程设置。
struct RestoreThreadCount {
    int previous = cv::getNumThreads();
    ~RestoreThreadCount() { cv::setNumThreads(previous); }
};

struct FrameFailure : std::runtime_error {
    explicit FrameFailure(std::size_t failed_index)
        : std::runtime_error("Frame failure sentinel"), index(failed_index) {}
    std::size_t index;
};

/// 直接检查调度边界：工作异常按帧序传播，取消或回调抛错后没有未完成任务。
void checkFrameTaskCompletion() {
    using mif::detail::fusion::parallelFrames;
    const RestoreThreadCount restore;
    cv::setNumThreads(4);
    const auto caller = std::this_thread::get_id();
    std::atomic<int> active{0}, completed{0};
    const auto work = [&](std::size_t index) {
        ++active;
        struct Finish {
            std::atomic<int>& active;
            std::atomic<int>& completed;
            ~Finish() { ++completed; --active; }
        } finish{active, completed};
        if (index == 1 || index == 2) throw FrameFailure(index);
    };
    std::vector<std::size_t> reports;
    bool failed = false;
    try {
        parallelFrames(7, {513, 515}, [&](std::size_t index) {
            require(std::this_thread::get_id() == caller, "Frame progress ran on a worker thread");
            reports.push_back(index);
        }, work);
    } catch (const FrameFailure& error) {
        failed = error.index == 1 && std::this_thread::get_id() == caller;
    }
    require(failed && active == 0 && completed == static_cast<int>(reports.size()),
            "Frame failure was reordered or returned before the batch finished");

    for (bool throw_from_callback : {false, true}) {
        active = 0;
        completed = 0;
        reports.clear();
        bool stopped = false;
        try {
            parallelFrames(9, {513, 515}, [&](std::size_t index) {
                require(std::this_thread::get_id() == caller && active == 0,
                        "Callback overlapped unfinished frame tasks");
                reports.push_back(index);
                if (index == 4) {
                    if (throw_from_callback) throw FrameFailure(99);
                    throw mif::Cancelled();
                }
            }, [&](std::size_t) {
                ++active;
                ++completed;
                --active;
            });
        } catch (const mif::Cancelled&) {
            stopped = !throw_from_callback;
        } catch (const FrameFailure& error) {
            stopped = throw_from_callback && error.index == 99;
        }
        require(stopped && active == 0 && completed <= 4 &&
                reports == std::vector<std::size_t>({0, 1, 2, 3, 4}),
                "Callback cancellation or failure left tasks or reported extra frames");
    }

    // 小图保持逐帧交替的报告/计算顺序，即使 OpenCV 允许使用多线程。
    std::vector<int> small_events;
    parallelFrames(3, {17, 19}, [&](std::size_t index) {
        small_events.push_back(static_cast<int>(2 * index));
    }, [&](std::size_t index) {
        require(std::this_thread::get_id() == caller, "Small frames unexpectedly used worker threads");
        small_events.push_back(static_cast<int>(2 * index + 1));
    });
    require(small_events == std::vector<int>({0, 1, 2, 3, 4, 5}), "Small frame scheduling changed");
}

/// 五张奇数尺寸、非连续的浮点图触发帧并行，也覆盖批次最后只剩一帧的情况。
/// 对照单线程结果，检查回调有无、诊断图、原始事件序列及输入所有权。
void checkParallelFusion() {
    const RestoreThreadCount restore;
    cv::setNumThreads(1);
    const cv::Mat sharp = texture(517, 519);
    cv::Mat soft;
    cv::GaussianBlur(sharp, soft, {0, 0}, 2.0);
    std::vector<cv::Mat> images, originals;
    for (int i = 0; i < 5; ++i) {
        cv::Mat storage = soft.clone();
        const cv::Rect stripe(i * 103, 0, 103, sharp.rows);
        sharp(stripe).copyTo(storage(stripe));
        storage.convertTo(storage, CV_32F, 1.0 / 255.0);
        images.push_back(storage(cv::Rect(1, 1, 513, 515)));
        originals.push_back(images.back().clone());
    }
    const auto caller = std::this_thread::get_id();
    using Event = std::pair<int, std::string>;
    auto check = [&](auto options) {
        options.include_weight_maps = true;
        using Options = decltype(options);
        constexpr bool gfg = std::is_same_v<Options, mif::GfgFgfFusionOptions>;
        constexpr bool pyramid = std::is_same_v<Options, mif::LaplacianPyramidFusionOptions>;
        std::vector<Event> expected{{0, "prepare"}};
        for (int i = 0; i < 5; ++i) expected.emplace_back(2 * i, "prepare");
        expected.emplace_back(10, "prepare");
        for (int i = 0; i < 5; ++i) expected.emplace_back(30 + (gfg ? 3 : 4) * i, "focus");
        if constexpr (gfg) {
            for (int i = 0; i < 5; ++i) expected.emplace_back(45 + 3 * i, "focus");
            expected.emplace_back(60, "weights");
        }
        for (int i = 0; i < 5; ++i)
            expected.emplace_back((gfg ? 60 : 50) + (gfg ? 3 : 4) * i, "weights");
        for (int i = 0; i < 5; ++i)
            expected.emplace_back((gfg ? 75 : 70) + (gfg ? 4 : 5) * i, pyramid ? "pyramid" : "blend");
        expected.emplace_back(97, "finish");
        expected.emplace_back(100, "done");

        const auto run = [&](int threads, std::vector<Event>& events) {
            cv::setNumThreads(threads);
            return mif::fuse(images, options, [&](int percent, const std::string& stage) {
                require(std::this_thread::get_id() == caller, "Fusion callback ran on a worker thread");
                events.emplace_back(percent, stage);
                return true;
            });
        };
        std::vector<Event> serial_events, parallel_events;
        const auto serial = run(1, serial_events);
        const auto parallel = run(4, parallel_events);
        const auto without_callback = mif::fuse(images, options);
        require(serial_events == expected && parallel_events == expected,
                "Frame parallelism changed the progress event sequence");
        const auto same_result = [&](const mif::FusionResult& result) {
            require(cv::norm(serial.image, result.image, cv::NORM_INF) == 0 &&
                    cv::norm(serial.source_index_map, result.source_index_map, cv::NORM_INF) == 0 &&
                    serial.weight_maps.size() == result.weight_maps.size(),
                    "Frame parallelism changed the fused image or source indices");
            for (std::size_t i = 0; i < serial.weight_maps.size(); ++i)
                require(cv::norm(serial.weight_maps[i], result.weight_maps[i], cv::NORM_INF) == 0,
                        "Frame parallelism changed diagnostic weights");
        };
        same_result(parallel);
        same_result(without_callback);

        for (bool throw_from_callback : {false, true}) {
            std::vector<Event> events;
            bool stopped = false;
            int weights_seen = 0;
            try {
                mif::fuse(images, options, [&](int percent, const std::string& stage) {
                    require(std::this_thread::get_id() == caller, "Cancellation callback ran on a worker thread");
                    events.emplace_back(percent, stage);
                    if (stage == "weights" && ++weights_seen == 2) {
                        if (throw_from_callback) throw FrameFailure(99);
                        return false;
                    }
                    return true;
                });
            } catch (const mif::Cancelled&) {
                stopped = !throw_from_callback;
            } catch (const FrameFailure& error) {
                stopped = throw_from_callback && error.index == 99;
            }
            require(stopped && events.size() <= expected.size() &&
                    std::equal(events.begin(), events.end(), expected.begin()),
                    "Parallel fusion changed callback cancellation or exception propagation");
        }
        for (std::size_t i = 0; i < images.size(); ++i)
            require(cv::norm(images[i], originals[i], cv::NORM_INF) == 0,
                    "Parallel fusion modified an input ROI");
    };
    check(mif::GuidedFilterFusionOptions{});
    check(mif::LaplacianPyramidFusionOptions{});
    check(mif::GfgFgfFusionOptions{});
    // 非零筛选比例使并行评分实际执行 Scharr；本组纹理帧均超过这一低阈值。
    mif::GfgFgfFusionOptions with_screening;
    with_screening.selection_ratio = 1e-6;
    check(with_screening);
}

} // 匿名命名空间

/// OpenCV float32 协方差计算会舍去过小的正则项；平坦引导图曾出现除零和 NaN。
/// 在公开入口验证参数保护与最小合法值，而不是重新实现公式来测试公式本身。
void testGuidedFilterNumerics() {
    checkFrameTaskCompletion();
    checkParallelFusion();
    const cv::Mat constant(25, 37, CV_32F, cv::Scalar(0.5));
    auto check = [&](auto options) {
        options.include_weight_maps = true;
        for (double epsilon : {1e-100, 1e-8, 0.0, -1.0, std::numeric_limits<double>::max(),
                                std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            setEpsilon(options, epsilon);
            bool rejected = false, reported = false;
            try {
                mif::fuse({constant, constant}, options, [&](int, const std::string&) {
                    reported = true;
                    return true;
                });
            } catch (const std::invalid_argument&) { rejected = true; }
            require(rejected && !reported, "Unsafe guided epsilon was not rejected before processing");
        }
        for (int radius : {1, 3, 15, 255}) {
            setEpsilon(options, 1e-6);
            using Options = decltype(options);
            if constexpr (std::is_same_v<Options, mif::GuidedFilterFusionOptions>) {
                options.base_radius = radius;
                options.detail_radius = radius;
            } else if constexpr (std::is_same_v<Options, mif::LaplacianPyramidFusionOptions>) {
                options.detail_radius = radius;
            } else {
                options.guided_radius = radius;
                options.selection_ratio = 0;
            }
            const auto averaged = mif::fuse({constant * 0.4f, constant, constant * 1.6f}, options);
            require(cv::checkRange(averaged.image), "Minimum legal epsilon produced nonfinite output");
            require(cv::norm(averaged.image, constant, cv::NORM_INF) < 2e-5,
                    "Minimum legal epsilon changed the mean of textureless inputs");
            cv::Mat near_constant = constant.clone();
            near_constant(cv::Rect(0, 0, 18, 25)).setTo(0.50001f);
            cv::Mat local_flat = constant.clone();
            local_flat(cv::Rect(0, 0, 18, 25)).setTo(0.125f);
            local_flat(cv::Rect(18, 0, 19, 25)).setTo(0.875f);
            for (const auto& image : {near_constant, local_flat}) {
                const auto result = mif::fuse({image, image}, options);
                require(cv::checkRange(result.image) && cv::norm(result.image, image, cv::NORM_INF) < 2e-5,
                        "Guided filter corrupted repeated near-constant or locally flat inputs");
                cv::Mat total = cv::Mat::zeros(image.size(), CV_32F);
                for (const auto& weight : result.weight_maps) {
                    require(cv::checkRange(weight, true, nullptr, 0, 1.00001),
                            "Guided filter returned invalid diagnostic weights");
                    total += weight;
                }
                require(cv::norm(total, cv::Mat(image.size(), CV_32F, cv::Scalar(1)), cv::NORM_INF) < 1e-5,
                        "Guided filter weights lost normalization at the epsilon boundary");
            }
        }
        setEpsilon(options, std::numeric_limits<float>::max());
        require(cv::checkRange(mif::fuse({constant, constant}, options).image),
                "Largest representable guided epsilon failed");
    };
    check(mif::GuidedFilterFusionOptions{});
    check(mif::LaplacianPyramidFusionOptions{});
    check(mif::GfgFgfFusionOptions{});
    // GFF 两个正则项独立校验；不能只检查细节参数而漏掉基础参数。
    mif::GuidedFilterFusionOptions base_only;
    base_only.base_epsilon = 1e-8;
    bool rejected = false;
    try { mif::fuse({constant, constant}, base_only); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Unsafe base-layer epsilon was accepted");
}
