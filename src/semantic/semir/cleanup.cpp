module carven:semantic.semir.cleanup.impl;

import :semantic.semir.children;
import :semantic.semir.structured;
import std;

namespace {
using Node = std::variant<SemanticExpression*, SemanticStatement*, SemanticRegion*>;

struct Event final {
    Node node;
    bool leave;
};

auto discard(SemanticExpressionValue& value) noexcept -> void {
    static_cast<SemanticExpressionValue::Base&>(value).emplace<SemDefault>();
}

auto discard(SemanticStatementValue& value) noexcept -> void {
    static_cast<SemanticStatementValue::Base&>(value).emplace<SemBreak>();
}

template<typename Value>
auto clear_tree(Value& value) noexcept -> void {
    auto pending = std::vector<Event>();
    const auto add = [&](auto& edge) noexcept {
        const auto node = [&]() noexcept {
            using Edge = std::remove_cvref_t<decltype(edge)>;
            if constexpr (std::same_as<Edge, OwnedSemanticExpression>
                          || std::same_as<Edge, OwnedSemanticRegion>) {
                return edge.get();
            } else {
                return std::addressof(edge);
            }
        }();
        if (node != nullptr) {
            pending.push_back({Node(node), false});
        }
    };
    visit_semantic_edges(value, add);
    while (!pending.empty()) {
        const auto event = pending.back();
        pending.pop_back();
        event.node.visit([&](auto* node) noexcept {
            using T = std::remove_pointer_t<decltype(node)>;
            if constexpr (std::same_as<T, SemanticRegion>) {
                visit_semantic_edges(*node, add);
            } else {
                if (event.leave) {
                    discard(node->value);
                    return;
                }
                pending.push_back({event.node, true});
                visit_semantic_edges(node->value, add);
            }
        });
    }
    discard(value);
}
} // namespace

auto SemanticExpressionCleanup::clear(SemanticExpressionValue& value) noexcept -> void {
    clear_tree(value);
}

auto SemanticStatementCleanup::clear(SemanticStatementValue& value) noexcept -> void {
    clear_tree(value);
}
