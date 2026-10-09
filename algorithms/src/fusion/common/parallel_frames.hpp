#pragma once

#include <opencv2/core.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>

namespace mif::detail::fusion {

/// 只并行帧间独立的评分或滤波，调用方应预分配结果槽，work 仅写自己的槽。
/// 每批最多 4 帧，大图最多 2 帧，限制同时存在的滤波临时缓冲；小图直接串行。
/// 不调整 OpenCV 的全局线程设置，线程数为 1 时保持逐帧执行。
/// before 始终在调用线程按输入顺序执行，并在本批工作开始前完成。
/// 因此取消或回调异常可能比逐帧执行更早停止计算，但报告的事件序列不变；
/// 已启动的上一批必定已结束，不会有后台任务在取消后继续访问局部变量。
/// 工作异常先按槽保存，等待本批全部结束后，在调用线程按输入顺序重抛。
template<class Before, class Work>
void parallelFrames(std::size_t count, cv::Size image_size, const Before& before, const Work& work) {
    const auto pixels = static_cast<std::size_t>(image_size.width) * static_cast<std::size_t>(image_size.height);
    const int threads = cv::getNumThreads();
    const std::size_t limit = pixels < 512u * 512u || threads <= 1 ? 1u
        : std::min<std::size_t>(static_cast<std::size_t>(threads), pixels >= 2048u * 2048u ? 2u : 4u);
    for (std::size_t begin = 0; begin < count; begin += limit) {
        const auto batch_size = std::min(limit, count - begin);
        for (std::size_t slot = 0; slot < batch_size; ++slot) before(begin + slot);
        if (batch_size == 1) {
            work(begin);
            continue;
        }
        std::array<std::exception_ptr, 4> failures{};
        cv::parallel_for_(cv::Range(0, static_cast<int>(batch_size)), [&](const cv::Range& range) {
            for (int slot = range.start; slot < range.end; ++slot) {
                try {
                    work(begin + static_cast<std::size_t>(slot));
                } catch (...) {
                    failures[static_cast<std::size_t>(slot)] = std::current_exception();
                }
            }
        }, static_cast<double>(batch_size));
        for (std::size_t slot = 0; slot < batch_size; ++slot)
            if (failures[slot]) std::rethrow_exception(failures[slot]);
    }
}

} // 命名空间 mif::detail::fusion
