module carven:backend.lowering.body.pattern.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.body.lowerer;
import :backend.lowering.context;
import :backend.target.builder;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

namespace body_lowering {

auto BodyLowerer::subject_expression(const PatternSubject& subject) noexcept -> TargetExpr {
    auto result = std::optional<TargetExpr>();
    auto path_begin = 0uz;
    for (auto index = subject.payload_path.size(); index > 0uz; --index) {
        const auto& step = subject.payload_path[index - 1uz];
        if (!step.projection.has_value()) {
            continue;
        }
        result = member_expression(
            dereference_expression(name_expression(*step.projection)),
            TargetNameAllocator::enum_payload_field(step.payload_index)
        );
        path_begin = index;
        break;
    }
    if (!result.has_value()) {
        result = name_expression(subject.root);
        if (subject.dereference_root) {
            result = dereference_expression(std::move(*result));
        }
    }
    for (auto index = path_begin; index < subject.payload_path.size(); ++index) {
        const auto& step = subject.payload_path[index];
        const auto owner = context.semantic().declarations().enum_case(step.enum_case).owner;
        if (!std::holds_alternative<PayloadEnumRepresentation>(
                context.semantic().declarations().enumeration(owner).representation
            )) {
            invariant_violation("numeric enum pattern carries a payload projection");
        }
        const auto ordinal = enum_case_index(context.semantic(), step.enum_case);
        const auto& case_names = context.payload_enum(owner).cases[ordinal];
        if (step.projection.has_value()) {
            result = name_expression(*step.projection);
        } else {
            result = call_member(std::move(*result), case_names.projection_function.spelling(), {});
        }
        result = member_expression(
            dereference_expression(std::move(*result)),
            TargetNameAllocator::enum_payload_field(step.payload_index)
        );
    }
    return std::move(*result);
}

auto BodyLowerer::lower_pattern(PatternID pattern_id, PatternSubject subject) noexcept
    -> std::vector<PatternSelection> {
    const auto one = [](PatternSelection selection) noexcept {
        auto result = std::vector<PatternSelection>();
        result.push_back(std::move(selection));
        return result;
    };
    const auto& pattern = body.pattern(pattern_id);
    return std::visit(
        Overloaded {
            [&](const WildcardPattern&) noexcept { return one(PatternSelection {}); },
            [&](const LiteralPattern& value) noexcept {
                auto constraints = std::vector<PatternConstraint>();
                constraints.push_back({
                    .subject = std::move(subject),
                    .value = value.constant,
                    .projection = std::nullopt,
                });
                return one(
                    PatternSelection {
                        .constraints = std::move(constraints),
                        .bindings = {},
                        .failure_projection = std::nullopt,
                    }
                );
            },
            [&](const OrPattern& value) noexcept {
                auto result = std::vector<PatternSelection>();
                for (const auto alternative : value.alternatives) {
                    auto lowered = lower_pattern(alternative, subject);
                    result.insert(
                        result.end(),
                        std::make_move_iterator(lowered.begin()),
                        std::make_move_iterator(lowered.end())
                    );
                }
                return result;
            },
            [&](const TypeConstraintPattern& value) noexcept {
                if (value.type != pattern.type) {
                    invariant_violation("concrete type-constraint pattern changed its type");
                }
                return one(PatternSelection {});
            },
            [&](const BindingPattern& value) noexcept {
                auto bindings = std::vector<PatternBinding>();
                bindings.push_back({
                    .binding = value.binding,
                    .subject = std::move(subject),
                });
                return one(
                    PatternSelection {
                        .constraints = {},
                        .bindings = std::move(bindings),
                        .failure_projection = std::nullopt,
                    }
                );
            },
            [&](const EnumCasePattern& value) noexcept {
                const auto owner =
                    context.semantic().declarations().enum_case(value.enum_case).owner;
                const auto& declaration =
                    context.semantic().declarations().enum_case(value.enum_case);
                if (declaration.payload_types.size() != value.payload.size()) {
                    invariant_violation("enum pattern payload does not match its declaration");
                }
                auto constraints = std::vector<PatternConstraint>();
                constraints.push_back({
                    .subject = subject,
                    .value = value.enum_case,
                    .projection = std::nullopt,
                });
                auto selections =
                    one(PatternSelection {
                        .constraints = std::move(constraints),
                        .bindings = {},
                        .failure_projection = std::nullopt,
                    });
                const auto payload = std::holds_alternative<PayloadEnumRepresentation>(
                    context.semantic().declarations().enumeration(owner).representation
                );
                if (!payload && !value.payload.empty()) {
                    invariant_violation("numeric enum pattern has payload children");
                }
                for (auto index = 0uz; index < value.payload.size(); ++index) {
                    auto child_subject = subject;
                    child_subject.payload_path.push_back({
                        .enum_case = value.enum_case,
                        .payload_index = static_cast<std::uint32_t>(index),
                        .projection = std::nullopt,
                    });
                    const auto children =
                        lower_pattern(value.payload[index], std::move(child_subject));
                    auto product = std::vector<PatternSelection>();
                    product.reserve(selections.size() * children.size());
                    for (const auto& base : selections) {
                        for (const auto& child : children) {
                            auto combined = base;
                            combined.constraints.insert(
                                combined.constraints.end(),
                                child.constraints.begin(),
                                child.constraints.end()
                            );
                            combined.bindings.insert(
                                combined.bindings.end(),
                                child.bindings.begin(),
                                child.bindings.end()
                            );
                            product.push_back(std::move(combined));
                        }
                    }
                    selections = std::move(product);
                }
                return selections;
            },
        },
        pattern.value
    );
}

auto BodyLowerer::cache_pattern_projections(
    std::vector<PatternSelection>& selections,
    std::vector<PatternProjection>& projections,
    StatementSequence& destination
) noexcept -> void {
    const auto same_subject = [](const PatternSubject& left, const PatternSubject& right) noexcept {
        if (left.root != right.root
            || left.dereference_root != right.dereference_root
            || left.payload_path.size() != right.payload_path.size()) {
            return false;
        }
        for (auto index = 0uz; index < left.payload_path.size(); ++index) {
            if (left.payload_path[index].enum_case != right.payload_path[index].enum_case
                || left.payload_path[index].payload_index
                    != right.payload_path[index].payload_index) {
                return false;
            }
        }
        return true;
    };
    const auto cache_payload_step = [&](PatternSubject& candidate,
                                        const PatternSubject& owner,
                                        EnumCaseID enum_case,
                                        const TargetIdentifier& projection) noexcept {
        if (candidate.root != owner.root
            || candidate.dereference_root != owner.dereference_root
            || candidate.payload_path.size() <= owner.payload_path.size()) {
            return;
        }
        for (auto index = 0uz; index < owner.payload_path.size(); ++index) {
            if (candidate.payload_path[index].enum_case != owner.payload_path[index].enum_case
                || candidate.payload_path[index].payload_index
                    != owner.payload_path[index].payload_index) {
                return;
            }
        }
        auto& step = candidate.payload_path[owner.payload_path.size()];
        if (step.enum_case == enum_case) {
            step.projection = projection;
        }
    };

    for (auto& selection : selections) {
        for (auto& constraint : selection.constraints) {
            const auto* enum_case = std::get_if<EnumCaseID>(&constraint.value);
            if (enum_case == nullptr) {
                continue;
            }
            const auto owner = context.semantic().declarations().enum_case(*enum_case).owner;
            if (!std::holds_alternative<PayloadEnumRepresentation>(
                    context.semantic().declarations().enumeration(owner).representation
                )) {
                continue;
            }

            // A nested projection may only be evaluated after its enclosing case
            // succeeds. Keep those projections in the short-circuit condition.
            if (constraint.subject.dereference_root || !constraint.subject.payload_path.empty()) {
                continue;
            }

            const auto found = std::ranges::find_if(
                projections,
                [&](const PatternProjection& projection) noexcept {
                    return projection.enum_case == *enum_case
                        && same_subject(projection.subject, constraint.subject);
                }
            );
            const auto projection = found == projections.end()
                ? names.fresh(TargetTemporaryNameKind::PayloadProjection)
                : found->name;
            if (found == projections.end()) {
                const auto ordinal = enum_case_index(context.semantic(), *enum_case);
                destination.emit(generated_statement(
                    TargetVariableStmt {
                        .binding = TargetVariableBinding::MutableValue,
                        .maybe_unused = false,
                        .name = projection,
                        .type = context.pointer_type(
                            context.intrinsic_type(TargetSymbol::Auto, true),
                            true
                        ),
                        .initializer = call_member(
                            subject_expression(constraint.subject),
                            context.payload_enum(owner)
                                .cases[ordinal]
                                .projection_function.spelling(),
                            {}
                        ),
                    }
                ));
                projections.push_back(
                    PatternProjection {
                        .subject = constraint.subject,
                        .enum_case = *enum_case,
                        .name = projection,
                    }
                );
            }
            constraint.projection = projection;
            for (auto& related : selection.constraints) {
                cache_payload_step(related.subject, constraint.subject, *enum_case, projection);
            }
            for (auto& binding : selection.bindings) {
                cache_payload_step(binding.subject, constraint.subject, *enum_case, projection);
            }
        }
    }
}

auto BodyLowerer::pattern_condition(const PatternSelection& selection) noexcept
    -> std::optional<TargetExpr> {
    auto result = selection.failure_projection.has_value()
        ? std::optional(name_expression(*selection.failure_projection))
        : std::optional<TargetExpr>();
    for (const auto& constraint : selection.constraints) {
        auto condition = std::visit(
            Overloaded {
                [&](ConstantID constant) noexcept {
                    return binary_expression(
                        subject_expression(constraint.subject),
                        TargetBinaryOperator::Equal,
                        constant_expression(context, constant)
                    );
                },
                [&](EnumCaseID enum_case) noexcept {
                    const auto owner = context.semantic().declarations().enum_case(enum_case).owner;
                    if (std::holds_alternative<PayloadEnumRepresentation>(
                            context.semantic().declarations().enumeration(owner).representation
                        )) {
                        if (constraint.projection.has_value()) {
                            return name_expression(*constraint.projection);
                        }
                        const auto ordinal = enum_case_index(context.semantic(), enum_case);
                        return call_member(
                            subject_expression(constraint.subject),
                            context.payload_enum(owner)
                                .cases[ordinal]
                                .projection_function.spelling(),
                            {}
                        );
                    }
                    return binary_expression(
                        subject_expression(constraint.subject),
                        TargetBinaryOperator::Equal,
                        enum_case_expression(context, enum_case, {})
                    );
                },
            },
            constraint.value
        );
        result = result.has_value() ? std::optional<TargetExpr> {binary_expression(
                                          std::move(*result),
                                          TargetBinaryOperator::LogicalAnd,
                                          std::move(condition)
                                      )}
                                    : std::optional<TargetExpr> {std::move(condition)};
    }
    return result;
}

auto BodyLowerer::pattern_binding_expression(
    const PatternSelection& selection,
    LocalBindingID binding
) noexcept -> TargetExpr {
    const auto found = std::ranges::find(selection.bindings, binding, &PatternBinding::binding);
    if (found == selection.bindings.end()) {
        invariant_violation("pattern successor requests an unbound value");
    }
    if (std::ranges::find(
            std::next(found),
            selection.bindings.end(),
            binding,
            &PatternBinding::binding
        )
        != selection.bindings.end()) {
        invariant_violation("pattern selection binds the same value more than once");
    }
    return subject_expression(found->subject);
}

} // namespace body_lowering
