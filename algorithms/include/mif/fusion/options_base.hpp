#pragma once

#include <memory>

namespace mif {

/// 融合参数的多态入口；具体参数类型唯一确定方法，不再重复填写 method。
struct FusionOptionsBase {
    virtual ~FusionOptionsBase() = default;
    /// 是否把方法提供的权重诊断图加入返回结果；不影响算法本身的权重计算。
    bool include_weight_maps = false;
    /// 保留具体类型的独立副本，避免后台任务或释放 GIL 后读取可变配置。
    virtual std::unique_ptr<FusionOptionsBase> clone() const = 0;
};

} // 命名空间 mif
