module;

#include <carven/hosting/api.hpp>

module carven:hosting.program;

import :semantic.evaluation.execution;
import :semantic.evaluation.value;
import :semantic.semir.program;
import std;

extern "C++" {

struct HostProgramState final {
    SemIRProgram program;
    std::string diagnostics;
    std::string module_path;

    static auto load(
        std::string_view module_path,
        std::span<const HostSource> batch,
        HostOutput output,
        HostSourceResolver dependencies,
        const HostEnvironment* environment
    ) noexcept -> std::expected<std::shared_ptr<const HostProgramState>, HostError>;

    auto function(std::string_view name) const noexcept -> std::expected<FunctionID, HostError>;

    auto complete(
        FunctionID function,
        ExecutionResult<ExecutionValue> result,
        HostLimits limits,
        std::span<const HostNativeType> native_types
    ) const noexcept -> std::expected<HostOutcome, HostError>;
};

struct HostFunctionState final {
    std::shared_ptr<const HostProgramState> owner;
    FunctionID function;
};

} // extern "C++"
