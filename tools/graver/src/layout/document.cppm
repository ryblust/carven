module carven:graver.layout.document;

import std;

namespace graver {

struct DocID final {
    std::size_t index;
};

// IDs belong to this document. Each operation only accepts earlier nodes.
class Document final {
public:
    auto text(std::string_view value) noexcept -> DocID;
    auto verbatim(std::string_view value) noexcept -> DocID;
    auto concat(std::vector<DocID> children) noexcept -> DocID;
    auto line(bool space_when_flat) noexcept -> DocID;
    auto hardline() noexcept -> DocID;
    auto indent(DocID child) noexcept -> DocID;
    auto group(DocID child) noexcept -> DocID;
    auto render(DocID root, std::size_t width = 100, std::size_t indent_width = 4) const noexcept
        -> std::string;

private:
    enum class Kind { Text, Verbatim, Concat, Line, HardLine, Indent, Group };

    struct Node final {
        Kind kind;
        std::string text;
        std::vector<DocID> children;
        std::optional<std::size_t> flat_width;
    };

    struct Frame final {
        DocID id;
        std::size_t indentation;
        bool flat;
    };

    auto append(Node node) noexcept -> DocID;
    auto node(DocID id) const noexcept -> const Node&;
    auto fits(std::span<const Frame> pending, std::size_t remaining) const noexcept -> bool;
    std::vector<Node> nodes;
};

}
