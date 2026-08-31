module carven:semantic.analysis.call_contract;

import :semantic.analysis.session.read;
import :semantic.hir;
import :semantic.hir.ids;
import std;

struct ConcreteCallableFailure final {
    CallableID callable;
};

struct FixedSignatureFailure final {
    FailureSetID failure_set;
};

struct ForeignCallableFailure final {};

using CallFailureSource =
    std::variant<ConcreteCallableFailure, FixedSignatureFailure, ForeignCallableFailure>;

struct CallContractView final {
    std::span<const HIRFunctionParameterType> parameters;
    HIRTypeID result;
    CallFailureSource failure_source;
};

auto callable_contract(SemanticDraftView semantic, HIRTypeID verified_callable_type) noexcept
    -> std::optional<CallContractView>;

auto call_contract(SemanticDraftView semantic, HIRExprID verified_call) noexcept
    -> CallContractView;
