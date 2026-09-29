#pragma once

#include <functional>
#include <stdexcept>
#include <string>

namespace mif {

/// 进度回调返回 false 时抛出，调用者可区分用户取消和处理失败。
class Cancelled : public std::runtime_error {
public:
    /// 配准和融合共用的取消异常，消息不限定某个处理阶段。
    Cancelled() : std::runtime_error("Processing cancelled") {}
};

/// 在调用处理函数的线程内同步执行，不会自动切换到界面线程。
/// percent 为 [0, 100]；单独调用配准或融合时，各自覆盖完整进度范围。
/// stage 为 prepare、align、focus、weights、blend、pyramid、finish 或 done。
/// 返回 false 后抛出 Cancelled；回调自己抛出的异常原样传播。
/// 取消检查只在进度报告点进行，不会中断正在执行的单次 OpenCV 操作。
using ProgressCallback = std::function<bool(int percent, const std::string& stage)>;

} // 命名空间 mif
