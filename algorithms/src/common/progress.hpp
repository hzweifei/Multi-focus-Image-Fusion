#pragma once

#include <mif/fusion.hpp>

namespace mif::detail {

// 此处只包含各模块共用的回调约定，不依赖具体融合或配准实现。
/// 同步报告阶段进度；空回调直接跳过，返回 false 时抛出 Cancelled。
/// 配准与融合共用同一取消约定，回调自身抛出的异常直接交给调用方处理。
inline void report(const ProgressCallback& callback, int percent, const std::string& stage) {
    if (callback && !callback(percent, stage)) throw Cancelled();
}

} // 命名空间 mif::detail
