module carven:semantic.analysis.validation.control.impl;

import :semantic.analysis.operations;
import :semantic.analysis.validation;
import :semantic.analysis.validation.context;
import :semantic.semir.constant;
import :semantic.semir.program;
import :semantic.semir.traversal;
import :support.invariant;
import :support.visit;
import std;

auto BodyContractVerifier::verify_region(
    const SemanticRegion& source,
    bool executable
) const noexcept -> void {
    const auto rewritten = executable
        && (body.specialized() || std::addressof(source) != std::addressof(body.region()));
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
            [&](const SemanticExpression& expression) noexcept {
                verify_expression(expression, rewritten);
                if (executable) {
                    if (const auto* call = std::get_if<SemCall>(&expression.value);
                        call && call->target) {
                        if (program.callable_signatures()
                                .signature(program.declarations().callable(*call->target).signature)
                                .has_static_parameters()) {
                            invariant_violation("realized body retains an unresolved static call");
                        }
                    }
                    if (const auto* conditional = std::get_if<SemIf>(&expression.value);
                        conditional && conditional->is_static) {
                        invariant_violation("realized body retains a static conditional");
                    }
                }
            },
            [&](const SemanticStatement& statement) noexcept {
                require_origin(statement.origin);
                if (!body.lifetime_regions().contains(statement.lifetime)) {
                    invariant_violation("statement has foreign lifetime");
                }
                statement.value.visit(
                    Overloaded {
                        [&](const SemReturn& value) noexcept { check_return(value.value); },
                        [&](const SemInitialize& value) noexcept {
                            if (body.binding(value.binding).type
                                != value.initializer.type.resolved()) {
                                invariant_violation("initializer differs from binding type");
                            }
                        },
                        [&](const SemDecompose& value) noexcept {
                            const auto& owner = body.binding(value.owner);
                            const auto* parameter =
                                std::get_if<ParameterBindingStorage>(&owner.storage);
                            if (value.initializer
                                    ? (owner.type != value.initializer->type.resolved()
                                       || !std::holds_alternative<OwnerBindingStorage>(
                                           owner.storage
                                       ))
                                    : (!parameter || parameter->access != AccessMode::Take)) {
                                invariant_violation(
                                    "decomposition owner differs from its initializer"
                                );
                            }
                            auto seen = std::flat_set<LocalBindingID>();
                            seen.insert(value.owner);
                            auto paths = std::vector<std::span<const std::uint32_t>>();
                            for (const auto& binding : value.bindings) {
                                if (body.binding(binding.binding).lifetime != owner.lifetime) {
                                    invariant_violation(
                                        "decomposition binding outlives its source owner"
                                    );
                                }
                                for (const auto path : paths) {
                                    const auto prefix = std::min(path.size(), binding.path.size());
                                    if (std::ranges::equal(
                                            path.first(prefix),
                                            std::span(binding.path).first(prefix)
                                        )) {
                                        invariant_violation("decomposition binding paths overlap");
                                    }
                                }
                                paths.emplace_back(binding.path);
                                auto selected = owner.type;
                                for (const auto index : binding.path) {
                                    const auto* tuple =
                                        std::get_if<TupleTypeValue>(&require_type(selected).value);
                                    if (tuple == nullptr || index >= tuple->elements.size()) {
                                        invariant_violation(
                                            "decomposition path exceeds tuple shape"
                                        );
                                    }
                                    selected = tuple->elements[index];
                                }
                                if (!seen.insert(binding.binding).second
                                    || body.binding(binding.binding).type != selected) {
                                    invariant_violation(
                                        "decomposition binding differs from tuple element"
                                    );
                                }
                            }
                        },
                        [&](const SemAsyncLet& value) noexcept {
                            const auto& binding = body.binding(value.child);
                            if (!std::holds_alternative<AsyncChildBindingStorage>(binding.storage)
                                || binding.type != value.initializer.type.resolved()) {
                                invariant_violation("async child initializer differs from binding");
                            }
                        },
                        [&](const SemStaticBinding& value) noexcept {
                            const auto initializer_type = value.initializer->type.resolved();
                            const auto frozen_text = require_type(initializer_type).value
                                    == CanonicalTypeValue {BuiltinTypeValue {
                                        .kind = BuiltinType::String
                                    }}
                                && require_type(body.binding(value.binding).type).value
                                    == CanonicalTypeValue {
                                        BuiltinTypeValue {.kind = BuiltinType::Str}
                                    };
                            if (!frozen_text
                                && body.binding(value.binding).type != initializer_type) {
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
                            auto compatible =
                                value.target.type.resolved() == value.value.type.resolved();
                            if (!external && value.compound) {
                                const auto decision = decide_binary_operator(
                                    program.types(),
                                    *value.compound,
                                    value.target.type.resolved(),
                                    value.value.type.resolved(),
                                    compatible,
                                    true
                                );
                                compatible = decision
                                    && (*decision == OperatorResult::Operand
                                        || require_type(value.target.type.resolved()).value
                                            == CanonicalTypeValue {BuiltinTypeValue {
                                                *operator_result_builtin(*decision)
                                            }});
                            }
                            if ((!external && !compatible)
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
                    }
                );
                if (executable) {
                    if (std::holds_alternative<SemStaticBinding>(statement.value)) {
                        invariant_violation("realized body retains a static local");
                    }
                    if (std::holds_alternative<SemConstBlock>(statement.value)) {
                        invariant_violation("realized body retains a const block");
                    }
                    if (const auto* loop = std::get_if<SemRangeLoop>(&statement.value);
                        loop && loop->is_static) {
                        invariant_violation("realized body retains a static loop");
                    }
                }
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
    const auto executes = program.executes(body.id());
    const auto& realized = body.realized_region();
    // An unchanged region is checked once with its executable constraints.
    if (!body.specialized()
        && (!executes || std::addressof(body.region()) != std::addressof(realized))) {
        verify_region(body.region());
    }
    if (executes) {
        verify_region(realized, true);
    }
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
