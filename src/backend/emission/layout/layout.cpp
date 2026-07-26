module carven:backend.emission.layout.impl;

import :backend.emission.layout;
import :support.invariant;
import :support.visit;
import std;

LayoutDocument::LayoutDocument(std::vector<LayoutNode> nodes, LayoutNodeID root) noexcept
    : nodes(std::move(nodes)),
      root_id(root) {}

LayoutBuilder::LayoutBuilder() noexcept
    : empty_id(add(LayoutConcat {.children = {}})) {}

auto LayoutBuilder::add(LayoutNodeValue value) noexcept -> LayoutNodeID {
    const auto id = LayoutNodeID {.value = nodes.size()};
    nodes.push_back({.value = std::move(value)});
    return id;
}

auto LayoutBuilder::empty() const noexcept -> LayoutNodeID {
    return empty_id;
}

auto LayoutBuilder::text(std::string_view value) noexcept -> LayoutNodeID {
    if (value.contains('\n') || value.contains('\r')) {
        invariant_violation("layout text nodes cannot contain line breaks");
    }
    return value.empty() ? empty_id : add(LayoutText {.value = std::string(value)});
}

auto LayoutBuilder::raw(std::string_view bytes) noexcept -> LayoutNodeID {
    return bytes.empty() ? empty_id : add(LayoutRaw {.bytes = std::string(bytes)});
}

auto LayoutBuilder::line() noexcept -> LayoutNodeID {
    return add(LayoutLine {});
}

auto LayoutBuilder::source_location(std::size_t line, std::string origin_literal) noexcept
    -> LayoutNodeID {
    return add(LayoutSourceLocation {.line = line, .origin_literal = std::move(origin_literal)});
}

auto LayoutBuilder::generated_location(std::string origin_literal) noexcept -> LayoutNodeID {
    return add(LayoutGeneratedLocation {.origin_literal = std::move(origin_literal)});
}

auto LayoutBuilder::concat(std::vector<LayoutNodeID> children) noexcept -> LayoutNodeID {
    std::erase(children, empty_id);
    if (children.empty()) {
        return empty_id;
    }
    if (children.size() == 1) {
        return children.front();
    }
    return add(LayoutConcat {.children = std::move(children)});
}

auto LayoutBuilder::join(std::span<const LayoutNodeID> children, LayoutNodeID separator) noexcept
    -> LayoutNodeID {
    auto joined = std::vector<LayoutNodeID> {};
    if (!children.empty()) {
        joined.reserve(children.size() * 2 - 1);
    }
    for (auto index = 0uz; index < children.size(); ++index) {
        if (index != 0) {
            joined.push_back(separator);
        }
        joined.push_back(children[index]);
    }
    return concat(std::move(joined));
}

auto LayoutBuilder::indent(std::size_t width, LayoutNodeID child) noexcept -> LayoutNodeID {
    return child == empty_id || width == 0 ? child
                                           : add(LayoutIndent {.width = width, .child = child});
}

auto LayoutBuilder::choice(std::span<const LayoutNodeID> alternatives) noexcept -> LayoutNodeID {
    if (alternatives.empty()) {
        invariant_violation("layout choices require at least one alternative");
    }
    if (alternatives.size() == 1) {
        return alternatives.front();
    }
    return add(
        LayoutChoice {
            .alternatives = std::vector<LayoutNodeID>(alternatives.begin(), alternatives.end()),
        }
    );
}

auto LayoutBuilder::flatten(LayoutNodeID child) noexcept -> LayoutNodeID {
    return child == empty_id ? child : add(LayoutFlatten {.child = child});
}

auto LayoutBuilder::reset_indent(LayoutNodeID child) noexcept -> LayoutNodeID {
    return child == empty_id ? child : add(LayoutResetIndent {.child = child});
}

auto LayoutBuilder::finish(LayoutNodeID root) && noexcept -> LayoutDocument {
    return LayoutDocument(std::move(nodes), root);
}

namespace {

struct LayoutCommand final {
    std::size_t indent;
    bool force_preferred_choices;
    LayoutNodeID node_id;
};

auto push_children(
    std::vector<LayoutCommand>& commands,
    const LayoutConcat& concatenation,
    std::size_t indent,
    bool force_preferred_choices
) noexcept -> void {
    for (auto child = concatenation.children.rbegin(); child != concatenation.children.rend();
         ++child) {
        commands.push_back({
            .indent = indent,
            .force_preferred_choices = force_preferred_choices,
            .node_id = *child,
        });
    }
}

auto fits(
    std::span<const LayoutNode> nodes,
    std::size_t line_width,
    std::size_t column,
    bool line_start,
    std::vector<LayoutCommand> commands
) noexcept -> bool {
    while (!commands.empty()) {
        const auto command = commands.back();
        commands.pop_back();
        const auto& current = nodes[command.node_id.value];
        const auto result = std::visit(
            Overloaded {
                [&](const LayoutText& value) noexcept -> std::optional<bool> {
                    if (line_start) {
                        column = command.indent;
                        line_start = false;
                    }
                    if (column > line_width || value.value.size() > line_width - column) {
                        return false;
                    }
                    column += value.value.size();
                    return std::nullopt;
                },
                [&](const LayoutRaw& value) noexcept -> std::optional<bool> {
                    const auto line_break = value.bytes.find_first_of("\r\n");
                    const auto width =
                        line_break == std::string::npos ? value.bytes.size() : line_break;
                    if (column > line_width || width > line_width - column) {
                        return false;
                    }
                    if (line_break != std::string::npos) {
                        return true;
                    }
                    column += width;
                    line_start = false;
                    return std::nullopt;
                },
                [](const LayoutLine&) static noexcept -> std::optional<bool> { return true; },
                [](const LayoutSourceLocation&) static noexcept -> std::optional<bool> {
                    return true;
                },
                [](const LayoutGeneratedLocation&) static noexcept -> std::optional<bool> {
                    return true;
                },
                [&](const LayoutConcat& concatenation) noexcept -> std::optional<bool> {
                    push_children(
                        commands,
                        concatenation,
                        command.indent,
                        command.force_preferred_choices
                    );
                    return std::nullopt;
                },
                [&](const LayoutIndent& indent) noexcept -> std::optional<bool> {
                    commands.push_back({
                        .indent = command.indent + indent.width,
                        .force_preferred_choices = command.force_preferred_choices,
                        .node_id = indent.child,
                    });
                    return std::nullopt;
                },
                [&](const LayoutChoice& choice) noexcept -> std::optional<bool> {
                    if (command.force_preferred_choices) {
                        commands.push_back({
                            .indent = command.indent,
                            .force_preferred_choices = true,
                            .node_id = choice.alternatives.front(),
                        });
                        return std::nullopt;
                    }
                    for (const auto alternative : choice.alternatives) {
                        auto candidate = commands;
                        candidate.push_back({
                            .indent = command.indent,
                            .force_preferred_choices = false,
                            .node_id = alternative,
                        });
                        if (fits(nodes, line_width, column, line_start, std::move(candidate))) {
                            return true;
                        }
                    }
                    return false;
                },
                [&](const LayoutFlatten& flatten) noexcept -> std::optional<bool> {
                    commands.push_back({
                        .indent = command.indent,
                        .force_preferred_choices = true,
                        .node_id = flatten.child,
                    });
                    return std::nullopt;
                },
                [&](const LayoutResetIndent& reset) noexcept -> std::optional<bool> {
                    commands.push_back({
                        .indent = 0,
                        .force_preferred_choices = command.force_preferred_choices,
                        .node_id = reset.child,
                    });
                    return std::nullopt;
                },
            },
            current.value
        );
        if (result.has_value()) {
            return *result;
        }
    }
    return true;
}

} // namespace

auto render_layout(const LayoutDocument& document, std::size_t line_width) noexcept -> std::string {
    auto output = std::string {};
    auto column = 0uz;
    auto line_start = true;
    auto preserved_prefix = 0uz;
    auto physical_line = 1uz;
    auto source_origin = std::string();
    auto next_source_line = 0uz;
    auto source_active = false;
    auto commands = std::vector<LayoutCommand> {{
        .indent = 0,
        .force_preferred_choices = false,
        .node_id = document.root_id,
    }};

    const auto append_newline = [&]() noexcept {
        output += '\n';
        ++physical_line;
        if (source_active) {
            ++next_source_line;
        }
        column = 0;
        line_start = true;
    };

    const auto begin_directive = [&]() noexcept {
        while (output.size() > preserved_prefix
               && (output.back() == ' ' || output.back() == '\t')) {
            output.pop_back();
        }
        if (!line_start) {
            append_newline();
        }
    };

    while (!commands.empty()) {
        const auto command = commands.back();
        commands.pop_back();
        const auto& current = document.nodes[command.node_id.value];
        std::visit(
            Overloaded {
                [&](const LayoutText& value) noexcept {
                    if (line_start && !value.value.empty()) {
                        output.append(command.indent, ' ');
                        column = command.indent;
                        line_start = false;
                    }
                    output += value.value;
                    column += value.value.size();
                },
                [&](const LayoutRaw& value) noexcept {
                    output += value.bytes;
                    preserved_prefix = output.size();
                    for (auto index = 0uz; index < value.bytes.size(); ++index) {
                        if (value.bytes[index] == '\r') {
                            if (index + 1 < value.bytes.size() && value.bytes[index + 1] == '\n') {
                                ++index;
                            }
                        } else if (value.bytes[index] != '\n') {
                            continue;
                        }
                        ++physical_line;
                        if (source_active) {
                            ++next_source_line;
                        }
                    }
                    const auto line_break = value.bytes.find_last_of("\r\n");
                    if (line_break == std::string::npos) {
                        column += value.bytes.size();
                        line_start = false;
                    } else {
                        column = value.bytes.size() - line_break - 1;
                        line_start = column == 0;
                    }
                },
                [&](const LayoutLine&) noexcept {
                    while (output.size() > preserved_prefix
                           && (output.back() == ' ' || output.back() == '\t')) {
                        output.pop_back();
                    }
                    append_newline();
                },
                [&](const LayoutSourceLocation& location) noexcept {
                    if (source_active
                        && source_origin == location.origin_literal
                        && next_source_line == location.line) {
                        return;
                    }
                    begin_directive();
                    output += std::format("#line {} {}", location.line, location.origin_literal);
                    source_active = true;
                    source_origin = location.origin_literal;
                    next_source_line = location.line;
                    append_newline();
                    --next_source_line;
                },
                [&](const LayoutGeneratedLocation& location) noexcept {
                    if (!source_active) {
                        return;
                    }
                    begin_directive();
                    source_active = false;
                    output +=
                        std::format("#line {} {}", physical_line + 1, location.origin_literal);
                    append_newline();
                },
                [&](const LayoutConcat& concatenation) noexcept {
                    push_children(
                        commands,
                        concatenation,
                        command.indent,
                        command.force_preferred_choices
                    );
                },
                [&](const LayoutIndent& indent) noexcept {
                    commands.push_back({
                        .indent = command.indent + indent.width,
                        .force_preferred_choices = command.force_preferred_choices,
                        .node_id = indent.child,
                    });
                },
                [&](const LayoutChoice& choice) noexcept {
                    if (command.force_preferred_choices) {
                        commands.push_back({
                            .indent = command.indent,
                            .force_preferred_choices = true,
                            .node_id = choice.alternatives.front(),
                        });
                        return;
                    }
                    auto selected = choice.alternatives.back();
                    for (auto index = 0uz; index + 1 < choice.alternatives.size(); ++index) {
                        const auto alternative = choice.alternatives[index];
                        auto candidate = commands;
                        candidate.push_back({
                            .indent = command.indent,
                            .force_preferred_choices = false,
                            .node_id = alternative,
                        });
                        if (fits(
                                document.nodes,
                                line_width,
                                column,
                                line_start,
                                std::move(candidate)
                            )) {
                            selected = alternative;
                            break;
                        }
                    }
                    commands.push_back({
                        .indent = command.indent,
                        .force_preferred_choices = false,
                        .node_id = selected,
                    });
                },
                [&](const LayoutFlatten& flatten) noexcept {
                    commands.push_back({
                        .indent = command.indent,
                        .force_preferred_choices = true,
                        .node_id = flatten.child,
                    });
                },
                [&](const LayoutResetIndent& reset) noexcept {
                    commands.push_back({
                        .indent = 0,
                        .force_preferred_choices = command.force_preferred_choices,
                        .node_id = reset.child,
                    });
                },
            },
            current.value
        );
    }
    while (output.size() > preserved_prefix
           && (output.back() == ' ' || output.back() == '\t' || output.back() == '\n')) {
        output.pop_back();
    }
    output += '\n';
    return output;
}
