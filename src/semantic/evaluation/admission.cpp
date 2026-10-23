module carven:semantic.evaluation.admission.impl;

import :semantic.evaluation.admission;
import std;

auto unsupported_execution_expression(const SemanticExpression& source) noexcept
    -> std::optional<std::string_view> {
    if (const auto* operation = std::get_if<SemCpp>(&source.value); operation != nullptr
        && !std::holds_alternative<CppMemberOperation>(operation->operation)
        && !std::holds_alternative<CppConvertOperation>(operation->operation)) {
        return "native operation has no execution protocol";
    }
    if (const auto* call = std::get_if<SemCppCall>(&source.value); call != nullptr
        && !std::holds_alternative<CppNameReference>(call->callee)
        && !std::holds_alternative<CppMemberCallee<SemCppOperand>>(call->callee)) {
        return "native call has no execution protocol";
    }
    return source.value.visit([]<typename Operation>(const Operation&) static noexcept {
        return unsupported_execution_operation<Operation>();
    });
}
