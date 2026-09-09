module carven:semantic.semir.traversal;

import :semantic.semir.structured;
import :support.visit;
import std;

// Visits stored regions, statements and expressions, including constant-inactive source.
// Execution selection belongs to analyses and lowering; this traversal only follows children.

template<bool ReadOnly, typename Visitor>
class SemanticTraversal final {
    template<typename T>
    using Node = std::conditional_t<ReadOnly, const T, T>;

public:
    explicit SemanticTraversal(Visitor& visitor) noexcept
        : visitor(visitor) {}

    auto operator()(Node<SemanticExpression>& expression) const noexcept -> void {
        if constexpr (std::invocable<Visitor&, Node<SemanticExpression>&>) {
            std::invoke(visitor, expression);
        }
        const auto child = [&](auto& value) noexcept {
            (*this)(value);
        };
        std::visit(
            Overloaded {
                [](Node<SemConstant>&) static noexcept {},
                [](Node<SemBinding>&) static noexcept {},
                [](Node<SemCallable>&) static noexcept {},
                [](Node<SemEnumConstructor>&) static noexcept {},
                [&](Node<SemCpp>& value) noexcept {
                    for (auto& operand : value.operands) {
                        child(operand.expression);
                    }
                },
                [&](Node<SemCppCall>& value) noexcept {
                    std::visit(
                        Overloaded {
                            [](Node<CppNameReference>&) static noexcept {},
                            [&](Node<CppMemberCallee<SemCppOperand>>& callee) noexcept {
                                child(*callee.receiver.expression);
                            },
                            [&](Node<SemCppOperand>& callee) noexcept {
                                child(*callee.expression);
                            },
                        },
                        value.callee
                    );
                    for (auto& argument : value.arguments) {
                        child(argument.expression);
                    }
                },
                [&](Node<SemArray>& value) noexcept {
                    for (auto& element : value.elements) {
                        child(element);
                    }
                },
                [&](Node<SemArrayAdopt>& value) noexcept { child(*value.source); },
                [&](Node<SemStruct>& value) noexcept {
                    for (auto& field : value.fields) {
                        child(field.value);
                    }
                },
                [&](Node<SemEnumCase>& value) noexcept {
                    for (auto& element : value.payload) {
                        child(element);
                    }
                },
                [&](Node<SemUnary>& value) noexcept { child(*value.operand); },
                [&](Node<SemBinary>& value) noexcept {
                    child(*value.left);
                    child(*value.right);
                },
                [&](Node<SemShortCircuit>& value) noexcept {
                    child(*value.left);
                    child(*value.right);
                },
                [&](Node<SemCast>& value) noexcept { child(*value.operand); },
                [&](Node<SemField>& value) noexcept { child(*value.source); },
                [&](Node<SemIndex>& value) noexcept {
                    child(*value.source);
                    child(*value.index);
                },
                [&](Node<SemTextIntrinsic>& value) noexcept { child(*value.source); },
                [&](Node<SemCall>& value) noexcept {
                    child(*value.callee);
                    for (auto& argument : value.arguments) {
                        child(argument.expression);
                    }
                },
                [&](Node<SemClosure>& value) noexcept {
                    for (auto& capture : value.captures) {
                        child(capture.expression);
                    }
                },
                [&](Node<SemBorrowCallable>& value) noexcept { child(*value.source); },
                [&](Node<SemTake>& value) noexcept { child(*value.place); },
                [&](Node<SemPropagate>& value) noexcept { child(*value.operand); },
                [&](Node<SemIf>& value) noexcept {
                    for (auto& branch : value.branches) {
                        child(branch.condition);
                        child(branch.body);
                    }
                    if (value.otherwise.has_value()) {
                        child(**value.otherwise);
                    }
                },
                [&](Node<SemMatch>& value) noexcept {
                    child(*value.subject);
                    for (auto& arm : value.arms) {
                        if (arm.guard.has_value()) {
                            child(*arm.guard);
                        }
                        child(arm.body);
                    }
                },
                [&](Node<SemTry>& value) noexcept {
                    child(*value.body);
                    for (auto& arm : value.arms) {
                        if (arm.guard.has_value()) {
                            child(*arm.guard);
                        }
                        child(arm.body);
                    }
                },
            },
            expression.value
        );
        if constexpr (requires { visitor.leave(expression); }) {
            visitor.leave(expression);
        }
    }

    auto operator()(Node<SemanticRegion>& region) const noexcept -> void {
        if constexpr (std::invocable<Visitor&, Node<SemanticRegion>&>) {
            std::invoke(visitor, region);
        }
        const auto child = [&](auto& value) noexcept {
            (*this)(value);
        };
        for (auto& statement : region.statements) {
            if constexpr (std::invocable<Visitor&, Node<SemanticStatement>&>) {
                std::invoke(visitor, statement);
            }
            std::visit(
                Overloaded {
                    [&](Node<SemReturn>& value) noexcept {
                        if (value.value.has_value()) {
                            child(*value.value);
                        }
                    },
                    [](Node<SemBreak>&) static noexcept {},
                    [](Node<SemContinue>&) static noexcept {},
                    [](Node<SemRethrow>&) static noexcept {},
                    [&](Node<SemThrow>& value) noexcept { child(value.value); },
                    [&](Node<SemExpressionStatement>& value) noexcept { child(value.expression); },
                    [&](Node<SemInitialize>& value) noexcept { child(value.initializer); },
                    [&](Node<SemAssign>& value) noexcept {
                        child(value.target);
                        child(value.value);
                    },
                    [&](Node<SemLoop>& value) noexcept {
                        child(*value.initializer);
                        if (value.condition.has_value()) {
                            child(*value.condition);
                        }
                        child(*value.body);
                        child(*value.steps);
                    },
                    [&](Node<SemRangeLoop>& value) noexcept {
                        std::visit(
                            [&](auto& range) noexcept {
                                if constexpr (requires { range.begin; }) {
                                    child(range.begin);
                                    child(range.end);
                                } else {
                                    child(range.value);
                                }
                            },
                            value.source
                        );
                        child(*value.body);
                    },
                    [&](Node<SemTestReport>& value) noexcept {
                        if (value.condition.has_value()) {
                            child(*value.condition);
                        }
                        if (value.message.has_value()) {
                            child(*value.message);
                        }
                    },
                    [&](Node<OwnedSemanticRegion>& value) noexcept { child(*value); },
                },
                statement.value
            );
        }
        if (region.result.has_value()) {
            child(*region.result);
        }
    }

private:
    Visitor& visitor;
};

template<typename Node, typename Visitor>
    requires std::same_as<std::remove_const_t<Node>, SemanticExpression>
    || std::same_as<std::remove_const_t<Node>, SemanticRegion>
auto visit_semantic_nodes(Node& node, Visitor visitor) noexcept -> void {
    const auto traverse = SemanticTraversal<std::is_const_v<Node>, Visitor>(visitor);
    traverse(node);
}
