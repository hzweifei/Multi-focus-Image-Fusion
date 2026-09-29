#include "registration/registration.hpp"

#include <opencv2/calib3d.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mif::detail::registration {
namespace {

// 单应矩阵有八个自由度。至少六个不同位置的对应点，给四点最小解留出冗余，
// 避免少数偶然匹配恰好产生一个形式上有效、实际无法可靠外推的变换。
constexpr size_t minimum_matches = 6;

/// 检查内点是否能在二维平面中约束单应变换，拒绝集中在一点或近似直线上的点集。
/// 判定使用工作分辨率坐标；绝对阈值避免亚像素小区域，相对阈值避免过度集中。
void requireSpatialSpread(const std::vector<cv::Point2f>& points, const cv::Size& image_size,
                          const char* image_role) {
    cv::Point2d mean(0.0, 0.0);
    for (const auto& point : points) {
        mean.x += point.x;
        mean.y += point.y;
    }
    mean *= 1.0 / static_cast<double>(points.size());

    // 协方差矩阵的较小特征值衡量垂直于点集主方向的离散程度。
    // 同时检查特征值比，可识别斜向的近共线点集，避免仅检查横纵跨度的漏洞。
    double xx = 0.0, xy = 0.0, yy = 0.0;
    for (const auto& point : points) {
        const double x = point.x - mean.x;
        const double y = point.y - mean.y;
        xx += x * x;
        xy += x * y;
        yy += y * y;
    }
    const double count = static_cast<double>(points.size());
    xx /= count;
    xy /= count;
    yy /= count;
    const double discriminant = std::hypot(xx - yy, 2.0 * xy);
    const double largest = 0.5 * (xx + yy + discriminant);
    const double smallest = 0.5 * (xx + yy - discriminant);

    std::vector<cv::Point2f> hull;
    cv::convexHull(points, hull);
    const double area = std::abs(cv::contourArea(hull));
    const double minimum_area = std::max(16.0,
        static_cast<double>(image_size.width) * image_size.height * 1e-4);
    // 次方向标准差至少为 1 像素，方差比至少为 1/1000；凸包还需覆盖足够面积。
    if (smallest < 1.0 || smallest < largest * 1e-3 || area < minimum_area)
        throw std::runtime_error(std::string("Homography has spatially degenerate ") +
                                 image_role + " inliers (clustered or nearly collinear)");
}

/// SIFT 特征配准估计器。参考图的特征仅提取一次，供同一焦点序列的所有源图复用。
/// 每个配准任务独立创建实例；可变 SIFT 状态不会在并行任务之间共享。
class HomographyEstimator final : public Estimator {
public:
    HomographyEstimator(const cv::Mat& reference_gray, const RegistrationOptions& options)
        : sift_(cv::SIFT::create(options.max_features)),
          reference_size_(reference_gray.size()),
          match_ratio_(options.match_ratio),
          ransac_threshold_(options.ransac_threshold),
          minimum_inlier_ratio_(options.min_inlier_ratio) {
        extractFeatures(reference_gray, reference_keypoints_, reference_descriptors_, "reference");
    }

    /// 输入与参考图同尺寸的归一化浮点灰度图，返回参考坐标到源图坐标的 3×3 矩阵。
    cv::Mat estimate(const cv::Mat& source_gray) override {
        std::vector<cv::KeyPoint> source_keypoints;
        cv::Mat source_descriptors;
        extractFeatures(source_gray, source_keypoints, source_descriptors, "source");

        // 查询是参考图，训练集合是源图，因此后续点对和单应矩阵方向始终为参考→源图。
        // SIFT 描述子使用 L2 距离；knn 的两个近邻用于 Lowe 比值筛选。
        cv::BFMatcher matcher(cv::NORM_L2, false);
        std::vector<std::vector<cv::DMatch>> neighbors;
        matcher.knnMatch(reference_descriptors_, source_descriptors, neighbors, 2);
        std::vector<cv::DMatch> candidates;
        for (const auto& pair : neighbors) {
            if (pair.size() < 2) continue;
            const auto& best = pair[0];
            const auto& next = pair[1];
            // 次近邻距离为零意味着描述子无法区分；不将这类歧义匹配交给 RANSAC。
            if (std::isfinite(best.distance) && std::isfinite(next.distance) &&
                next.distance > 0.0f && best.distance < match_ratio_ * next.distance)
                candidates.push_back(best);
        }
        if (candidates.size() < minimum_matches)
            throw std::runtime_error("Homography requires at least 6 reliable SIFT matches after ratio filtering");

        // 多个参考特征可能指向同一个源特征，优先保留描述子距离最小的对应关系。
        // SIFT 还会为同一位置生成多个方向描述子；仅去重 trainIdx 不足以保证几何独立，
        // 因此同时去重双方坐标，防止重复点虚增匹配数和 RANSAC 内点数。
        std::stable_sort(candidates.begin(), candidates.end(),
            [](const cv::DMatch& a, const cv::DMatch& b) { return a.distance < b.distance; });
        std::vector<bool> used_source_indices(source_keypoints.size(), false);
        std::set<std::pair<float, float>> used_reference_positions, used_source_positions;
        std::vector<cv::Point2f> reference_points, source_points;
        reference_points.reserve(candidates.size());
        source_points.reserve(candidates.size());
        for (const auto& match : candidates) {
            if (used_source_indices[match.trainIdx]) continue;
            const cv::Point2f reference_point = reference_keypoints_[match.queryIdx].pt;
            const cv::Point2f source_point = source_keypoints[match.trainIdx].pt;
            const auto reference_position = std::make_pair(reference_point.x, reference_point.y);
            const auto source_position = std::make_pair(source_point.x, source_point.y);
            if (used_reference_positions.count(reference_position) ||
                used_source_positions.count(source_position)) continue;
            used_source_indices[match.trainIdx] = true;
            used_reference_positions.insert(reference_position);
            used_source_positions.insert(source_position);
            reference_points.push_back(reference_point);
            source_points.push_back(source_point);
        }
        if (reference_points.size() < minimum_matches)
            throw std::runtime_error("Homography requires at least 6 unique SIFT matches after duplicate removal");

        // 阈值以工作分辨率像素为单位，RANSAC 剔除错误匹配后由 OpenCV 优化模型。
        // 使用 OpenCV 默认的 2000 次迭代上限和 0.995 置信度，不复用 ECC 的迭代参数。
        std::vector<unsigned char> inliers;
        cv::Mat homography = cv::findHomography(reference_points, source_points, cv::RANSAC,
                                               ransac_threshold_, inliers);
        if (homography.empty() || inliers.size() != reference_points.size())
            throw std::runtime_error("Homography estimation failed: inconsistent or degenerate SIFT matches");

        std::vector<cv::Point2f> reference_inliers, source_inliers;
        for (size_t i = 0; i < inliers.size(); ++i) {
            if (!inliers[i]) continue;
            reference_inliers.push_back(reference_points[i]);
            source_inliers.push_back(source_points[i]);
        }
        const double inlier_ratio = static_cast<double>(reference_inliers.size()) /
                                    static_cast<double>(reference_points.size());
        if (reference_inliers.size() < minimum_matches || inlier_ratio < minimum_inlier_ratio_)
            throw std::runtime_error("Homography has insufficient RANSAC inliers or a low inlier ratio");

        // 两侧均需有二维分布；只检查参考侧会遗漏源图中挤成线或点的错误对应关系。
        requireSpatialSpread(reference_inliers, reference_size_, "reference");
        requireSpatialSpread(source_inliers, source_gray.size(), "source");

        // 输出固定为双精度。共享配准管线随后统一检查有限性、可逆性和投影地平线，
        // 并把工作分辨率变换恢复到原图坐标；此处不对原始高位深像素进行重采样。
        homography.convertTo(homography, CV_64F);
        return homography;
    }

private:
    /// SIFT 接受 8 位图像；这份转换仅用于特征提取，不替换用于重采样的高位深图像。
    void extractFeatures(const cv::Mat& gray, std::vector<cv::KeyPoint>& keypoints,
                         cv::Mat& descriptors, const char* image_role) {
        cv::Mat feature_image;
        gray.convertTo(feature_image, CV_8U, 255.0);
        sift_->detectAndCompute(feature_image, cv::noArray(), keypoints, descriptors);
        if (descriptors.empty() || descriptors.rows < static_cast<int>(minimum_matches))
            throw std::runtime_error(std::string("Homography requires at least 6 SIFT features in the ") +
                                     image_role + " image");
    }

    cv::Ptr<cv::SIFT> sift_;
    cv::Size reference_size_;
    double match_ratio_;
    double ransac_threshold_;
    double minimum_inlier_ratio_;
    std::vector<cv::KeyPoint> reference_keypoints_;
    cv::Mat reference_descriptors_;
};

} // 匿名命名空间

std::unique_ptr<Estimator> makeHomographyEstimator(const cv::Mat& reference_gray,
                                                  const RegistrationOptions& options) {
    return std::make_unique<HomographyEstimator>(reference_gray, options);
}

} // 命名空间 mif::detail::registration
