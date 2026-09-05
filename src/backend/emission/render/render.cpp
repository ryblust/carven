module carven:backend.emission.render.impl;

import :backend.emission.render.string;
import :backend.emission.render;
import :backend.target.symbol;
import :support.visit;
import std;

TargetRenderer::TargetRenderer(const TargetUnit& unit, EmissionPolicy policy) noexcept
    : unit(unit),
      generated_origin(
          std::visit(
              []<typename Policy>(const Policy& value) noexcept -> std::string {
                  if constexpr (std::same_as<Policy, SourceAttributedEmission>) {
                      return std::string(value.generated_origin);
                  } else if constexpr (std::same_as<Policy, StableInterfaceEmission>) {
                      return {};
                  } else {
                      static_assert(std::same_as<Policy, void>, "unhandled emission policy");
                  }
              },
              policy
          )
      ),
      stable_interface(std::holds_alternative<StableInterfaceEmission>(policy)) {}

auto TargetRenderer::text(std::string_view value) noexcept -> LayoutNodeID {
    return builder.text(value);
}

auto TargetRenderer::raw(std::string_view bytes) noexcept -> LayoutNodeID {
    return builder.raw(bytes);
}

auto TargetRenderer::render_identifier(const TargetIdentifier& value) noexcept -> LayoutNodeID {
    return text(value.spelling());
}

auto TargetRenderer::qualified_sequence(
    std::span<const SyntaxLayouts> components,
    bool globally_qualified
) noexcept -> SyntaxLayouts {
    const auto prefix = text(globally_qualified ? "::" : "");
    auto inline_qualified = concat({prefix, components.front().inline_qualified});
    for (const auto component : components.subspan(1)) {
        inline_qualified = concat({inline_qualified, text("::"), component.inline_qualified});
    }
    return {
        .inline_qualified = inline_qualified,
        .wrapping = inline_qualified,
    };
}

auto TargetRenderer::render_name_layouts(const TargetName& value) noexcept -> SyntaxLayouts {
    auto rendered = std::vector<SyntaxLayouts> {};
    rendered.reserve(value.components().size());
    for (const auto& component : value.components()) {
        const auto layout = render_identifier(component);
        rendered.push_back({.inline_qualified = layout, .wrapping = layout});
    }
    return qualified_sequence(rendered, value.is_globally_qualified());
}

auto TargetRenderer::render_name(const TargetName& value) noexcept -> LayoutNodeID {
    return render_name_layouts(value).wrapping;
}

auto TargetRenderer::concat(std::initializer_list<LayoutNodeID> children) noexcept -> LayoutNodeID {
    return builder.concat(std::vector<LayoutNodeID>(children));
}

auto TargetRenderer::choice(std::initializer_list<LayoutNodeID> alternatives) noexcept
    -> LayoutNodeID {
    return builder.choice(std::span<const LayoutNodeID>(alternatives.begin(), alternatives.size()));
}

auto TargetRenderer::stack(std::span<const LayoutNodeID> children, std::size_t blank_lines) noexcept
    -> LayoutNodeID {
    if (children.empty()) {
        return builder.empty();
    }
    auto separators = std::vector<LayoutNodeID> {};
    separators.reserve(blank_lines + 1);
    for (auto index = 0uz; index <= blank_lines; ++index) {
        separators.push_back(builder.line());
    }
    return builder.join(children, builder.concat(std::move(separators)));
}

auto TargetRenderer::delimited_sequence(
    std::span<const LayoutNodeID> children,
    std::string_view open,
    std::string_view separator,
    std::string_view close
) noexcept -> LayoutNodeID {
    if (children.empty()) {
        return concat({text(open), text(close)});
    }
    const auto flat_separator = text(std::format("{} ", separator));
    const auto flat_contents = builder.flatten(builder.join(children, flat_separator));
    const auto broken_separator = concat({text(separator), builder.line()});
    const auto broken_contents = builder.join(children, broken_separator);
    return choice(
        {concat({text(open), flat_contents, text(close)}),
         concat(
             {text(open),
              builder.indent(indent_width, concat({builder.line(), broken_contents})),
              builder.line(),
              text(close)}
         )}
    );
}

auto TargetRenderer::delimited_list(
    std::span<const LayoutNodeID> children,
    std::string_view open,
    std::string_view close
) noexcept -> LayoutNodeID {
    return delimited_sequence(children, open, ",", close);
}

auto TargetRenderer::braced_block(std::span<const LayoutNodeID> body) noexcept -> LayoutNodeID {
    if (body.empty()) {
        return text("{}");
    }
    return concat({
        text("{"),
        builder.indent(indent_width, concat({builder.line(), stack(body)})),
        builder.line(),
        text("}"),
    });
}

auto TargetRenderer::render_statement_block(std::span<const TargetStmt> body) noexcept
    -> LayoutNodeID {
    auto rendered = std::vector<LayoutNodeID> {};
    rendered.reserve(body.size());
    for (const auto& statement : body) {
        rendered.push_back(render_statement(statement));
    }
    return braced_block(rendered);
}

auto TargetRenderer::directive(LayoutNodeID value) noexcept -> LayoutNodeID {
    return builder.reset_indent(value);
}

auto TargetRenderer::render_raw_fragment(const TargetRawFragment& value) noexcept -> LayoutNodeID {
    return raw(value.bytes);
}

auto TargetRenderer::generated_transition() noexcept -> LayoutNodeID {
    return stable_interface ? builder.empty()
                            : builder.generated_location(cpp_string_token(generated_origin));
}

auto TargetRenderer::with_attribution(
    LayoutNodeID value,
    const TargetAttribution& attribution
) noexcept -> LayoutNodeID {
    if (stable_interface) {
        return value;
    }
    return std::visit(
        Overloaded {
            [&](const TargetSourceOwnedAttribution& source) noexcept {
                return concat({
                    builder.source_location(
                        source.origin.line,
                        cpp_string_token(source.origin.display_origin)
                    ),
                    value,
                });
            },
            [&](const TargetSourceExpansionAttribution& source) noexcept {
                return concat({
                    builder.source_location(
                        source.origin.line,
                        cpp_string_token(source.origin.display_origin)
                    ),
                    value,
                });
            },
            [&](const TargetRawSourceAttribution& source) noexcept {
                return concat({
                    builder.source_location(
                        source.origin.line,
                        cpp_string_token(source.origin.display_origin)
                    ),
                    value,
                });
            },
            [&](const TargetGeneratedExpansionAttribution&) noexcept { return value; },
            [&](const TargetCompilerOwnedAttribution&) noexcept {
                return concat({generated_transition(), value});
            },
        },
        attribution
    );
}
