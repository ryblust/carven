module carven:semantic.analysis.validation.control.impl;

import :semantic.analysis.operations;
import :semantic.analysis.validation.context;
import :semantic.analysis.validation;
import :semantic.semir.constant;
import :semantic.semir.program;
import :semantic.semir.traversal;
import :support.invariant;
import :support.visit;
import std;

auto BodyContractVerifier::verify_region(const SemanticRegion& source) const noexcept -> void {
    const auto callable = body_callable();
    const auto result = callable.has_value()
        ? std::optional(program.callable_signatures()
                            .signature(program.declarations().callable(*callable).signature)
                            .result)
        : std::nullopt;
    const auto check_return = [&](const std::optional<SemanticExpression>& value) noexcept {
        const auto is_void = !result.has_value()
            || require_type(*result).value
                == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Void}};
        const auto operand_matches = !value.has_value()
            || (result.has_value() ? value->type.resolved() == *result
                                   : require_type(value->type.resolved()).value
                        == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Void}});
        if (!operand_matches || (!value.has_value() && !is_void)) {
            invariant_violation("return differs from callable contract");
        }
    };
    visit_semantic_nodes(
        source,
        Overloaded {
            [&](const SemanticExpression& expression) noexcept { verify_expression(expression); },
            [&](const SemanticStatement& statement) noexcept {
                require_origin(statement.origin);
                if (!body.lifetime_regions().contains(statement.lifetime)) {
                    invariant_violation("statement has foreign lifetime");
                }
                std::visit(
                    Overloaded {
                        [&](const SemReturn& value) noexcept { check_return(value.value); },
                        [&](const SemInitialize& value) noexcept {
                            if (body.binding(value.binding).type
                                != value.initializer.type.resolved()) {
                                invariant_violation("initializer differs from binding type");
                            }
                        },
                        [&](const SemAssign& value) noexcept {
                            const auto external =
                                std::holds_alternative<CppTypeValue>(
                                    require_type(value.target.type.resolved()).value
                                )
                                || std::holds_alternative<CppTypeValue>(
                                    require_type(value.value.type.resolved()).value
                                );
                            if ((!external
                                 && value.target.type.resolved() != value.value.type.resolved())
                                || value.target.category != SemanticValueCategory::Place) {
                                invariant_violation("invalid assignment contract");
                            }
                        },
                        [&](const SemThrow& value) noexcept {
                            require_nominal_failure_member(value.failure_type);
                            if (value.value.type.resolved() != value.failure_type) {
                                invariant_violation("throw payload type mismatch");
                            }
                        },
                        [](const auto&) static noexcept {},
                    },
                    statement.value
                );
            },
        }
    );
    if (source.result.has_value()) {
        check_return(source.result);
    }
}

auto BodyContractVerifier::verify() noexcept -> void {
    require_top_level_owners();
    verify_lifetimes();
    verify_rows();
    verify_computations();
    verify_region(body.region());
    if (!require_failure_set(body.region().failures.resolved()).members.empty()) {
        require_body_failure_set(body.region().failures.resolved());
    }
}

BodyContractVerifier::BodyContractVerifier(
    const SemIRBody& source,
    const SemIRProgram& semantic
) noexcept
    : body(source),
      program(semantic) {}
