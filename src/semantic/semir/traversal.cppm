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
        pending.push_back({.node = std::addressof(root), .leaving = false});
        while (!pending.empty()) {
            const auto next = pending.back();
            pending.pop_back();
            next.node.visit([&](auto* node) noexcept {
                if (next.leaving) {
                    if constexpr (requires { visitor.leave(*node); }) {
                        visitor.leave(*node);
                    }
                } else {
                    enter(*node);
                }
            });
        }
    }

private:
    struct Event final {
        std::variant<Node<SemanticExpression>*, Node<SemanticRegion>*, Node<SemanticStatement>*>
            node;
        bool leaving;
    };

    template<typename Value>
    auto enter(Value& value) noexcept -> void {
        if constexpr (std::invocable<Visitor&, Value&>) {
            std::invoke(visitor, value);
        }
        if constexpr (requires { visitor.leave(value); }) {
            pending.push_back({.node = std::addressof(value), .leaving = true});
        }
        const auto first_child = pending.size();
        const auto child = [&](auto& source) noexcept {
            pending.push_back({.node = std::addressof(source), .leaving = false});
        };
        if constexpr (std::same_as<std::remove_const_t<Value>, SemanticRegion>) {
            visit_semantic_children(value, child);
        } else {
            visit_semantic_children(value.value, child);
        }
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
