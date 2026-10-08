#pragma once

#include <memory>

namespace mif {

/// 配准参数的公共接口；具体参数类型决定配准方法，无需另设方法枚举。
/// 同步入口只借用参数；异步任务和语言绑定可通过 clone() 创建独立快照。
struct RegistrationOptionsBase {
    virtual ~RegistrationOptionsBase() = default;

    /// 估计变换时的最长边上限，[16, 8192]，默认 1200；不会放大小图。
    /// 开启配准后，工作图短边须至少为 16；最终重采样仍使用原始分辨率。
    /// 跳过配准时不使用此值，但仍检查其合法范围。
    int max_working_dimension = 1200;

    /// 复制实际派生类型的全部参数；返回对象与本对象可独立修改。
    virtual std::unique_ptr<RegistrationOptionsBase> clone() const = 0;
};

} // 命名空间 mif
