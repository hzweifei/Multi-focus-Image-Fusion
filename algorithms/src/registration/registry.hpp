#pragma once

#include <mif/export.hpp>
#include <mif/registration/options_base.hpp>
#include <opencv2/core.hpp>
#include <functional>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <type_traits>
#include <utility>

namespace mif::detail::registration {

/// 内部变换估计接口：每次配准创建一个对象，可以缓存参考图或参考特征。
/// 输入为工作分辨率下、同尺寸的 CV_32FC1 灰度图，值域 [0, 1]。
/// 实现只估计变换，不裁剪或重采样原图，也不持有跨任务的可变全局状态。
class Estimator {
public:
    virtual ~Estimator() = default;
    /// 返回参考坐标到源图坐标的 3×3 CV_64F 齐次矩阵；无法可靠求解时必须抛异常。
    /// 平移/仿射也补齐末行 [0, 0, 1]，使公共流程与具体求解方法解耦。
    virtual cv::Mat estimate(const cv::Mat& source_gray) = 0;
    /// 是否使用投影模型。为 true 时输出 3×3 并执行透视采样，否则输出 2×3。
    /// 由每次任务的实际估计器报告，公共流程无需知道具体算法及其参数类型。
    virtual bool isProjective() const noexcept = 0;
};

/// 注册项按值取得后在锁外执行；校验器和工厂不得持有一次调用之外的可变求解状态。
/// copy_only 只表示独立复制输入；否则必须提供每次创建新估计器的工厂。
struct RegistrationMethodEntry {
    std::function<void(const RegistrationOptionsBase&)> validate;
    std::function<std::unique_ptr<Estimator>(const cv::Mat&, const RegistrationOptionsBase&)> create;
    bool copy_only = false;
};

/// 注册表由核心库唯一持有；导出供内部扩展测试使用，私有头不安装到 SDK。
/// 重复参数类型、缺失回调或未知类型均明确报错，不覆盖已有方法。
MIF_EXPORT void registerRegistrationMethodImpl(std::type_index type, RegistrationMethodEntry entry);
MIF_EXPORT RegistrationMethodEntry findRegistrationMethod(const RegistrationOptionsBase& options);

/// 把具体参数、校验函数及工厂一次绑定，避免调用者另传方法标识造成类型不匹配。
/// 查找使用 typeid 的准确动态类型；不能用一个已注册父类替代未注册的派生方法。
template<class Options>
void registerRegistrationMethod(void (*validate)(const Options&),
    std::unique_ptr<Estimator> (*create)(const cv::Mat&, const Options&)) {
    static_assert(std::is_base_of_v<RegistrationOptionsBase, Options>,
                  "Registration parameters must derive from RegistrationOptionsBase");
    if (!validate || !create) throw std::invalid_argument("Registration method requires validation and a factory");
    RegistrationMethodEntry entry;
    entry.validate = [validate](const RegistrationOptionsBase& options) {
        validate(dynamic_cast<const Options&>(options));
    };
    entry.create = [create](const cv::Mat& reference, const RegistrationOptionsBase& options) {
        return create(reference, dynamic_cast<const Options&>(options));
    };
    registerRegistrationMethodImpl(typeid(Options), std::move(entry));
}

/// 注册跳过几何求解的复制方法，保留小图、原像素以及独立缓冲区的约定。
template<class Options>
void registerRegistrationCopyMethod(void (*validate)(const Options&)) {
    static_assert(std::is_base_of_v<RegistrationOptionsBase, Options>,
                  "Registration parameters must derive from RegistrationOptionsBase");
    if (!validate) throw std::invalid_argument("Registration copy method requires validation");
    RegistrationMethodEntry entry;
    entry.validate = [validate](const RegistrationOptionsBase& options) {
        validate(dynamic_cast<const Options&>(options));
    };
    entry.copy_only = true;
    registerRegistrationMethodImpl(typeid(Options), std::move(entry));
}

} // 命名空间 mif::detail::registration
