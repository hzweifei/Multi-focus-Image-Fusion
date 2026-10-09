#include "fusion/dtcwt/transform.hpp"

#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace mif::detail::fusion::dtcwt {
namespace {

// 滤波器组采用 Kingsbury 的 near_sym_a（首层 5/7 抽头）和 qshift_a（后续 10 抽头）。
// 数学背景：N. Kingsbury, ACHA 10(3), 2001, DOI: 10.1006/acha.2000.0343。
// 以下仅记录滤波器数值数据；数据来源、采样公式和独立实现约定见同目录 FILTERS.md。
// Q-shift 的另一棵树由时间反转得到，高通由正交镜像关系得到，无需复制八份常数。
constexpr std::array<double, 5> first_low = {-0.05, 0.25, 0.6, 0.25, -0.05};
constexpr std::array<double, 7> first_high = {
    3.0 / 280.0, -15.0 / 280.0, -73.0 / 280.0, 170.0 / 280.0,
    -73.0 / 280.0, -15.0 / 280.0, 3.0 / 280.0};
constexpr std::array<double, 7> first_low_synthesis = {
    -3.0 / 280.0, -15.0 / 280.0, 73.0 / 280.0, 170.0 / 280.0,
    73.0 / 280.0, -15.0 / 280.0, -3.0 / 280.0};
constexpr std::array<double, 5> first_high_synthesis = {-0.05, -0.25, 0.6, -0.25, -0.05};
constexpr std::array<double, 10> qshift_low_a = {
    0.051130405283831656, -0.013975370246888838, -0.10983605166597087,
    0.26383956105893763, 0.7666284677930372, 0.5636557101270515,
    0.0008736226952170968, -0.1002312195074762, -0.0016896812725281543,
    -0.006181881892116438};
constexpr double inverse_sqrt_two = 0.70710678118654752440;

/// 只把互不写入同一像素的范围交给 OpenCV 线程池；小图和粗尺度直接顺序执行。
/// 每次调用返回后所有范围均已完成，因此层间 checkpoint 仍由原调用线程执行。
template<class Operation>
void independentRanges(int extent, std::size_t pixels, const Operation& operation) {
    constexpr std::size_t minimum_parallel_pixels = 256 * 256;
    const cv::Range range(0, extent);
    if (extent < 2 || pixels < minimum_parallel_pixels || cv::getNumThreads() == 1)
        operation(range);
    else
        cv::parallel_for_(range, operation);
}

/// 关于像素边缘的对称延拓，端点重复；取模也支持小于滤波器长度的极小图像。
int reflected(int index, int length) {
    const int period = 2 * length;
    index %= period;
    if (index < 0) index += period;
    return index < length ? index : period - 1 - index;
}

/// 首层使用未降采样的奇长度对称滤波；偶、奇位置分别携带两个相位树。
template<std::size_t Length>
cv::Mat symmetricColumns(const cv::Mat& input, const std::array<double, Length>& filter) {
    cv::Mat output = cv::Mat::zeros(input.size(), CV_64F);
    const int radius = static_cast<int>(Length / 2);
    independentRanges(input.rows, output.total(), [&](const cv::Range& rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            auto* dst = output.ptr<double>(y);
            // 每行独立；仍按原抽头顺序用 double 累加，线程数不改变舍入次序。
            for (int tap = 0; tap < static_cast<int>(Length); ++tap) {
                const auto* src = input.ptr<double>(reflected(y + radius - tap, input.rows));
                const double coefficient = filter[static_cast<std::size_t>(tap)];
                for (int x = 0; x < input.cols; ++x) dst[x] += coefficient * src[x];
            }
        }
    });
    return output;
}

/// 两棵相位树的分析滤波器；高通使用正交镜像关系，并交换输出相位以保持方向配对。
double treeCoefficient(bool highpass, int tree, int tap) {
    if (!highpass) return qshift_low_a[tree == 0 ? 9 - tap : tap];
    const double value = qshift_low_a[tree == 0 ? tap : 9 - tap];
    return ((tap + tree) % 2 == 0) ? -value : value;
}

/// Q-shift 分析的直接采样公式：
/// y[2j+t'] = Σ_k h_t[k] x_reflect[4j+10+t-2k]。
/// 低通 t'=t，高通 t'=1-t；输入交错树中每棵树各降采样二倍。
/// 四的倍数长度与端点对称边界使低、高通合起来成为正交分析矩阵 A。
cv::Mat analyzeColumns(const cv::Mat& input, bool highpass) {
    CV_Assert(input.rows % 4 == 0 && input.type() == CV_64FC1);
    cv::Mat output = cv::Mat::zeros(input.rows / 2, input.cols, CV_64F);
    // 每组只写自己的两个输出行，两个相位树及各抽头的内部顺序保持不变。
    independentRanges(input.rows / 4, output.total(), [&](const cv::Range& groups) {
        for (int group = groups.start; group < groups.end; ++group) {
            for (int tree = 0; tree < 2; ++tree) {
                auto* dst = output.ptr<double>(2 * group + (highpass ? 1 - tree : tree));
                for (int tap = 0; tap < 10; ++tap) {
                    const int row = reflected(4 * group + 10 + tree - 2 * tap, input.rows);
                    const auto* src = input.ptr<double>(row);
                    const double coefficient = treeCoefficient(highpass, tree, tap);
                    for (int x = 0; x < input.cols; ++x) dst[x] += coefficient * src[x];
                }
            }
        }
    });
    return output;
}

/// 直接应用分析矩阵的转置 Aᵀ：把每个子带采样按相同系数加回原位置。
/// 延拓索引重复时累加到同一位置，这一步同时实现合成滤波、插零和正确的边界重建。
/// qshift_a 满足 AᵀA=I，因此无需单独实现另一套边界插值规则。
cv::Mat synthesizeColumns(const cv::Mat& lowpass, const cv::Mat& highpass) {
    CV_Assert(lowpass.size() == highpass.size() && lowpass.rows % 2 == 0);
    cv::Mat output = cv::Mat::zeros(2 * lowpass.rows, lowpass.cols, CV_64F);
    // 不同组的反射索引会写到同一行，不能按组并行。改按列条带分配输出像素，
    // 每个像素仍依次收到原 group -> tree -> tap 的贡献，不使用归约或临时累加图。
    constexpr int stripe_width = 64;
    const int stripes = (output.cols + stripe_width - 1) / stripe_width;
    independentRanges(stripes, output.total(), [&](const cv::Range& columns) {
        const int first = columns.start * stripe_width;
        const int last = std::min(output.cols, columns.end * stripe_width);
        for (int group = 0; group < lowpass.rows / 2; ++group) {
            for (int tree = 0; tree < 2; ++tree) {
                const auto* low = lowpass.ptr<double>(2 * group + tree);
                const auto* high = highpass.ptr<double>(2 * group + 1 - tree);
                for (int tap = 0; tap < 10; ++tap) {
                    const int row = reflected(4 * group + 10 + tree - 2 * tap, output.rows);
                    auto* dst = output.ptr<double>(row);
                    const double lo = treeCoefficient(false, tree, tap);
                    const double hi = treeCoefficient(true, tree, tap);
                    for (int x = first; x < last; ++x) dst[x] += lo * low[x] + hi * high[x];
                }
            }
        }
    });
    return output;
}

/// 将 2×2 交错的四棵实树 a,b,c,d 组合成两个相反方向的复子带：
/// z₊=((a-d)+i(b+c))/√2，z₋=((a+d)+i(b-c))/√2。
/// 该正交组合用于水平、对角、垂直三组子带，合计六个方向。
void encodeDirections(const cv::Mat& plane, cv::Mat& positive, cv::Mat& negative) {
    CV_Assert(plane.rows % 2 == 0 && plane.cols % 2 == 0);
    positive.create(plane.rows / 2, plane.cols / 2, CV_64FC2);
    negative.create(positive.size(), CV_64FC2);
    independentRanges(positive.rows, positive.total(), [&](const cv::Range& rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            const auto* even = plane.ptr<double>(2 * y);
            const auto* odd = plane.ptr<double>(2 * y + 1);
            auto* pos = positive.ptr<cv::Vec2d>(y);
            auto* neg = negative.ptr<cv::Vec2d>(y);
            for (int x = 0; x < positive.cols; ++x) {
                const double a = even[2 * x], b = even[2 * x + 1];
                const double c = odd[2 * x], d = odd[2 * x + 1];
                pos[x] = cv::Vec2d(a - d, b + c) * inverse_sqrt_two;
                neg[x] = cv::Vec2d(a + d, b - c) * inverse_sqrt_two;
            }
        }
    });
}

/// 上述正交方向组合的逆，恢复四棵实树的交错布局。
cv::Mat decodeDirections(const cv::Mat& positive, const cv::Mat& negative) {
    CV_Assert(positive.size() == negative.size() && positive.type() == CV_64FC2);
    cv::Mat plane(2 * positive.rows, 2 * positive.cols, CV_64F);
    independentRanges(positive.rows, plane.total(), [&](const cv::Range& rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            const auto* pos = positive.ptr<cv::Vec2d>(y);
            const auto* neg = negative.ptr<cv::Vec2d>(y);
            auto* even = plane.ptr<double>(2 * y);
            auto* odd = plane.ptr<double>(2 * y + 1);
            for (int x = 0; x < positive.cols; ++x) {
                even[2 * x] = (pos[x][0] + neg[x][0]) * inverse_sqrt_two;
                even[2 * x + 1] = (pos[x][1] + neg[x][1]) * inverse_sqrt_two;
                odd[2 * x] = (pos[x][1] - neg[x][1]) * inverse_sqrt_two;
                odd[2 * x + 1] = (neg[x][0] - pos[x][0]) * inverse_sqrt_two;
            }
        }
    });
    return plane;
}

/// 行方向复用列操作；显式转置使两维采用完全相同的采样与边界约定。
template<class Operation>
cv::Mat alongRows(const cv::Mat& input, Operation operation) {
    cv::Mat transposed, filtered, output;
    cv::transpose(input, transposed);
    filtered = operation(transposed);
    cv::transpose(filtered, output);
    return output;
}

} // 匿名命名空间

int effectiveLevels(cv::Size size, int requested_levels) {
    if (size.width <= 0 || size.height <= 0 || requested_levels < 1 || requested_levels > 16)
        throw std::invalid_argument("Invalid DTCWT image size or level count");
    int rows = size.height + size.height % 2;
    int cols = size.width + size.width % 2;
    int levels = 1;
    while (levels < requested_levels && rows > 2 && cols > 2) {
        rows = (rows + rows % 4) / 2;
        cols = (cols + cols % 4) / 2;
        ++levels;
    }
    return levels;
}

Pyramid forward(const cv::Mat& image, int requested_levels, const Checkpoint& checkpoint) {
    if (image.empty() || (image.type() != CV_32FC1 && image.type() != CV_64FC1))
        throw std::invalid_argument("DTCWT forward requires a nonempty float grayscale image");
    const int levels = effectiveLevels(image.size(), requested_levels);
    Pyramid result;
    result.original_size = image.size();
    cv::Mat lowpass;
    image.convertTo(lowpass, CV_64F);
    result.highpass.reserve(static_cast<std::size_t>(levels));
    result.input_sizes.reserve(static_cast<std::size_t>(levels));
    for (int level = 0; level < levels; ++level) {
        if (checkpoint) checkpoint();
        result.input_sizes.push_back(lowpass.size());
        if (level == 0) {
            // 首层偶数布局是四棵实树成对分组的前提；重复末端不会改变原图有效区。
            cv::copyMakeBorder(lowpass, lowpass, 0, lowpass.rows % 2, 0, lowpass.cols % 2,
                               cv::BORDER_REFLECT);
        } else {
            // 每棵树降采样前须有偶数点；前后各补一点，保留对称的相位位置。
            const int vertical = lowpass.rows % 4 == 0 ? 0 : 1;
            const int horizontal = lowpass.cols % 4 == 0 ? 0 : 1;
            cv::copyMakeBorder(lowpass, lowpass, vertical, vertical, horizontal, horizontal,
                               cv::BORDER_REFLECT);
        }
        const auto low = [level](const cv::Mat& m) {
            return level == 0 ? symmetricColumns(m, first_low) : analyzeColumns(m, false);
        };
        const auto high = [level](const cv::Mat& m) {
            return level == 0 ? symmetricColumns(m, first_high) : analyzeColumns(m, true);
        };
        const cv::Mat vertical_low = low(lowpass);
        const cv::Mat vertical_high = high(lowpass);
        DirectionBands bands;
        encodeDirections(alongRows(vertical_high, low), bands[0], bands[5]);
        encodeDirections(alongRows(vertical_high, high), bands[1], bands[4]);
        encodeDirections(alongRows(vertical_low, high), bands[2], bands[3]);
        lowpass = alongRows(vertical_low, low);
        result.highpass.push_back(std::move(bands));
    }
    result.lowpass = std::move(lowpass);
    return result;
}

cv::Mat inverse(const Pyramid& pyramid, const Checkpoint& checkpoint) {
    if (pyramid.highpass.empty() || pyramid.input_sizes.size() != pyramid.highpass.size())
        throw std::invalid_argument("Invalid DTCWT pyramid metadata");
    cv::Mat lowpass = pyramid.lowpass;
    for (std::size_t index = pyramid.highpass.size(); index-- > 0;) {
        if (checkpoint) checkpoint();
        const auto& bands = pyramid.highpass[index];
        const cv::Mat vertical_high = decodeDirections(bands[0], bands[5]);
        const cv::Mat diagonal_high = decodeDirections(bands[1], bands[4]);
        const cv::Mat horizontal_high = decodeDirections(bands[2], bands[3]);
        const auto combine = [index](const cv::Mat& lo, const cv::Mat& hi) -> cv::Mat {
            if (index != 0) return synthesizeColumns(lo, hi);
            return symmetricColumns(lo, first_low_synthesis) +
                   symmetricColumns(hi, first_high_synthesis);
        };
        // 先撤销列分析，再撤销行分析；方向解码与分析时的水平/垂直子带一致。
        const cv::Mat column_low = combine(lowpass, vertical_high);
        const cv::Mat column_high = combine(horizontal_high, diagonal_high);
        cv::Mat low_t, high_t, reconstructed_t, reconstructed;
        cv::transpose(column_low, low_t);
        cv::transpose(column_high, high_t);
        reconstructed_t = combine(low_t, high_t);
        cv::transpose(reconstructed_t, reconstructed);
        const cv::Size target = pyramid.input_sizes[index];
        const int top = index == 0 ? 0 : (reconstructed.rows - target.height) / 2;
        const int left = index == 0 ? 0 : (reconstructed.cols - target.width) / 2;
        // 克隆避免下一次补边看到 ROI 外的数据，也使最终结果只拥有原始有效图像。
        lowpass = reconstructed(cv::Rect(left, top, target.width, target.height)).clone();
    }
    CV_Assert(lowpass.size() == pyramid.original_size);
    return lowpass;
}

} // 命名空间 mif::detail::fusion::dtcwt
