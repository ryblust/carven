module carven:formatter.layout.document.impl;

import :formatter.layout.document;
import :support.invariant;
import std;

namespace {

// Width counts UTF-8 bytes, so non-ASCII text can wrap before its display width.
auto flat_width(std::string_view value) noexcept -> std::optional<std::size_t> {
    if (value.find_first_of("\r\n") != std::string_view::npos) {
        return std::nullopt;
    }
    return value.size();
}

auto add_width(std::size_t left, std::size_t right) noexcept -> std::size_t {
    return left > std::numeric_limits<std::size_t>::max() - right
        ? std::numeric_limits<std::size_t>::max()
        : left + right;
}

} // namespace

auto FormattingDocument::append(Node value) noexcept -> FormattingNodeID {
    const auto id = FormattingNodeID {.index = nodes.size()};
    nodes.push_back(std::move(value));
    return id;
}

auto FormattingDocument::node(FormattingNodeID id) const noexcept -> const Node& {
    if (id.index >= nodes.size()) {
        invariant_violation("Formatter document used an invalid node");
    }
    return nodes[id.index];
}

auto FormattingDocument::text(std::string_view value) noexcept -> FormattingNodeID {
    const auto width = flat_width(value);
    if (!width) {
        invariant_violation("Formatter Text must not contain line endings");
    }
    return append(
        Node {.kind = Kind::Text, .text = std::string(value), .children = {}, .flat_width = width}
    );
}

auto FormattingDocument::verbatim(std::string_view value) noexcept -> FormattingNodeID {
    return append(
        Node {
            .kind = Kind::Verbatim,
            .text = std::string(value),
            .children = {},
            .flat_width = flat_width(value)
        }
    );
}

auto FormattingDocument::concat(std::vector<FormattingNodeID> children) noexcept
    -> FormattingNodeID {
    auto width = std::optional<std::size_t>(0);
    for (const auto child : children) {
        const auto child_width = node(child).flat_width;
        width =
            width && child_width ? std::optional(add_width(*width, *child_width)) : std::nullopt;
    }
    return append(
        Node {
            .kind = Kind::Concat,
            .text = {},
            .children = std::move(children),
            .flat_width = width
        }
    );
}

auto FormattingDocument::line(bool space_when_flat) noexcept -> FormattingNodeID {
    return append(
        Node {
            .kind = Kind::Line,
            .text = space_when_flat ? " " : "",
            .children = {},
            .flat_width = space_when_flat ? 1uz : 0uz
        }
    );
}

auto FormattingDocument::hardline() noexcept -> FormattingNodeID {
    return append(
        Node {.kind = Kind::HardLine, .text = {}, .children = {}, .flat_width = std::nullopt}
    );
}

auto FormattingDocument::indent(FormattingNodeID child) noexcept -> FormattingNodeID {
    const auto width = node(child).flat_width;
    return append(
        Node {.kind = Kind::Indent, .text = {}, .children = {child}, .flat_width = width}
    );
}

auto FormattingDocument::group(FormattingNodeID child) noexcept -> FormattingNodeID {
    const auto width = node(child).flat_width;
    return append(Node {.kind = Kind::Group, .text = {}, .children = {child}, .flat_width = width});
}

auto FormattingDocument::fits(std::span<const Frame> pending, std::size_t remaining) const noexcept
    -> bool {
    auto expanded = std::vector<Frame>();
    auto cursor = pending.size();
    while (cursor != 0 || !expanded.empty()) {
        const auto frame = expanded.empty() ? pending[--cursor] : expanded.back();
        if (!expanded.empty()) {
            expanded.pop_back();
        }
        const auto& value = node(frame.id);
        if (frame.flat && value.flat_width) {
            if (*value.flat_width > remaining) {
                return false;
            }
            remaining -= *value.flat_width;
            continue;
        }
        switch (value.kind) {
            case Kind::Text:
            case Kind::Verbatim: {
                const auto end = value.text.find_first_of("\r\n");
                const auto width = end == std::string::npos ? value.text.size() : end;
                if (width > remaining) {
                    return false;
                }
                remaining -= width;
                if (end != std::string::npos) {
                    return true;
                }
                break;
            }
            case Kind::Line:
                if (!frame.flat) {
                    return true;
                }
                if (value.text.size() > remaining) {
                    return false;
                }
                remaining -= value.text.size();
                break;
            case Kind::HardLine: return true;
            case Kind::Concat:
            case Kind::Indent:
            case Kind::Group:
                for (const auto child : value.children | std::views::reverse) {
                    expanded.push_back(
                        Frame {.id = child, .indentation = frame.indentation, .flat = frame.flat}
                    );
                }
                break;
        }
    }
    return true;
}

auto FormattingDocument::render(
    FormattingNodeID root,
    std::size_t width,
    std::size_t indent_width
) const noexcept -> std::string {
    auto pending = std::vector<Frame> {{.id = root, .indentation = 0, .flat = false}};
    auto output = std::string();
    auto column = 0uz;
    auto pending_indentation = true;
    while (!pending.empty()) {
        const auto frame = pending.back();
        pending.pop_back();
        const auto& value = node(frame.id);
        const auto emit_text = [&](std::string_view text) noexcept {
            if (text.empty()) {
                return;
            }
            if (pending_indentation && text.front() != '\r' && text.front() != '\n') {
                output.append(frame.indentation, ' ');
                column = frame.indentation;
            }
            output += text;
            const auto last_break = text.find_last_of("\r\n");
            column = last_break == std::string_view::npos ? add_width(column, text.size())
                                                          : text.size() - last_break - 1uz;
            pending_indentation = false;
        };
        switch (value.kind) {
            case Kind::Text:
            case Kind::Verbatim: emit_text(value.text); break;
            case Kind::Line:
                if (frame.flat) {
                    emit_text(value.text);
                    break;
                }
                [[fallthrough]];
            case Kind::HardLine:
                output += '\n';
                column = 0;
                pending_indentation = true;
                break;
            case Kind::Concat:
                for (const auto child : value.children | std::views::reverse) {
                    pending.push_back(
                        Frame {.id = child, .indentation = frame.indentation, .flat = frame.flat}
                    );
                }
                break;
            case Kind::Indent:
                pending.push_back(
                    Frame {
                        .id = value.children.front(),
                        .indentation = frame.indentation + indent_width,
                        .flat = frame.flat
                    }
                );
                break;
            case Kind::Group: {
                const auto current_column = pending_indentation ? frame.indentation : column;
                const auto remaining = width > current_column ? width - current_column : 0uz;
                const auto flat = frame.flat
                    || (value.flat_width
                        && *value.flat_width <= remaining
                        && fits(pending, remaining - *value.flat_width));
                pending.push_back(
                    Frame {
                        .id = value.children.front(),
                        .indentation = frame.indentation,
                        .flat = flat
                    }
                );
                break;
            }
        }
    }
    return output;
}
