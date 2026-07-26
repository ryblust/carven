module carven:backend.emission.render.unit.impl;

import :backend.emission.render;
import :support.visit;
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

auto TargetRenderer::render_unit(const TargetUnitRoot& value) && noexcept -> LayoutDocument {
    auto sections = std::visit(
        Overloaded {
            [&](const TargetInterfaceComponentUnit& header) noexcept {
                auto result = std::vector<LayoutNodeID> {
                    directive(text("#pragma once")),
                    directive(text("#include <carven/runtime/runtime.hpp>")),
                };
                for (const auto& header_path : header.prerequisite_header_paths) {
                    result.push_back(directive(text(std::format("#include <{}>", header_path))));
                }
                if (const auto body = render_sections(header.sections)) {
                    result.push_back(*body);
                }
                return result;
            },
            [&](const TargetModuleImplementationUnit& implementation) noexcept {
                auto includes = std::vector<LayoutNodeID> {
                    directive(text("#include <carven/runtime/runtime.hpp>")),
                };
                for (const auto& header_path : implementation.interface_header_paths) {
                    includes.push_back(directive(text(std::format("#include <{}>", header_path))));
                }
                if (implementation.testing_support) {
                    includes.push_back(
                        directive(text("#include <carven/std/testing/testing.hpp>"))
                    );
                }
                auto result = std::vector<LayoutNodeID> {stack(includes)};
                if (const auto body = render_sections(implementation.sections)) {
                    result.push_back(*body);
                }
                return result;
            },
            [&](const TargetTestEntryUnit& test_entry) noexcept {
                return std::vector<LayoutNodeID> {
                    directive(text("#include <carven/std/testing/testing.hpp>")),
                    stack(render_items(test_entry.items), 1),
                };
            },
        },
        value
    );

    const auto root = stack(sections, 1);
    return std::move(builder).finish(root);
}
