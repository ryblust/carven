module carven:semantic.analysis.validation.control.impl;
import :semantic.analysis.validation.context;
import std;

namespace validation_detail {
auto BodyContractVerifier::verify_region(const SemIRRegion& source) const noexcept -> void {
    const auto callable = body_callable();
    const auto result = callable.has_value()
        ? std::optional(
              draft->concrete_type(draft->construction_callable_contract_copy(*callable).result)
          )
        : std::nullopt;
    const auto check_return = [&](const std::optional<SemIRExpression>& value) noexcept {
        const auto is_void = !result.has_value()
            || require_type(*result).value
                == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Void}};
        if ((value.has_value() && (!result.has_value() || value->type != *result))
            || (!value.has_value() && !is_void)) {
            invariant_violation("return differs from callable contract");
        }
    };
    visit_semantic_nodes(
        source,
        Overloaded {
            [&](const SemIRExpression& expression) noexcept { verify_expression(expression); },
            [&](const SemIRStatement& statement) noexcept {
                require_origin(statement.origin);
                if (!body.lifetime_regions().contains(statement.lifetime)) {
                    invariant_violation("statement has foreign lifetime");
                }
                std::visit(
                    Overloaded {
                        [&](const SemReturn<TypeID, FailureSetID>& value) noexcept {
                            check_return(value.value);
                        },
                        [&](const SemInitialize<TypeID, FailureSetID>& value) noexcept {
                            if (body.binding(value.binding).type != value.initializer.type) {
                                invariant_violation("initializer differs from binding type");
                            }
                        },
                        [&](const SemAssign<TypeID, FailureSetID>& value) noexcept {
                            const auto external = std::holds_alternative<CppTypeValue>(
                                                      require_type(value.target.type).value
                                                  )
                                || std::holds_alternative<CppTypeValue>(
                                                      require_type(value.value.type).value
                                );
                            if ((!external && value.target.type != value.value.type)
                                || value.target.category != SemanticValueCategory::Place) {
                                invariant_violation("invalid assignment contract");
                            }
                        },
                        [&](const SemThrow<TypeID, FailureSetID>& value) noexcept {
                            require_nominal_failure_member(value.failure_type);
                            if (value.value.type != value.failure_type) {
                                invariant_violation("throw payload type mismatch");
                            }
                        },
                        [&](const SemTestReport<TypeID, FailureSetID>&) noexcept {
                            if (body.kind() != BodyKind::Test) {
                                invariant_violation("ordinary body contains a test exit");
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
    verify_trees();
    verify_rows();
    verify_computations();
    verify_region(body.region());
    if (!require_failure_set(body.region().failures).members.empty()) {
        require_body_failure_set(body.region().failures);
    }
}
} // namespace validation_detail
