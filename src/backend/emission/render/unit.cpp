module carven:backend.emission.render.unit.impl;

import :backend.emission.render;
import std;

auto TargetRenderer::render_items(std::span<const TargetItemID> items) noexcept
    -> std::vector<LayoutNodeID> {
    auto rendered = std::vector<LayoutNodeID>();
    rendered.reserve(items.size());
    for (const auto item : items) {
        rendered.push_back(render_item(item));
    }
    return rendered;
}

auto TargetRenderer::render_sections(const TargetUnitSections& sections) noexcept
    -> std::optional<LayoutNodeID> {
    auto rendered = std::vector<LayoutNodeID>();
    if (!sections.preamble.empty()) {
        rendered.push_back(stack(render_items(sections.preamble), 1));
    }
    if (!sections.body.empty()) {
        rendered.push_back(stack(render_items(sections.body), 1));
    }
    if (!sections.epilogue.empty()) {
        rendered.push_back(stack(render_items(sections.epilogue), 1));
    }
    if (rendered.empty()) {
        return std::nullopt;
    }
    return stack(rendered, 1);
}

auto TargetRenderer::render_unit() && noexcept -> LayoutDocument {
    const auto& root = unit.root();
    auto rendered = std::vector<LayoutNodeID>();
    rendered.reserve(root.directive_groups.size() + 1uz);
    for (const auto& group : root.directive_groups) {
        auto directives = std::vector<LayoutNodeID>();
        directives.reserve(group.directives.size());
        for (const auto& value : group.directives) {
            directives.push_back(directive(text(value.bytes)));
        }
        rendered.push_back(stack(directives));
    }
    if (const auto sections = render_sections(root.sections)) {
        rendered.push_back(*sections);
    }
    const auto document = stack(rendered, 1);
    return std::move(builder).finish(document);
}
