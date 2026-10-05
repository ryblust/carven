module carven:graver.layout.document;

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
    auto render(
        FormattingNodeID root,
        std::size_t width = 100,
        std::size_t indent_width = 4
    ) const noexcept -> std::string;

private:
    enum class Kind { Text, Verbatim, Concat, Line, HardLine, Indent, Group };

    struct Node final {
        Kind kind;
        std::string text;
        std::vector<FormattingNodeID> children;
        std::optional<std::size_t> flat_width;
    };

    struct Frame final {
        FormattingNodeID id;
        std::size_t indentation;
        bool flat;
    };

    auto append(Node node) noexcept -> FormattingNodeID;
    auto node(FormattingNodeID id) const noexcept -> const Node&;
    auto fits(std::span<const Frame> pending, std::size_t remaining) const noexcept -> bool;
    std::vector<Node> nodes;
};
