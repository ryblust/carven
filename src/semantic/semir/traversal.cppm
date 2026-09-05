module carven:semantic.semir.traversal;

import :semantic.semir.structured;
import :support.visit;
import std;

// Visits stored expressions and statements, including constant-inactive source. Execution
// selection belongs to the analyses and lowering that consume semantic facts.
template<typename Type, typename Failures, typename Visitor>
auto visit_semantic_nodes(const SemanticRegion<Type, Failures>& region, Visitor visitor) noexcept
    -> void;

template<typename Type, typename Failures, typename Visitor>
auto visit_semantic_nodes(
    const SemanticExpression<Type, Failures>& expression,
    Visitor visitor
) noexcept -> void {
    if constexpr (std::invocable<Visitor&, const SemanticExpression<Type, Failures>&>) {
        std::invoke(visitor, expression);
    }
    const auto child = [&](const auto& value) noexcept {
        visit_semantic_nodes(value, visitor);
    };
    std::visit(
        Overloaded {
            [](const SemLiteral&) static noexcept {},
            [](const SemConstant&) static noexcept {},
            [](const SemBinding&) static noexcept {},
            [](const SemCallable&) static noexcept {},
            [](const SemEnumConstructor&) static noexcept {},
            [&](const SemSequence<Type, Failures>& value) noexcept {
                for (const auto& expression : value.expressions) {
                    child(expression);
                }
            },
            [&](const SemArray<Type, Failures>& value) noexcept {
                for (const auto& element : value.elements) {
                    child(element);
                }
            },
            [&](const SemArrayAdopt<Type, Failures>& value) noexcept { child(*value.source); },
            [&](const SemStruct<Type, Failures>& value) noexcept {
                for (const auto& field : value.fields) {
                    child(field.value);
                }
            },
            [&](const SemEnumCase<Type, Failures>& value) noexcept {
                for (const auto& element : value.payload) {
                    child(element);
                }
            },
            [&](const SemUnary<Type, Failures>& value) noexcept { child(*value.operand); },
            [&](const SemBinary<Type, Failures>& value) noexcept {
                child(*value.left);
                child(*value.right);
            },
            [&](const SemShortCircuit<Type, Failures>& value) noexcept {
                child(*value.left);
                child(*value.right);
            },
            [&](const SemCast<Type, Failures>& value) noexcept { child(*value.operand); },
            [&](const SemField<Type, Failures>& value) noexcept { child(*value.source); },
            [&](const SemIndex<Type, Failures>& value) noexcept {
                child(*value.source);
                child(*value.index);
            },
            [&](const SemTextIntrinsic<Type, Failures>& value) noexcept { child(*value.source); },
            [&](const SemCall<Type, Failures>& value) noexcept {
                child(*value.callee);
                for (const auto& argument : value.arguments) {
                    child(argument.expression);
                }
            },
            [&](const SemClosure<Type, Failures>& value) noexcept {
                for (const auto& capture : value.captures) {
                    child(capture.expression);
                }
            },
            [&](const SemBorrowCallable<Type, Failures>& value) noexcept { child(*value.source); },
            [&](const SemTake<Type, Failures>& value) noexcept { child(*value.place); },
            [&](const SemPropagate<Type, Failures>& value) noexcept { child(*value.operand); },
            [&](const SemIf<Type, Failures>& value) noexcept {
                for (const auto& branch : value.branches) {
                    child(branch.condition);
                    child(branch.body);
                }
                if (value.otherwise.has_value()) {
                    child(**value.otherwise);
                }
            },
            [&](const SemMatch<Type, Failures>& value) noexcept {
                child(*value.subject);
                for (const auto& arm : value.arms) {
                    if (arm.guard.has_value()) {
                        child(*arm.guard);
                    }
                    child(arm.body);
                }
            },
            [&](const SemTry<Type, Failures>& value) noexcept {
                child(*value.body);
                for (const auto& arm : value.arms) {
                    if (arm.guard.has_value()) {
                        child(*arm.guard);
                    }
                    child(arm.body);
                }
            },
        },
        expression.value
    );
}

template<typename Type, typename Failures, typename Visitor>
auto visit_semantic_nodes(const SemanticRegion<Type, Failures>& region, Visitor visitor) noexcept
    -> void {
    const auto child = [&](const auto& value) noexcept {
        visit_semantic_nodes(value, visitor);
    };
    for (const auto& statement : region.statements) {
        if constexpr (std::invocable<Visitor&, const SemanticStatement<Type, Failures>&>) {
            std::invoke(visitor, statement);
        }
        std::visit(
            Overloaded {
                [&](const SemReturn<Type, Failures>& value) noexcept {
                    if (value.value.has_value()) {
                        child(*value.value);
                    }
                },
                [](const SemBreak&) static noexcept {},
                [](const SemContinue&) static noexcept {},
                [](const SemRethrow&) static noexcept {},
                [&](const SemThrow<Type, Failures>& value) noexcept { child(value.value); },
                [&](const SemExpressionStatement<Type, Failures>& value) noexcept {
                    child(value.expression);
                },
                [&](const SemInitialize<Type, Failures>& value) noexcept {
                    child(value.initializer);
                },
                [&](const SemAssign<Type, Failures>& value) noexcept {
                    child(value.target);
                    child(value.value);
                },
                [&](const SemLoop<Type, Failures>& value) noexcept {
                    child(*value.initializer);
                    if (value.condition.has_value()) {
                        child(*value.condition);
                    }
                    child(*value.body);
                    child(*value.steps);
                },
                [&](const SemRangeLoop<Type, Failures>& value) noexcept {
                    child(value.begin);
                    if (value.end.has_value()) {
                        child(*value.end);
                    }
                    child(*value.body);
                },
                [&](const SemTestReport<Type, Failures>& value) noexcept {
                    if (value.condition.has_value()) {
                        child(*value.condition);
                    }
                    if (value.message.has_value()) {
                        child(*value.message);
                    }
                },
                [&](const OwnedSemanticRegion<Type, Failures>& value) noexcept { child(*value); },
            },
            statement.value
        );
    }
    if (region.result.has_value()) {
        child(*region.result);
    }
}
