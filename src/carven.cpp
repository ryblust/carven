module;

#include <carven/std/providers.hpp>

module carven:main;

import :driver.cli;
import :semantic.evaluation.native;
import std;

extern "C++" auto main(int argc, const char* const* argv) noexcept -> int {
    auto native_bindings = std::vector<ExecutionNativeBinding>();
    carven::standard::visit_native_bindings(
        [&]<auto Function>(std::string_view name, bool environment_effects) noexcept {
            native_bindings.push_back(
                execution_native_function<Function>(
                    std::string(name),
                    {.static_execution = true, .environment_effects = environment_effects}
                )
            );
        },
        [&]<typename From, typename To>(std::string_view name) noexcept {
            native_bindings.push_back(
                execution_native_conversion<From, To>(
                    std::string(name),
                    {.static_execution = true, .environment_effects = false}
                )
            );
        }
    );

    return carven_main(argc, argv, native_bindings);
}
