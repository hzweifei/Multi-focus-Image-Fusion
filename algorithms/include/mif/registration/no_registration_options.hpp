#pragma once

#include <mif/registration/options_base.hpp>

namespace mif {

/// 跳过配准，返回各输入的独立副本、完整裁剪区域和 2×3 单位变换。
/// 不执行浮点归一化、重采样或共同区域裁剪，原像素逐值保留。
struct NoRegistrationOptions final : RegistrationOptionsBase {
    std::unique_ptr<RegistrationOptionsBase> clone() const override {
        return std::make_unique<NoRegistrationOptions>(*this);
    }
};

} // 命名空间 mif
