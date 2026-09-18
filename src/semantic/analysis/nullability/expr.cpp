module carven:semantic.analysis.nullability.expr.impl;

import :semantic.analysis.nullability.context;
import :semantic.semir.initialization;
import :support.visit;
import std;

auto NullabilityBodyAnalyzer::condition(const SemanticExpression& source, NullState state) noexcept
    -> ContinuationTask<NullCondition> {
    if (const auto* unary = std::get_if<SemUnary>(&source.value);
        unary != nullptr && unary->operation == UnaryOperator::LogicalNot) {
        auto result = (co_await condition(*unary->operand, std::move(state)));
        std::swap(result.yes, result.no);
        co_return result;
    }
    if (const auto* logical = std::get_if<SemShortCircuit>(&source.value)) {
        auto left = (co_await condition(*logical->left, std::move(state)));
        auto& selected = logical->operation == ShortCircuitOperator::And ? left.yes : left.no;
        auto result = NullCondition {.yes = {}, .no = {}, .exits = std::move(left.exits)};
        if (selected) {
            auto right = (co_await condition(*logical->right, std::move(selected->state)));
            result.yes = std::move(right.yes);
            result.no = std::move(right.no);
            append_null_exits(result.exits, std::move(right.exits));
        }
        if (logical->operation == ShortCircuitOperator::And) {
            join_null_normal(result.no, left.no);
        } else {
            join_null_normal(result.yes, left.yes);
        }
        co_return result;
    }
    auto evaluated = (co_await expression(source, std::move(state)));
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
    co_return result;
}

auto NullabilityBodyAnalyzer::expression(const SemanticExpression& source, NullState state) noexcept
    -> ContinuationTask<NullFlow> {
    auto flow =
        NullFlow {.normal = NullNormal {.state = std::move(state), .value = {}}, .exits = {}};
    const auto evaluate =
        [&](const SemanticExpression& child) noexcept -> ContinuationTask<NullValue> {
        if (!flow.normal) {
            co_return {};
        }
        auto next = (co_await expression(child, std::move(flow.normal->state)));
        append_null_exits(flow.exits, std::move(next.exits));
        flow.normal = std::move(next.normal);
        co_return flow.normal ? flow.normal->value : NullValue();
    };
    const auto set_value = [&](NullValue value) noexcept {
        if (flow.normal) {
            flow.normal->value = std::move(value);
        }
    };
    const auto aggregate = [&](const SemanticExpression& child,
                               std::uint64_t index,
                               NullValue& target) noexcept -> ContinuationTask<std::monostate> {
        const auto facts = (co_await evaluate(child));
        for (const auto& [path, fact] : facts) {
            auto nested = NullPath {index};
            nested.append_range(path);
            target.emplace(std::move(nested), fact);
        }
        co_return {};
    };
    const auto external = [&](const auto& value) noexcept -> ContinuationTask<std::monostate> {
        auto operands = std::vector<const SemanticExpression*>();
        visit_cpp_operands(value, [&](AccessMode, const SemanticExpression& operand) noexcept {
            operands.push_back(std::addressof(operand));
        });
        for (const auto* operand : operands) {
            static_cast<void>((co_await evaluate(*operand)));
        }
        if (!flow.normal) {
            co_return {};
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
        co_return {};
    };
    (co_await source.value.visit(
        Overloaded {
            [&](const SemDefault&) noexcept -> ContinuationTask<std::monostate> {
                if (default_initialization(program, source.type.resolved())
                    == DefaultInitialization::Native) {
                    invalidate_exposed(flow.normal->state);
                }
                if (std::holds_alternative<PointerTypeValue>(
                        program.types().type(source.type.resolved()).value
                    )) {
                    set_value({{{}, NullFact::Null}});
                }
                co_return {};
            },
            [&](const SemConstant&) noexcept -> ContinuationTask<std::monostate> {
                set_value(constant_value(source));
                co_return {};
            },
            [&](const SemBinding& value) noexcept -> ContinuationTask<std::monostate> {
                set_value(value_at(flow.normal->state, {.root = value.binding, .path = {}}));
                co_return {};
            },
            [](const SemCallable&) static noexcept -> ContinuationTask<std::monostate> {
                co_return {};
            },
            [](const SemEnumConstructor&) static noexcept -> ContinuationTask<std::monostate> {
                co_return {};
            },
            [&](const SemRange& value) noexcept -> ContinuationTask<std::monostate> {
                static_cast<void>((co_await evaluate(*value.begin)));
                static_cast<void>((co_await evaluate(*value.end)));
                co_return {};
            },
            [&](const SemArray& value) noexcept -> ContinuationTask<std::monostate> {
                auto result = NullValue();
                for (const auto [index, element] : std::views::enumerate(value.elements)) {
                    (co_await aggregate(element, index, result));
                }
                set_value(std::move(result));
                co_return {};
            },
            [&](const SemStruct& value) noexcept -> ContinuationTask<std::monostate> {
                auto result = NullValue();
                for (const auto& field : value.fields) {
                    (co_await aggregate(field.value, field.declaration_index, result));
                }
                set_value(std::move(result));
                co_return {};
            },
            [&](const SemEnumCase& value) noexcept -> ContinuationTask<std::monostate> {
                auto result = NullValue();
                for (const auto [index, element] : std::views::enumerate(value.payload)) {
                    (co_await aggregate(element, index, result));
                }
                set_value(std::move(result));
                co_return {};
            },
            [&](const SemArrayAdopt& value) noexcept -> ContinuationTask<std::monostate> {
                set_value((co_await evaluate(*value.source)));
                co_return {};
            },
            [&](const SemCast& value) noexcept -> ContinuationTask<std::monostate> {
                set_value((co_await evaluate(*value.operand)));
                co_return {};
            },
            [&](const SemBorrowCallable& value) noexcept -> ContinuationTask<std::monostate> {
                static_cast<void>((co_await evaluate(*value.source)));
                set_value({});
                co_return {};
            },
            [&](const SemPropagate& value) noexcept -> ContinuationTask<std::monostate> {
                set_value((co_await evaluate(*value.operand)));
                co_return {};
            },
            [&](const SemUnary& value) noexcept -> ContinuationTask<std::monostate> {
                static_cast<void>((co_await evaluate(*value.operand)));
                set_value({});
                co_return {};
            },
            [&](const SemBinary& value) noexcept -> ContinuationTask<std::monostate> {
                static_cast<void>((co_await evaluate(*value.left)));
                static_cast<void>((co_await evaluate(*value.right)));
                set_value({});
                co_return {};
            },
            [&](const SemShortCircuit&) noexcept -> ContinuationTask<std::monostate> {
                auto branches = (co_await condition(source, std::move(flow.normal->state)));
                flow.normal = std::move(branches.yes);
                join_null_normal(flow.normal, branches.no);
                append_null_exits(flow.exits, std::move(branches.exits));
                set_value({});
                co_return {};
            },
            [&](const SemDereference& value) noexcept -> ContinuationTask<std::monostate> {
                const auto pointer = (co_await evaluate(*value.source));
                if (flow.normal) {
                    require_nonnull(value.origin, pointer);
                }
                set_value({});
                co_return {};
            },
            [&](const SemField& value) noexcept -> ContinuationTask<std::monostate> {
                const auto target = (co_await evaluate(*value.source));
                if (!flow.normal) {
                    co_return {};
                }
                if (const auto place = location(source)) {
                    set_value(value_at(flow.normal->state, *place));
                } else {
                    set_value(project_null_value(target, value.field.field_index));
                }
                co_return {};
            },
            [&](const SemIndex& value) noexcept -> ContinuationTask<std::monostate> {
                const auto target = (co_await evaluate(*value.source));
                static_cast<void>((co_await evaluate(*value.index)));
                if (!flow.normal) {
                    co_return {};
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
                co_return {};
            },
            [&](const SemTestReport& value) noexcept -> ContinuationTask<std::monostate> {
                if (value.condition) {
                    static_cast<void>((co_await evaluate(**value.condition)));
                }
                if (value.message) {
                    static_cast<void>((co_await evaluate(**value.message)));
                }
                if (flow.normal && value.kind == TestReportKind::Fail) {
                    flow.exits.push_back({NullTransfer::Return, std::move(flow.normal->state)});
                    flow.normal.reset();
                }
                co_return {};
            },
            [&](const SemPrint& value) noexcept -> ContinuationTask<std::monostate> {
                for (const auto& operand : value.operands) {
                    static_cast<void>((co_await evaluate(operand.expression)));
                }
                set_value({});
                co_return {};
            },
            [&](const SemFormat& value) noexcept -> ContinuationTask<std::monostate> {
                if (value.receiver) {
                    static_cast<void>((co_await evaluate(**value.receiver)));
                }
                for (const auto& operand : value.operands) {
                    static_cast<void>((co_await evaluate(operand.expression)));
                }
                set_value({});
                co_return {};
            },
            [&](const SemSliceIntrinsic& value) noexcept -> ContinuationTask<std::monostate> {
                for (const auto& operand : value.operands) {
                    static_cast<void>((co_await evaluate(operand.expression)));
                }
                set_value({});
                co_return {};
            },
            [&](const SemTextIntrinsic& value) noexcept -> ContinuationTask<std::monostate> {
                for (const auto& operand : value.operands) {
                    static_cast<void>((co_await evaluate(operand.expression)));
                }
                set_value({});
                co_return {};
            },
            [&](const SemTake& value) noexcept -> ContinuationTask<std::monostate> {
                auto taken = (co_await evaluate(*value.place));
                if (flow.normal) {
                    invalidate(flow.normal->state, location(*value.place, true));
                }
                set_value(std::move(taken));
                co_return {};
            },
            [&](const SemClosure& value) noexcept -> ContinuationTask<std::monostate> {
                for (const auto& capture : value.captures) {
                    static_cast<void>((co_await evaluate(capture.expression)));
                    if (flow.normal && capture.mode == CaptureMode::Write) {
                        expose(flow.normal->state, capture.expression);
                    }
                }
                set_value({});
                co_return {};
            },
            [&](const SemCpp& value) noexcept -> ContinuationTask<std::monostate> {
                (co_await external(value));
                co_return {};
            },
            [&](const SemCppCall& value) noexcept -> ContinuationTask<std::monostate> {
                (co_await external(value));
                co_return {};
            },
            [&](const SemCall& value) noexcept -> ContinuationTask<std::monostate> {
                static_cast<void>((co_await evaluate(*value.callee)));
                for (const auto& argument : value.arguments) {
                    static_cast<void>((co_await evaluate(argument.expression)));
                }
                if (!flow.normal) {
                    co_return {};
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
                co_return {};
            },
            [&](const SemIf& value) noexcept -> ContinuationTask<std::monostate> {
                flow = (co_await conditional(value, std::move(flow.normal->state)));
                co_return {};
            },
            [&](const SemMatch& value) noexcept -> ContinuationTask<std::monostate> {
                flow = (co_await match(value, std::move(flow.normal->state)));
                co_return {};
            },
            [&](const SemTry& value) noexcept -> ContinuationTask<std::monostate> {
                flow = (co_await attempt(value, std::move(flow.normal->state)));
                co_return {};
            },
        }
    ));
    co_return flow;
}
