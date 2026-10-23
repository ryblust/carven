#include <carven/hosting/standard.hpp>

#include <carven/hosting/operation.hpp>
#include <carven/std/providers.hpp>

#include <string_view>
#include <vector>

auto host_standard_services() noexcept -> std::vector<HostServiceBinding> {
    auto services = std::vector<HostServiceBinding>();
    carven::standard::visit_native_bindings(
        [&]<auto Function>(std::string_view name, bool environment_effects) noexcept {
            services.push_back(
                host_cpp_operation<Function>(name).with_static_execution(
                    environment_effects ? HostServiceEffects::Environment : HostServiceEffects::None
                )
            );
        },
        [&]<typename From, typename To>(std::string_view name) noexcept {
            services.push_back(
                host_cpp_conversion<From, To>(name).with_static_execution(HostServiceEffects::None)
            );
        }
    );
    return services;
}
