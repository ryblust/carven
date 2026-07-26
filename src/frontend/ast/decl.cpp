module carven:frontend.ast.decl.impl;

import :frontend.ast.decl;
import std;

auto binding_target_span(const ASTBindingTarget& target) noexcept -> Span {
    return std::visit(
        [](const auto& value) static noexcept -> Span {
            if constexpr (requires { value.name_span; }) {
                return value.name_span;
            } else {
                return value.underscore_span;
            }
        },
        target
    );
}
