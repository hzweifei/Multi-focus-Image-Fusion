#include "fusion/dtcwt/dtcwt.hpp"
#include "fusion/dtcwt/transform.hpp"
#include "common/progress.hpp"

#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <utility>

namespace mif::detail::fusion {
namespace {

/// 从复系数模长取得局部最大活动度；端点重复的对称边界与变换采用同一约定。
cv::Mat activity(const cv::Mat& coefficients, const cv::Mat& window) {
    cv::Mat parts[2], magnitude, maximum;
    cv::split(coefficients, parts);
    cv::magnitude(parts[0], parts[1], magnitude);
    cv::dilate(magnitude, maximum, window, {-1, -1}, 1, cv::BORDER_REFLECT);
    return maximum;
}

/// 逐方向比较两组复系数，经邻域多数表决后选择完整的复数，保留相位。
/// 活动度相等时选择后输入；窗口外投票为零，与 OpenFocus 的一致性规则相同。
void mergeBand(cv::Mat& accumulated, const cv::Mat& incoming, const cv::Mat& window) {
    cv::Mat mask, votes;
    cv::compare(activity(accumulated, window), activity(incoming, window), mask, cv::CMP_GT);
    cv::boxFilter(mask, votes, CV_32F, window.size(), {-1, -1}, false, cv::BORDER_CONSTANT);
    cv::compare(votes, 255.0 * window.total() / 2.0, mask, cv::CMP_LE);
    incoming.copyTo(accumulated, mask);
}

} // 匿名命名空间

void validateDtcwtOptions(const DtcwtOptions& options) {
    if (options.levels < 1 || options.levels > 16)
        throw std::invalid_argument("DTCWT levels must be in [1, 16]");
    if (options.activity_window < 1 || options.activity_window > 31 || options.activity_window % 2 == 0)
        throw std::invalid_argument("DTCWT activity_window must be odd and in [1, 31]");
}

MethodResult dtcwtFusion(const std::vector<cv::Mat>& images, const DtcwtOptions& options,
                        const ProgressCallback& progress) {
    const int channels = images.front().channels();
    const int levels = dtcwt::effectiveLevels(images.front().size(), options.levels);
    // 每通道：每帧正变换、后续帧逐层合并、最后逐层逆变换。计数不依赖图像内容。
    const std::size_t total_steps = static_cast<std::size_t>(channels) * levels * images.size() * 2;
    std::size_t completed = 0;
    const auto checkpoint = [&] {
        detail::report(progress, 10 + static_cast<int>(85 * completed / total_steps), "dtcwt");
        ++completed;
    };
    const cv::Mat window = cv::Mat::ones(options.activity_window, options.activity_window, CV_8U);
    std::vector<cv::Mat> fused_channels;
    fused_channels.reserve(static_cast<std::size_t>(channels));
    for (int channel = 0; channel < channels; ++channel) {
        dtcwt::Pyramid fused;
        for (std::size_t frame = 0; frame < images.size(); ++frame) {
            cv::Mat plane;
            cv::extractChannel(images[frame], plane, channel);
            auto incoming = dtcwt::forward(plane, levels, checkpoint);
            if (frame == 0) {
                fused = std::move(incoming);
                continue;
            }
            fused.lowpass += incoming.lowpass;
            for (int level = 0; level < levels; ++level) {
                checkpoint();
                for (int direction = 0; direction < 6; ++direction)
                    mergeBand(fused.highpass[level][direction], incoming.highpass[level][direction], window);
            }
            // 当前帧用完即释放，只保留累计结果和下一帧，内存不随图像张数线性增长。
        }
        fused.lowpass /= static_cast<double>(images.size());
        cv::Mat restored;
        dtcwt::inverse(fused, checkpoint).convertTo(restored, CV_32F);
        fused_channels.push_back(std::move(restored));
    }
    MethodResult result;
    cv::merge(fused_channels, result.image);
    detail::report(progress, 95, "dtcwt");
    return result;
}

} // 命名空间 mif::detail::fusion
