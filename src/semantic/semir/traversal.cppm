module carven:semantic.semir.traversal;

import :semantic.semir.children;
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
        visit_semantic_children(expression.value, child);
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
                        child(value.source);
                        child(*value.body);
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
