module carven:backend.emission.layout;

import std;

struct LayoutNodeID final {
    std::size_t value;
    auto operator==(const LayoutNodeID&) const noexcept -> bool = default;
};

struct LayoutText final {
    std::string value;
};

struct LayoutRaw final {
    std::string bytes;
};

struct LayoutLine final {};

struct LayoutSourceLocation final {
    std::size_t line;
    std::string origin_literal;
};

struct LayoutGeneratedLocation final {
    std::string origin_literal;
};

struct LayoutConcat final {
    std::vector<LayoutNodeID> children;
};

struct LayoutIndent final {
    std::size_t width;
    LayoutNodeID child;
};

struct LayoutChoice final {
    std::vector<LayoutNodeID> alternatives;
};

struct LayoutFlatten final {
    LayoutNodeID child;
};

struct LayoutResetIndent final {
    LayoutNodeID child;
};

using LayoutNodeValue = std::variant<
    LayoutText,
    LayoutRaw,
    LayoutLine,
    LayoutSourceLocation,
    LayoutGeneratedLocation,
    LayoutConcat,
    LayoutIndent,
    LayoutChoice,
    LayoutFlatten,
    LayoutResetIndent>;

struct LayoutNode final {
    LayoutNodeValue value;
};

class LayoutDocument final {
public:
    LayoutDocument(const LayoutDocument&) = delete;
    LayoutDocument(LayoutDocument&&) = default;

    auto operator=(const LayoutDocument&) -> LayoutDocument& = delete;
    auto operator=(LayoutDocument&&) -> LayoutDocument& = default;

private:
    friend class LayoutBuilder;
    friend auto render_layout(const LayoutDocument&, std::size_t) noexcept -> std::string;

    LayoutDocument(std::vector<LayoutNode> nodes, LayoutNodeID root) noexcept;

    std::vector<LayoutNode> nodes;
    LayoutNodeID root_id;
};

class LayoutBuilder final {
public:
    LayoutBuilder() noexcept;

    auto empty() const noexcept -> LayoutNodeID;
    auto text(std::string_view value) noexcept -> LayoutNodeID;
    auto raw(std::string_view bytes) noexcept -> LayoutNodeID;
    auto line() noexcept -> LayoutNodeID;
    auto source_location(std::size_t line, std::string origin_literal) noexcept -> LayoutNodeID;
    auto generated_location(std::string origin_literal) noexcept -> LayoutNodeID;
    auto concat(std::vector<LayoutNodeID> children) noexcept -> LayoutNodeID;
    auto join(std::span<const LayoutNodeID> children, LayoutNodeID separator) noexcept
        -> LayoutNodeID;
    auto indent(std::size_t width, LayoutNodeID child) noexcept -> LayoutNodeID;
    auto choice(std::span<const LayoutNodeID> alternatives) noexcept -> LayoutNodeID;
    auto flatten(LayoutNodeID child) noexcept -> LayoutNodeID;
    auto reset_indent(LayoutNodeID child) noexcept -> LayoutNodeID;
    auto finish(LayoutNodeID root) && noexcept -> LayoutDocument;

private:
    auto add(LayoutNodeValue value) noexcept -> LayoutNodeID;

    std::vector<LayoutNode> nodes;
    LayoutNodeID empty_id;
};

auto render_layout(const LayoutDocument& document, std::size_t line_width = 100) noexcept
    -> std::string;
