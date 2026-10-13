module carven:semantic.semir.callable;

import :semantic.semir.constant_access;
import :semantic.semir.program;
import std;

enum class CallableAdaptationKind { CopyTarget, FunctionTarget, StatelessClosure, BorrowObject };

struct CallableAdaptation final {
    CallableAdaptationKind kind;
    std::optional<CallableID> callable;

    auto borrows_storage() const noexcept -> bool;
};

auto callable_identity(const SemIRProgram& program, TypeID type) noexcept
    -> std::optional<CallableID>;

// The adaptation has checked invariant children and a source failure subset.
// An unresolved failure relation can leave copy identity undetermined.
auto query_callable_adaptation(
    const ExecutionValueAccess& values,
    ConstructionTypeRef from,
    ConstructionTypeRef to
) noexcept -> std::optional<CallableAdaptation>;

// Types and closure bodies must be complete. Array adaptation applies the same
// leaf contract at each element; storage paths and lifetimes belong to consumers.
auto callable_adaptation(const SemIRProgram& program, TypeID from, TypeID to) noexcept
    -> CallableAdaptation;
