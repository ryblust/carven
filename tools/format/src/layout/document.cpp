module carven:format.layout.document.impl;

import :format.layout.document;
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
        Node {
            .kind = Kind::Text,
            .text = std::string(value),
            .children = {},
            .flat_width = width,
            .breakable = false
        }
    );
}

auto FormattingDocument::verbatim(std::string_view value) noexcept -> FormattingNodeID {
    return append(
        Node {
            .kind = Kind::Verbatim,
            .text = std::string(value),
            .children = {},
            .flat_width = flat_width(value),
            .breakable = value.find_first_of("\r\n") != std::string_view::npos
        }
    );
}

auto FormattingDocument::concat(std::vector<FormattingNodeID> children) noexcept
    -> FormattingNodeID {
    auto width = std::optional<std::size_t>(0);
    auto breakable = false;
    for (const auto child : children) {
        const auto child_width = node(child).flat_width;
        width =
            width && child_width ? std::optional(add_width(*width, *child_width)) : std::nullopt;
        breakable = breakable || node(child).breakable;
    }
    return append(
        Node {
            .kind = Kind::Concat,
            .text = {},
            .children = std::move(children),
            .flat_width = width,
            .breakable = breakable
        }
    );
}

auto FormattingDocument::line(bool space_when_flat) noexcept -> FormattingNodeID {
    return append(
        Node {
            .kind = Kind::Line,
            .text = space_when_flat ? " " : "",
            .children = {},
            .flat_width = space_when_flat ? 1uz : 0uz,
            .breakable = true
        }
    );
}

auto FormattingDocument::hardline() noexcept -> FormattingNodeID {
    return append(
        Node {
            .kind = Kind::HardLine,
            .text = {},
            .children = {},
            .flat_width = std::nullopt,
            .breakable = true
        }
    );
}

auto FormattingDocument::indent(FormattingNodeID child) noexcept -> FormattingNodeID {
    const auto width = node(child).flat_width;
    return append(
        Node {
            .kind = Kind::Indent,
            .text = {},
            .children = {child},
            .flat_width = width,
            .breakable = node(child).breakable
        }
    );
}

auto FormattingDocument::group(FormattingNodeID child) noexcept -> FormattingNodeID {
    const auto width = node(child).flat_width;
    return append(
        Node {
            .kind = Kind::Group,
            .text = {},
            .children = {child},
            .flat_width = width,
            .breakable = node(child).breakable
        }
    );
}

auto FormattingDocument::wrap(
    FormattingNodeID normal,
    FormattingNodeID attached,
    FormattingNodeID element,
    FormattingNodeID boundary,
    FormattingNodeID normal_tail
) noexcept -> FormattingNodeID {
    if (!node(boundary).flat_width) {
        return normal;
    }
    return append(
        Node {
            .kind = Kind::Wrap,
            .text = {},
            .children = {normal, attached, element, normal_tail},
            .flat_width = node(normal).flat_width,
            .breakable = node(normal).breakable
        }
    );
}

auto FormattingDocument::has_leading_break(FormattingNodeID id) const noexcept -> bool {
    const auto& value = node(id);
    switch (value.kind) {
        case Kind::Line:
        case Kind::HardLine: return true;
        case Kind::Group:
        case Kind::Indent:   return has_leading_break(value.children.front());
        case Kind::Concat:
            for (const auto child : value.children) {
                if (node(child).flat_width != 0uz || node(child).breakable) {
                    return has_leading_break(child);
                }
            }
            return false;
        default: return false;
    }
}

auto FormattingDocument::wrap_body(
    const Node& value,
    std::span<const Frame> pending,
    std::size_t remaining,
    std::size_t indentation,
    std::size_t width,
    std::size_t indent_width
) const noexcept -> FormattingNodeID {
    const auto& element = node(value.children[2]);
    const auto inset = add_width(indentation, indent_width);
    const auto fresh_width = width > inset ? width - inset : 0uz;
    // Prefer one complete element per line. When it needs its own breaks,
    // its wrapper stays attached and does not add another indentation level.
    if (!element.breakable) {
        return value.children[0];
    }
    auto following = std::vector<Frame>(pending.begin(), pending.end());
    following.push_back(Frame {.id = value.children[3], .indentation = indentation, .flat = false});
    if (element.flat_width
        && *element.flat_width <= fresh_width
        && fits(following, fresh_width - *element.flat_width, width, indent_width, std::nullopt)) {
        return value.children[0];
    }
    following.back().id = value.children[1];
    return value.children[fits(following, remaining, width, indent_width, fresh_width) ? 1uz : 0uz];
}

auto FormattingDocument::fits(
    std::span<const Frame> pending,
    std::size_t remaining,
    std::size_t width,
    std::size_t indent_width,
    std::optional<std::size_t> preserve_width
) const noexcept -> bool {
    auto work = std::vector<Frame>(pending.begin(), pending.end());
    while (!work.empty()) {
        const auto frame = work.back();
        work.pop_back();
        const auto& value = node(frame.id);
        // Attaching a wrapper must not split a group that fits on its own line.
        auto preserve = preserve_width
            && value.kind == Kind::Group
            && value.flat_width
            && *value.flat_width <= *preserve_width
            && !has_leading_break(frame.id);
        if (preserve) {
            preserve =
                fits(work, *preserve_width - *value.flat_width, width, indent_width, std::nullopt);
        }
        if ((frame.flat || preserve) && value.flat_width) {
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
            case Kind::Wrap:     {
                work.push_back(
                    Frame {
                        .id = wrap_body(
                            value,
                            work,
                            remaining,
                            frame.indentation,
                            width,
                            indent_width
                        ),
                        .indentation = frame.indentation,
                        .flat = frame.flat
                    }
                );
                break;
            }
            case Kind::Indent:
                work.push_back(
                    Frame {
                        .id = value.children.front(),
                        .indentation = frame.indentation + indent_width,
                        .flat = frame.flat
                    }
                );
                break;
            case Kind::Concat:
            case Kind::Group:
                for (const auto child : value.children | std::views::reverse) {
                    work.push_back(
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
                        && fits(
                            pending,
                            remaining - *value.flat_width,
                            width,
                            indent_width,
                            std::nullopt
                        ));
                pending.push_back(
                    Frame {
                        .id = value.children.front(),
                        .indentation = frame.indentation,
                        .flat = flat
                    }
                );
                break;
            }
            case Kind::Wrap: {
                const auto current_column = pending_indentation ? frame.indentation : column;
                pending.push_back(
                    Frame {
                        .id = wrap_body(
                            value,
                            pending,
                            width > current_column ? width - current_column : 0uz,
                            frame.indentation,
                            width,
                            indent_width
                        ),
                        .indentation = frame.indentation,
                        .flat = frame.flat
                    }
                );
                break;
            }
        }
    }
    return output;
}
