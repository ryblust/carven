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

    template<typename Root>
    auto operator()(Root& root) noexcept -> void {
        pending.emplace_back(std::addressof(root));
        while (!pending.empty()) {
            const auto next = pending.back();
            pending.pop_back();
            next.visit(
                Overloaded {
                    [&](Node<SemanticExpression>* expression) noexcept { enter(*expression); },
                    [&](Node<SemanticRegion>* region) noexcept { enter(*region); },
                    [&](Node<SemanticStatement>* statement) noexcept { enter(*statement); },
                    [&](Leave leave) noexcept {
                        if constexpr (requires { visitor.leave(*leave.expression); }) {
                            visitor.leave(*leave.expression);
                        }
                    },
                }
            );
        }
    }

private:
    struct Leave final {
        Node<SemanticExpression>* expression;
    };

    using Event = std::
        variant<Node<SemanticExpression>*, Node<SemanticRegion>*, Node<SemanticStatement>*, Leave>;

    auto enter(Node<SemanticExpression>& expression) noexcept -> void {
        if constexpr (std::invocable<Visitor&, Node<SemanticExpression>&>) {
            std::invoke(visitor, expression);
        }
        if constexpr (requires { visitor.leave(expression); }) {
            pending.emplace_back(Leave {.expression = std::addressof(expression)});
        }
        const auto first_child = pending.size();
        visit_semantic_children(expression.value, [&](auto& child) noexcept {
            pending.emplace_back(std::addressof(child));
        });
        std::reverse(pending.begin() + static_cast<std::ptrdiff_t>(first_child), pending.end());
    }

    auto enter(Node<SemanticRegion>& region) noexcept -> void {
        if constexpr (std::invocable<Visitor&, Node<SemanticRegion>&>) {
            std::invoke(visitor, region);
        }
        if (region.result) {
            pending.emplace_back(std::addressof(*region.result));
        }
        for (auto& statement : region.statements | std::views::reverse) {
            pending.emplace_back(std::addressof(statement));
        }
    }

    auto enter(Node<SemanticStatement>& statement) noexcept -> void {
        if constexpr (std::invocable<Visitor&, Node<SemanticStatement>&>) {
            std::invoke(visitor, statement);
        }
        const auto first_child = pending.size();
        const auto child = [&](auto& value) noexcept {
            pending.emplace_back(std::addressof(value));
        };
        statement.value.visit(
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
            }
        );
        std::reverse(pending.begin() + static_cast<std::ptrdiff_t>(first_child), pending.end());
    }

    std::vector<Event> pending;
    Visitor& visitor;
};

template<typename Node, typename Visitor>
    requires std::same_as<std::remove_const_t<Node>, SemanticExpression>
    || std::same_as<std::remove_const_t<Node>, SemanticRegion>
auto visit_semantic_nodes(Node& node, Visitor visitor) noexcept -> void {
    auto traverse = SemanticTraversal<std::is_const_v<Node>, Visitor>(visitor);
    traverse(node);
}
