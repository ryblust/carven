module carven:semantic.analysis.nullability.expr.impl;

import :semantic.analysis.nullability.context;
import :support.visit;
import std;

auto NullabilityBodyAnalyzer::condition(const SemanticExpression& source, NullState state) noexcept
    -> NullCondition {
    if (const auto* unary = std::get_if<SemUnary>(&source.value);
        unary != nullptr && unary->operation == UnaryOperator::LogicalNot) {
        auto result = condition(*unary->operand, std::move(state));
        std::swap(result.yes, result.no);
        return result;
    }
    if (const auto* logical = std::get_if<SemShortCircuit>(&source.value)) {
        auto left = condition(*logical->left, std::move(state));
        auto& selected = logical->operation == ShortCircuitOperator::And ? left.yes : left.no;
        auto result = NullCondition {.yes = {}, .no = {}, .exits = std::move(left.exits)};
        if (selected) {
            auto right = condition(*logical->right, std::move(selected->state));
            result.yes = std::move(right.yes);
            result.no = std::move(right.no);
            append_null_exits(result.exits, std::move(right.exits));
        }
        if (logical->operation == ShortCircuitOperator::And) {
            join_null_normal(result.no, left.no);
        } else {
            join_null_normal(result.yes, left.yes);
        }
        return result;
    }
    auto evaluated = expression(source, std::move(state));
    auto result = NullCondition {
        .yes = evaluated.normal,
        .no = std::move(evaluated.normal),
        .exits = std::move(evaluated.exits)
    };
    if (truth(source) == true) {
        result.no.reset();
    }
    if (truth(source) == false) {
        result.yes.reset();
    }
    if (const auto* comparison = std::get_if<SemBinary>(&source.value); comparison != nullptr
        && (comparison->operation == BinaryOperator::Equal
            || comparison->operation == BinaryOperator::NotEqual)) {
        // Only a literal/folded constant operation is side-effect free. A known
        // null result of an arbitrary operation may have changed the other slot.
        const auto is_null = [&](const SemanticExpression& value) noexcept {
            return std::holds_alternative<SemConstant>(value.value)
                && !constant_value(value).empty();
        };
        auto place = std::optional<NullPlace>();
        if (is_null(*comparison->right)) {
            place = location(*comparison->left);
        } else if (is_null(*comparison->left)) {
            place = location(*comparison->right);
        }
        if (place) {
            const auto equal = comparison->operation == BinaryOperator::Equal;
            refine(result.yes, *place, equal ? NullFact::Null : NullFact::NonNull);
            refine(result.no, *place, equal ? NullFact::NonNull : NullFact::Null);
        }
    }
    return result;
}

auto NullabilityBodyAnalyzer::expression(const SemanticExpression& source, NullState state) noexcept
    -> NullFlow {
    auto flow =
        NullFlow {.normal = NullNormal {.state = std::move(state), .value = {}}, .exits = {}};
    const auto evaluate = [&](const SemanticExpression& child) noexcept -> NullValue {
        if (!flow.normal) {
            return {};
        }
        auto next = expression(child, std::move(flow.normal->state));
        append_null_exits(flow.exits, std::move(next.exits));
        flow.normal = std::move(next.normal);
        return flow.normal ? flow.normal->value : NullValue();
    };
    const auto set_value = [&](NullValue value) noexcept {
        if (flow.normal) {
            flow.normal->value = std::move(value);
        }
    };
    const auto aggregate =
        [&](const SemanticExpression& child, std::uint64_t index, NullValue& target) noexcept {
            const auto facts = evaluate(child);
            for (const auto& [path, fact] : facts) {
                auto nested = NullPath {index};
                nested.append_range(path);
                target.emplace(std::move(nested), fact);
            }
        };
    const auto external = [&](const auto& value) noexcept {
        visit_cpp_operands(value, [&](AccessMode, const SemanticExpression& operand) noexcept {
            static_cast<void>(evaluate(operand));
        });
        if (!flow.normal) {
            return;
        }
        visit_cpp_operands(
            value,
            [&](AccessMode access, const SemanticExpression& operand) noexcept {
                if (access == AccessMode::Write) {
                    expose(flow.normal->state, operand);
                    invalidate(flow.normal->state, location(operand, true));
                }
            }
        );
        invalidate_exposed(flow.normal->state);
        flow.normal->value = {};
    };
    std::visit(
        Overloaded {
            [&](const SemConstant&) noexcept { set_value(constant_value(source)); },
            [&](const SemBinding& value) noexcept {
                set_value(value_at(flow.normal->state, {.root = value.binding, .path = {}}));
            },
            [](const SemCallable&) static noexcept {},
            [](const SemEnumConstructor&) static noexcept {},
            [&](const SemArray& value) noexcept {
                auto result = NullValue();
                for (const auto [index, element] : std::views::enumerate(value.elements)) {
                    aggregate(element, index, result);
                }
                set_value(std::move(result));
            },
            [&](const SemStruct& value) noexcept {
                auto result = NullValue();
                for (const auto& field : value.fields) {
                    aggregate(field.value, field.declaration_index, result);
                }
                set_value(std::move(result));
            },
            [&](const SemEnumCase& value) noexcept {
                auto result = NullValue();
                for (const auto [index, element] : std::views::enumerate(value.payload)) {
                    aggregate(element, index, result);
                }
                set_value(std::move(result));
            },
            [&](const SemArrayAdopt& value) noexcept { set_value(evaluate(*value.source)); },
            [&](const SemCast& value) noexcept { set_value(evaluate(*value.operand)); },
            [&](const SemBorrowCallable& value) noexcept {
                static_cast<void>(evaluate(*value.source));
                set_value({});
            },
            [&](const SemPropagate& value) noexcept { set_value(evaluate(*value.operand)); },
            [&](const SemUnary& value) noexcept {
                static_cast<void>(evaluate(*value.operand));
                set_value({});
            },
            [&](const SemBinary& value) noexcept {
                static_cast<void>(evaluate(*value.left));
                static_cast<void>(evaluate(*value.right));
                set_value({});
            },
            [&](const SemShortCircuit&) noexcept {
                auto branches = condition(source, std::move(flow.normal->state));
                flow.normal = std::move(branches.yes);
                join_null_normal(flow.normal, branches.no);
                append_null_exits(flow.exits, std::move(branches.exits));
                set_value({});
            },
            [&](const SemDereference& value) noexcept {
                const auto pointer = evaluate(*value.source);
                if (flow.normal) {
                    require_nonnull(value.origin, pointer);
                }
                set_value({});
            },
            [&](const SemField& value) noexcept {
                const auto target = evaluate(*value.source);
                if (!flow.normal) {
                    return;
                }
                if (const auto place = location(source)) {
                    set_value(value_at(flow.normal->state, *place));
                } else {
                    set_value(project_null_value(target, value.field.field_index));
                }
            },
            [&](const SemIndex& value) noexcept {
                const auto target = evaluate(*value.source);
                static_cast<void>(evaluate(*value.index));
                if (!flow.normal) {
                    return;
                }
                if (const auto place = location(source)) {
                    set_value(value_at(flow.normal->state, *place));
                } else if (value.index->constant
                           && value.source->category == SemanticValueCategory::Value) {
                    const auto* integer = std::get_if<IntegerConstant>(
                        &program.constants().constant(*value.index->constant).value
                    );
                    set_value(
                        integer != nullptr && integer->as_unsigned()
                            ? project_null_value(target, *integer->as_unsigned())
                            : NullValue()
                    );
                } else {
                    set_value({});
                }
            },
            [&](const SemFormat& value) noexcept {
                for (const auto& operand : value.operands) {
                    static_cast<void>(evaluate(operand.expression));
                }
                set_value({});
            },
            [&](const SemTextIntrinsic& value) noexcept {
                for (const auto& operand : value.operands) {
                    static_cast<void>(evaluate(operand.expression));
                }
                set_value({});
            },
            [&](const SemTake& value) noexcept {
                auto taken = evaluate(*value.place);
                if (flow.normal) {
                    invalidate(flow.normal->state, location(*value.place, true));
                }
                set_value(std::move(taken));
            },
            [&](const SemClosure& value) noexcept {
                for (const auto& capture : value.captures) {
                    static_cast<void>(evaluate(capture.expression));
                    if (flow.normal && capture.mode == CaptureMode::Write) {
                        expose(flow.normal->state, capture.expression);
                    }
                }
                set_value({});
            },
            [&](const SemCpp& value) noexcept { external(value); },
            [&](const SemCppCall& value) noexcept { external(value); },
            [&](const SemCall& value) noexcept {
                static_cast<void>(evaluate(*value.callee));
                for (const auto& argument : value.arguments) {
                    static_cast<void>(evaluate(argument.expression));
                }
                if (!flow.normal) {
                    return;
                }
                for (const auto& argument : value.arguments) {
                    if (argument.access == AccessMode::Write) {
                        expose(flow.normal->state, argument.expression);
                        invalidate(flow.normal->state, location(argument.expression, true));
                    }
                }
                invalidate_exposed(flow.normal->state);
                flow.normal->value = {};
                failures(flow, value.callee_failures.resolved());
            },
            [&](const SemIf& value) noexcept {
                flow = conditional(value, std::move(flow.normal->state));
            },
            [&](const SemMatch& value) noexcept {
                flow = match(value, std::move(flow.normal->state));
            },
            [&](const SemTry& value) noexcept {
                flow = attempt(value, std::move(flow.normal->state));
            },
        },
        source.value
    );
    return flow;
}
