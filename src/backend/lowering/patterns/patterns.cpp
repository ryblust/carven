module carven:backend.lowering.patterns.impl;

import :backend.generation.names;
import :backend.lowering.expr;
import :backend.lowering.names;
import :backend.lowering.patterns;
import :backend.lowering.program;
import :backend.lowering.types;
import :backend.target;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.stmt;
import :backend.target.type;
import :semantic.hir.decl;
import :semantic.hir.pattern;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :support.invariant;
import std;

auto combine_pattern_conditions(
    TargetUnitBuilder& builder,
    std::optional<TargetExprID> left,
    std::optional<TargetExprID> right
) noexcept -> std::optional<TargetExprID> {
    if (!left.has_value()) {
        return right;
    }
    if (!right.has_value()) {
        return left;
    }
    return builder.append_expression({
        .value = TargetBinaryExpr {
            .left = *left,
            .op = TargetBinaryOperator::LogicalAnd,
            .right = *right,
        },
    });
}

auto enumeration_for_case(const TargetCallableLowerer& context, EnumCaseID enum_case) noexcept
    -> std::pair<const HIREnumDecl*, std::size_t> {
    const auto& case_contract = context.source().enum_case(enum_case);
    const auto& enumeration = context.source().enumeration(case_contract.owner);
    const auto member = std::ranges::find(enumeration.cases, enum_case);
    if (member == enumeration.cases.end()) {
        invariant_violation("enum case pattern is absent from its owning enum contract");
    }
    return {
        &enumeration,
        static_cast<std::size_t>(std::distance(enumeration.cases.begin(), member))
    };
}

auto pattern_requires_subject(TargetCallableLowerer& context, HIRPatternID pattern_id) noexcept
    -> bool {
    const auto& value = context.source().pattern(pattern_id).value;
    if (std::holds_alternative<HIRWildcardPattern>(value)) {
        return false;
    }
    const auto* alternatives = std::get_if<HIROrPattern>(&value);
    return alternatives == nullptr
        || std::ranges::any_of(
               alternatives->alternatives,
               [&](HIRPatternID alternative_id) noexcept {
                   return pattern_requires_subject(context, alternative_id);
               }
        );
}

auto materialize_pattern_bindings(
    TargetCallableLowerer& context,
    std::span<const LoweredPatternBinding> bindings
) noexcept -> std::vector<TargetStmtID> {
    return bindings | std::views::transform([&](const auto& binding) noexcept {
               return context.target().append_lowering_statement(
                   TargetVariableStmt {
                       .binding = TargetVariableBinding::ConstValue,
                       .name = binding.name,
                       .type = binding.type,
                       .initializer = binding.initializer,
                       .maybe_unused = !symbol_is_used(context, binding.symbol),
                   }
               );
           })
        | std::ranges::to<std::vector>();
}

namespace {

class PatternSubjectOccurrences final {
public:
    PatternSubjectOccurrences(TargetCallableLowerer& context, TargetExprID prototype_id) noexcept
        : context(context),
          prototype(prototype_id),
          first(prototype_id) {}

    auto next() noexcept -> TargetExprID {
        if (first.has_value()) {
            const auto occurrence = *first;
            first.reset();
            return occurrence;
        }
        return context.target().clone_expression_occurrence(prototype);
    }

private:
    TargetCallableLowerer& context;
    TargetExprID prototype;
    std::optional<TargetExprID> first;
};

auto next_pattern_subject(PatternSubjectOccurrences* subject) noexcept -> TargetExprID {
    if (subject == nullptr) {
        invariant_violation("subject-dependent pattern has no target subject");
    }
    return subject->next();
}

auto clone_pattern_occurrences(TargetUnitBuilder& builder, const LoweredPattern& pattern) noexcept
    -> LoweredPattern {
    auto bindings = std::vector<LoweredPatternBinding>();
    bindings.reserve(pattern.bindings.size());
    for (const auto& binding : pattern.bindings) {
        bindings.push_back({
            .symbol = binding.symbol,
            .name = binding.name,
            .type = binding.type,
            .initializer = builder.clone_expression_occurrence(binding.initializer),
        });
    }
    return {
        .condition = pattern.condition.transform([&](TargetExprID condition_id) noexcept {
            return builder.clone_expression_occurrence(condition_id);
        }),
        .bindings = std::move(bindings),
    };
}

auto lower_pattern_with_subject_occurrences(
    TargetCallableLowerer& context,
    PatternSubjectOccurrences* subject,
    HIRPatternID pattern_id
) noexcept -> std::vector<LoweredPattern> {
    const auto& pattern = context.source().pattern(pattern_id);
    if (std::holds_alternative<HIRWildcardPattern>(pattern.value)) {
        return {LoweredPattern {
            .condition = std::nullopt,
            .bindings = {},
        }};
    }
    if (const auto* binding = std::get_if<HIRBindingPattern>(&pattern.value)) {
        return {
            LoweredPattern {
                .condition = std::nullopt,
                .bindings = {
                    LoweredPatternBinding {
                        .symbol = binding->target.symbol,
                        .name = symbol_identifier(context, binding->target.symbol),
                        .type = lower_type(context, binding->type),
                        .initializer = next_pattern_subject(subject),
                    },
                },
            },
        };
    }
    if (const auto* literal = std::get_if<HIRLiteralPattern>(&pattern.value)) {
        const auto value = context.target().append_expression({
            .value = TargetLiteralExpr {
                .value = lower_literal(context, literal->literal, literal->type),
            },
        });
        return {LoweredPattern {
            .condition = call_expression(
                context,
                name_expression(context, TargetSymbol::RuntimeIs),
                {next_pattern_subject(subject), value}
            ),
            .bindings = {},
        }};
    }
    if (const auto* alternatives = std::get_if<HIROrPattern>(&pattern.value)) {
        auto result = std::vector<LoweredPattern>();
        for (const auto alternative : alternatives->alternatives) {
            auto lowered = lower_pattern_with_subject_occurrences(context, subject, alternative);
            result.insert(
                result.end(),
                std::make_move_iterator(lowered.begin()),
                std::make_move_iterator(lowered.end())
            );
        }
        return result;
    }
    const auto* constraint = std::get_if<HIRTypeConstraintPattern>(&pattern.value);
    if (constraint != nullptr) {
        const auto condition = context.target().append_expression({
            .value = TargetCallExpr {
                .callee = name_expression(context, TargetSymbol::RuntimeIs),
                .template_argument_type_ids = {lower_type(context, constraint->type)},
                .arguments = {next_pattern_subject(subject)},
            },
        });
        return {LoweredPattern {.condition = condition, .bindings = {}}};
    }

    const auto& case_pattern = std::get<HIRCasePattern>(pattern.value);
    const auto [enumeration, ordinal] = enumeration_for_case(context, case_pattern.enum_case);
    const auto& case_contract = context.source().enum_case(case_pattern.enum_case);
    if (case_pattern.payload.size() != case_contract.payload_types.size()) {
        invariant_violation("enum case pattern is inconsistent with its declaration");
    }
    const auto* representation_names = enumeration->profile == HIREnumProfile::Payload
        ? std::addressof(context.payload_enum(case_contract.owner))
        : nullptr;
    const auto holds = enumeration->profile == HIREnumProfile::Numeric
        ? context.target().append_expression({
              .value =
                  TargetBinaryExpr {
                      .left = next_pattern_subject(subject),
                      .op = TargetBinaryOperator::Equal,
                      .right = name_expression(
                          context,
                          symbol_reference_name(context, case_contract.symbol)
                      ),
                  },
          })
        : member_call_expression(
              context,
              next_pattern_subject(subject),
              representation_names->cases[ordinal].holds_function
          );
    auto alternatives = std::vector<LoweredPattern> {
        LoweredPattern {.condition = holds, .bindings = {}},
    };
    for (auto index = 0uz; index < case_pattern.payload.size(); ++index) {
        const auto child_pattern = case_pattern.payload[index];
        if (!pattern_requires_subject(context, child_pattern)) {
            continue;
        }
        const auto case_payload = member_call_expression(
            context,
            next_pattern_subject(subject),
            representation_names->cases[ordinal].payload_function
        );
        const auto payload = context.target().append_expression({
            .value = TargetMemberExpr {
                .operand_id = case_payload,
                .name = TargetNameAllocator::enum_payload_field(index),
            },
        });
        auto payload_occurrences = PatternSubjectOccurrences(context, payload);
        auto children = lower_pattern_with_subject_occurrences(
            context,
            std::addressof(payload_occurrences),
            child_pattern
        );
        auto product = std::vector<LoweredPattern>();
        const auto base_count = alternatives.size();
        const auto child_count = children.size();
        product.reserve(base_count * child_count);
        for (auto base_index = 0uz; base_index < base_count; ++base_index) {
            for (auto child_index = 0uz; child_index < child_count; ++child_index) {
                auto base = child_index + 1 == child_count
                    ? std::move(alternatives[base_index])
                    : clone_pattern_occurrences(context.target(), alternatives[base_index]);
                auto child = base_index + 1 == base_count
                    ? std::move(children[child_index])
                    : clone_pattern_occurrences(context.target(), children[child_index]);
                base.bindings.insert(
                    base.bindings.end(),
                    std::make_move_iterator(child.bindings.begin()),
                    std::make_move_iterator(child.bindings.end())
                );
                product.push_back({
                    .condition = combine_pattern_conditions(
                        context.target(),
                        base.condition,
                        child.condition
                    ),
                    .bindings = std::move(base.bindings),
                });
            }
        }
        alternatives = std::move(product);
    }
    return alternatives;
}

} // namespace
auto lower_pattern(
    TargetCallableLowerer& context,
    std::optional<TargetExprID> subject_occurrence_id,
    HIRPatternID pattern_id
) noexcept -> std::vector<LoweredPattern> {
    const auto requires_subject = pattern_requires_subject(context, pattern_id);
    if (requires_subject != subject_occurrence_id.has_value()) {
        invariant_violation("target pattern subject availability does not match semantic facts");
    }
    auto occurrences = std::optional<PatternSubjectOccurrences>();
    if (subject_occurrence_id.has_value()) {
        occurrences.emplace(context, *subject_occurrence_id);
    }
    return lower_pattern_with_subject_occurrences(
        context,
        occurrences ? std::addressof(*occurrences) : nullptr,
        pattern_id
    );
}
