#include "registration/registry.hpp"
#include <mif/registration/no_registration_options.hpp>
#include <mutex>
#include <unordered_map>

namespace mif::detail::registration {

// 每种实现提供自己的注册函数；显式引用让静态库链接器也能找到对应对象文件。
void registerEccRegistrationMethod();
void registerSiftRegistrationMethod();

namespace {

/// 无方法专属数值；公共工作尺寸约束仍由配准入口统一校验。
void validateNoRegistration(const NoRegistrationOptions&) {}

void registerNoRegistrationMethod() {
    registerRegistrationCopyMethod<NoRegistrationOptions>(validateNoRegistration);
}

/// 显式注册全部内置方法；重复调用和并行首次使用均由 once_flag 保证安全。
void registerBuiltinRegistrationMethods() {
    static std::once_flag once;
    std::call_once(once, [] {
        registerNoRegistrationMethod();
        registerEccRegistrationMethod();
        registerSiftRegistrationMethod();
    });
}

/// 仅核心库创建此实例；模板包装、SDK 调用者和测试不各自持有一份注册表。
struct Registry {
    std::mutex mutex;
    std::unordered_map<std::type_index, RegistrationMethodEntry> methods;
};

Registry& registry() {
    static Registry instance;
    return instance;
}

} // 匿名命名空间

void registerRegistrationMethodImpl(std::type_index type, RegistrationMethodEntry entry) {
    if (!entry.validate || (entry.copy_only ? static_cast<bool>(entry.create) : !entry.create))
        throw std::invalid_argument("Invalid registration method descriptor");
    auto& state = registry();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.methods.emplace(type, std::move(entry)).second)
        throw std::invalid_argument("Registration options type is already registered");
}

RegistrationMethodEntry findRegistrationMethod(const RegistrationOptionsBase& options) {
    // 内置注册会获取注册表锁，所以必须在查找加锁之前完成，避免递归死锁。
    registerBuiltinRegistrationMethods();
    auto& state = registry();
    std::lock_guard<std::mutex> lock(state.mutex);
    const auto found = state.methods.find(typeid(options));
    if (found == state.methods.end())
        throw std::invalid_argument("Unregistered registration options type");
    return found->second;
}

} // 命名空间 mif::detail::registration
