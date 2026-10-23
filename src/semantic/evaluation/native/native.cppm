module carven:semantic.evaluation.native;

import :semantic.evaluation.execution;
import :semantic.evaluation.limits;
import :semantic.evaluation.native.call;
import :semantic.evaluation.native.request;
import :semantic.evaluation.native.value;
import :semantic.evaluation.runtime;
import :semantic.evaluation.value;
import std;

// Bindings and this host outlive the analysis/interpreter and its cancellation.
// The host supplies implementations; all source contracts remain in SemIR.
class ExecutionNativeHost final {
public:
    ExecutionNativeHost(
        std::span<const ExecutionNativeBinding> bindings,
        ExecutionLimits limits
    ) noexcept;
    ExecutionNativeHost(const ExecutionNativeHost&) = delete;
    auto operator=(const ExecutionNativeHost&) -> ExecutionNativeHost& = delete;
    ~ExecutionNativeHost() = default;

    template<typename Reader>
        requires std::derived_from<Reader, ExecutionValueAccess>
        && requires (Reader& reader, ConstructionTypeRef type, ConstructionFailureRef failures) {
               {
                   reader.try_canonicalize_declared_type(type)
               } -> std::same_as<std::optional<TypeID>>;
               {
                   reader.construction_failure_known_members(failures)
               } -> std::same_as<std::vector<TypeID>>;
           }
    auto operator()(
        Reader& values,
        SemanticExecutionContext& context,
        ExecutionCallSite target,
        std::span<ExecutionValue> arguments,
        ProgramOriginID origin
    ) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure> {
        const auto* expression = std::get_if<const SemanticExpression*>(&target);
        if (expression == nullptr) {
            return execution_native_error(
                origin,
                "native function has no registered implementation"
            );
        }
        const auto close_type = [&](ConstructionTypeRef type) noexcept {
            return values.try_canonicalize_declared_type(type);
        };
        const auto close_failures = [&](ConstructionFailureRef failures) noexcept {
            return values.construction_failure_known_members(failures);
        };
        const auto request =
            execution_native_request(values, **expression, close_type, close_failures);
        if (!request) {
            return execution_native_error(
                origin,
                "native request has no closed execution contract"
            );
        }
        return call(values, context, *request, arguments, origin);
    }

    auto operator()(
        const ExecutionValueAccess& values,
        SemanticExecutionContext& context,
        ExecutionCallSite target,
        std::span<ExecutionValue> arguments,
        ProgramOriginID origin
    ) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure>;

private:
    std::span<const ExecutionNativeBinding> bindings;
    ExecutionNativeContext state;
    carven::runtime::hosting::detail::Scope scope;

    auto call(
        const ExecutionValueAccess& values,
        SemanticExecutionContext& context,
        const ExecutionNativeRequest& request,
        std::span<ExecutionValue> arguments,
        ProgramOriginID origin
    ) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure>;
};
