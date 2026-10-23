module carven:format.layout.document;

import std;

struct FormattingNodeID final {
    std::size_t index;
};

// IDs belong to this document. Each operation only accepts earlier nodes.
class FormattingDocument final {
public:
    auto text(std::string_view value) noexcept -> FormattingNodeID;
    auto verbatim(std::string_view value) noexcept -> FormattingNodeID;
    auto concat(std::vector<FormattingNodeID> children) noexcept -> FormattingNodeID;
    auto line(bool space_when_flat) noexcept -> FormattingNodeID;
    auto hardline() noexcept -> FormattingNodeID;
    auto indent(FormattingNodeID child) noexcept -> FormattingNodeID;
    auto group(FormattingNodeID child) noexcept -> FormattingNodeID;

    // Prefer a complete element on its own line; otherwise let it own the breaks.
    // normal_tail follows the element in the normal layout, before the outer suffix.
    auto wrap(
        FormattingNodeID normal,
        FormattingNodeID attached,
        FormattingNodeID element,
        FormattingNodeID boundary,
        FormattingNodeID normal_tail
    ) noexcept -> FormattingNodeID;

    auto render(FormattingNodeID root, std::size_t width, std::size_t indent_width) const noexcept
        -> std::string;

private:
    enum class Kind { Text, Verbatim, Concat, Line, HardLine, Indent, Group, Wrap };

    struct Node final {
        Kind kind;
        std::string text;
        std::vector<FormattingNodeID> children;
        std::optional<std::size_t> flat_width;
        bool breakable;
    };

    struct Frame final {
        FormattingNodeID id;
        std::size_t indentation;
        bool flat;
    };

    auto append(Node node) noexcept -> FormattingNodeID;
    auto node(FormattingNodeID id) const noexcept -> const Node&;
    auto has_leading_break(FormattingNodeID id) const noexcept -> bool;

    auto wrap_body(
        const Node& value,
        std::span<const Frame> pending,
        std::size_t remaining,
        std::size_t indentation,
        std::size_t width,
        std::size_t indent_width
    ) const noexcept -> FormattingNodeID;

    auto fits(
        std::span<const Frame> pending,
        std::size_t remaining,
        std::size_t width,
        std::size_t indent_width,
        std::optional<std::size_t> preserve_width
    ) const noexcept -> bool;

    std::vector<Node> nodes;
};
