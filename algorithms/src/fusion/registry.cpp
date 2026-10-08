#include "fusion/registry.hpp"
#include <mutex>
#include <unordered_map>
#include <utility>

namespace mif::detail::fusion {

// 显式引用各方法的注册函数，确保静态库链接器保留对应实现。
void registerGuidedFilterFusionMethod();
void registerLaplacianPyramidFusionMethod();
void registerBlockVarianceFusionMethod();
void registerDtcwtFusionMethod();
void registerGfgFgfFusionMethod();

namespace {
struct Registry {
    std::mutex mutex;
    std::unordered_map<std::type_index, FusionMethodEntry> methods;
};
Registry& registry() {
    static Registry instance;
    return instance;
}

/// 内置方法只登记一次；新增方法在这里添加一条调用，不修改 fuse()。
void registerBuiltinFusionMethods() {
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        registerGuidedFilterFusionMethod();
        registerLaplacianPyramidFusionMethod();
        registerBlockVarianceFusionMethod();
        registerDtcwtFusionMethod();
        registerGfgFgfFusionMethod();
    });
}
} // 匿名命名空间

void addFusionMethod(std::type_index type, FusionMethodEntry entry) {
    if (!entry.validate || !entry.run) throw std::invalid_argument("Incomplete fusion method registration");
    auto& state = registry();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.methods.emplace(type, std::move(entry)).second)
        throw std::invalid_argument("Fusion options type is already registered");
}

FusionMethodEntry findFusionMethod(const FusionOptionsBase& options) {
    // 显式调用确保静态库链接器保留内置方法，避免依赖全局对象构造顺序。
    registerBuiltinFusionMethods();
    auto& state = registry();
    std::lock_guard<std::mutex> lock(state.mutex);
    const auto found = state.methods.find(typeid(options));
    if (found == state.methods.end()) throw std::invalid_argument("Unregistered fusion options type");
    // 复制后释放锁，运行算法和进度回调时不持有注册表锁。
    return found->second;
}
} // 命名空间 mif::detail::fusion
