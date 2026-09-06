module carven:backend.emission.render.unit.impl;

import :backend.emission.render;
import :support.visit;
import std;

auto TargetRenderer::item_category(const TargetItem& item) const noexcept
    -> std::optional<TargetItemCategory> {
    return std::visit(
        Overloaded {
            [](const TargetRawFragment&) static noexcept -> std::optional<TargetItemCategory> {
                return std::nullopt;
            },
            [](const TargetUsing&) static noexcept -> std::optional<TargetItemCategory> {
                return TargetItemCategory::OtherDeclaration;
            },
            [](const TargetNamespace&) static noexcept -> std::optional<TargetItemCategory> {
                return TargetItemCategory::OtherDeclaration;
            },
            [](const TargetDecl& declaration) static noexcept -> std::optional<TargetItemCategory> {
                return std::visit(
                    []<typename Value>(const Value&) static noexcept -> TargetItemCategory {
                        if constexpr (std::same_as<Value, TargetFunctionDecl>
                                      || std::same_as<Value, TargetStructForwardDecl>
                                      || std::same_as<Value, TargetEnumForwardDecl>
                                      || std::same_as<Value, TargetClassForwardDecl>) {
                            return TargetItemCategory::ForwardOrFunctionDeclaration;
                        } else if constexpr (std::same_as<Value, TargetStructDecl>
                                             || std::same_as<Value, TargetEnumDecl>
                                             || std::same_as<Value, TargetClassDecl>) {
                            return TargetItemCategory::OtherDeclaration;
                        } else {
                            static_assert(
                                std::same_as<Value, void>,
                                "unhandled target declaration category"
                            );
                        }
                    },
                    declaration
                );
            },
        },
        item.value
    );
}

auto TargetRenderer::separation_between(
    TargetItemCategory previous,
    TargetItemCategory current,
    TargetContainerKind container
) const noexcept -> std::size_t {
    static_cast<void>(container);
    return previous == TargetItemCategory::ForwardOrFunctionDeclaration
            && current == TargetItemCategory::ForwardOrFunctionDeclaration
        ? 0uz
        : 1uz;
}

auto TargetRenderer::render_items(
    std::span<const TargetItem> items,
    TargetContainerKind container
) noexcept -> LayoutNodeID {
    if (items.empty()) {
        return builder.empty();
    }
    auto rendered = render_item(items.front());
    auto previous = item_category(items.front());
    for (const auto& item : items.subspan(1)) {
        const auto current = item_category(item);
        const auto blank_lines = previous.has_value() && current.has_value()
            ? separation_between(*previous, *current, container)
            : 0uz;
        auto separator = builder.line();
        if (blank_lines != 0) {
            separator = concat({separator, builder.line()});
        }
        rendered = concat({rendered, separator, render_item(item)});
        previous = current;
    }
    return rendered;
}

auto TargetRenderer::render_sections(const TargetUnitSections& sections) noexcept
    -> std::optional<LayoutNodeID> {
    auto rendered = std::vector<LayoutNodeID>();
    if (!sections.preamble.empty()) {
        rendered.push_back(render_items(sections.preamble, TargetContainerKind::TopLevel));
    }
    if (!sections.body.empty()) {
        rendered.push_back(render_items(sections.body, TargetContainerKind::TopLevel));
    }
    if (!sections.epilogue.empty()) {
        rendered.push_back(render_items(sections.epilogue, TargetContainerKind::TopLevel));
    }
    if (rendered.empty()) {
        return std::nullopt;
    }
    return stack(rendered, 1);
}

auto TargetRenderer::render_unit() && noexcept -> LayoutDocument {
    auto rendered = std::vector<LayoutNodeID>();
    rendered.reserve(unit.directive_groups().size() + 1uz);
    for (const auto& group : unit.directive_groups()) {
        auto directives = std::vector<LayoutNodeID>();
        directives.reserve(group.directives.size());
        for (const auto& value : group.directives) {
            directives.push_back(directive(text(value.bytes)));
        }
        auto rendered_group = stack(directives);
        if (group.attribution.has_value()) {
            rendered_group = with_attribution(rendered_group, *group.attribution);
        }
        rendered.push_back(rendered_group);
    }
    if (const auto sections = render_sections(unit.sections())) {
        rendered.push_back(*sections);
    }
    const auto document = stack(rendered, 1);
    return std::move(builder).finish(document);
}
