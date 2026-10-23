module;

#include <carven/hosting/api.hpp>
#include <carven/hosting/value.hpp>
#include <carven/runtime/execution/execution.hpp>

module carven:hosting.environment;

import :semantic.evaluation.execution;
import std;

struct HostProvider final {
    void* context;
    HostTypedCall call;
    void (*cancel)(void*, std::uint64_t) noexcept;
    std::optional<HostObjectRef> object;
    const void* object_type;
    HostCallContract contract;
    std::optional<HostServiceEffects> static_effects;
};

extern "C++" {

struct HostEnvironment final {
    std::vector<HostProvider> providers;
    std::vector<carven::runtime::hosting::detail::Binding> native;
    HostOutput output;
    HostEffects host_effects;
    HostLimits limits;
    std::vector<HostNativeType> native_types;
    std::unique_ptr<carven::runtime::execution::Driver> native_driver;
};

} // extern "C++"

auto hosting_contract_valid(const HostCallContract& contract) noexcept -> bool;

auto add_host_native_types(
    HostEnvironment& environment,
    std::span<const HostNativeType> types
) noexcept -> std::expected<void, HostError>;

auto make_host_environment(
    HostCallOptions options,
    std::span<const HostServiceBinding> services,
    std::span<const HostObjectBinding> objects
) noexcept -> std::expected<HostEnvironment, HostError>;
