module;

#include <carven/hosting/api.hpp>
#include <carven/hosting/value.hpp>
#include <carven/runtime/execution/execution.hpp>

module carven:hosting.environment.impl;

import :hosting.environment;
import :semantic.evaluation.execution;
import :semantic.evaluation.limits;
import std;

extern "C++" {

auto host_call_options() noexcept -> HostCallOptions {
    const auto limits = static_execution_limits();
    return {
        .output = {.context = nullptr, .write = nullptr},
        .allow_host_effects = false,
        .limits =
            {.steps = limits.steps,
             .byte_work = limits.byte_work,
             .aggregate_work = limits.aggregate_work},
        .native_types = {}
    };
}

} // extern "C++"

namespace {

auto environment_error(HostErrorKind kind, std::string message) noexcept
    -> std::unexpected<HostError> {
    return std::unexpected(hosting_error(kind, std::move(message)));
}

} // namespace

auto hosting_contract_valid(const HostCallContract& contract) noexcept -> bool {
    if (contract.name.empty()
        || (contract.source_signature && contract.source_signature->empty())
        || contract.result.describe == nullptr
        || !std::ranges::all_of(contract.parameters, [](const auto& parameter) static noexcept {
               return parameter.type.describe != nullptr;
           })) {
        return false;
    }
    for (auto index = 0uz; index < contract.failures.size(); ++index) {
        const auto& failure = contract.failures[index];
        if (failure.module_path.empty() || failure.name.empty()) {
            return false;
        }
        for (auto previous = 0uz; previous < index; ++previous) {
            if (contract.failures[previous].module_path == failure.module_path
                && contract.failures[previous].name == failure.name) {
                return false;
            }
        }
    }
    return true;
}

auto add_host_native_types(
    HostEnvironment& environment,
    std::span<const HostNativeType> types
) noexcept -> std::expected<void, HostError> {
    for (const auto& type : types) {
        if (type.name.empty() || type.type == nullptr) {
            return environment_error(
                HostErrorKind::Arguments,
                "native type requires a name and identity"
            );
        }
        const auto found =
            std::ranges::find(environment.native_types, type.name, &HostNativeType::name);
        if (found == environment.native_types.end()) {
            environment.native_types.push_back(type);
        } else if (found->type != type.type) {
            return environment_error(
                HostErrorKind::Signature,
                "native source name has conflicting carrier types"
            );
        } else if (found->encode == nullptr || found->decode == nullptr) {
            *found = type;
        }
    }
    return {};
}

auto make_host_environment(
    HostCallOptions options,
    std::span<const HostServiceBinding> services,
    std::span<const HostObjectBinding> objects
) noexcept -> std::expected<HostEnvironment, HostError> {
    auto registrations = std::vector<HostProvider>();
    for (const auto& registration : objects) {
        const auto& binding = HostObjectAccess::registration(registration);
        if (binding.contract.name.empty() || !binding.object.alive()) {
            return environment_error(
                HostErrorKind::Arguments,
                "object binding requires a live object and name"
            );
        }
        registrations.push_back(
            {.context = nullptr,
             .call = binding.call,
             .cancel = nullptr,
             .object = binding.object,
             .object_type = binding.type,
             .contract = binding.contract,
             .static_effects = std::nullopt}
        );
    }
    auto native = std::vector<carven::runtime::hosting::detail::Binding>(registrations.size());
    for (const auto& binding : services) {
        const auto& service = HostServiceAccess::registration(binding);
        if (service.contract.name.empty()
            || (!service.contract.cpp_operation
                && ((!service.contract.source_signature
                     || service.contract.source_signature->empty())
                    || service.native.target == nullptr))) {
            return environment_error(
                HostErrorKind::Arguments,
                "host service requires a contract and binding"
            );
        }
        if (!service.contract.cpp_operation
            && std::ranges::any_of(
                service.contract.parameters,
                [](const auto& parameter) static noexcept {
                    return parameter.access == HostCallAccess::Write;
                }
            )) {
            return environment_error(
                HostErrorKind::Signature,
                "host service Write parameters are not supported"
            );
        }
        registrations.push_back(
            {.context = service.context,
             .call = service.call,
             .cancel = service.cancel,
             .object = std::nullopt,
             .object_type = nullptr,
             .contract = service.contract,
             .static_effects = service.static_effects}
        );
        native.push_back(service.native);
    }
    for (const auto& provider : registrations) {
        if (!hosting_contract_valid(provider.contract)) {
            return environment_error(
                HostErrorKind::Signature,
                "host binding requires a valid call contract and registered types"
            );
        }
    }
    for (auto index = 0uz; index < registrations.size(); ++index) {
        for (auto previous = 0uz; previous < index; ++previous) {
            const auto& left = registrations[previous].contract;
            const auto& right = registrations[index].contract;
            const auto same_native_signature = left.cpp_operation
                && right.cpp_operation
                && left.cpp_operation == right.cpp_operation
                && left.result.describe().native_type == right.result.describe().native_type
                && std::ranges::equal(left.parameters,
                                      right.parameters,
                                      [](const auto& a, const auto& b) static noexcept {
                                          return a.access == b.access
                                              && a.type.describe().native_type
                                              == b.type.describe().native_type;
                                      });
            if (left.name == right.name
                && ((!left.cpp_operation || !right.cpp_operation) || same_native_signature)) {
                return environment_error(
                    HostErrorKind::Arguments,
                    "duplicate host binding: " + registrations[index].contract.name
                );
            }
        }
    }
    auto environment = HostEnvironment {
        .providers = std::move(registrations),
        .native = std::move(native),
        .output = options.output,
        .host_effects = options.allow_host_effects ? HostEffects::Allow : HostEffects::Deny,
        .limits = options.limits,
        .native_types = {},
        .native_driver = std::make_unique<carven::runtime::execution::Driver>()
    };
    for (auto index = 0uz; index < options.native_types.size(); ++index) {
        for (auto previous = 0uz; previous < index; ++previous) {
            if (options.native_types[index].name == options.native_types[previous].name) {
                return environment_error(
                    HostErrorKind::Arguments,
                    "duplicate native type: " + options.native_types[index].name
                );
            }
        }
    }
    if (auto collected = add_host_native_types(environment, options.native_types); !collected) {
        return std::unexpected(std::move(collected.error()));
    }
    for (const auto& provider : environment.providers) {
        if (auto collected = add_host_native_types(environment, provider.contract.native_types);
            !collected) {
            return std::unexpected(std::move(collected.error()));
        }
    }
    return environment;
}
