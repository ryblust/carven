module carven:semantic.semir.format;

import std;

struct FormatPart;

struct FormatText final {
    std::string bytes;
    auto operator==(const FormatText&) const noexcept -> bool = default;
};

struct FormatHole final {
    std::size_t operand_index;
    bool has_specification;
    std::vector<FormatPart> specification;
    auto operator==(const FormatHole&) const noexcept -> bool = default;
};

struct FormatPart final {
    std::variant<FormatText, FormatHole> value;
    auto operator==(const FormatPart&) const noexcept -> bool = default;
};

struct FormatSpec final {
    std::vector<FormatPart> parts;
    auto operator==(const FormatSpec&) const noexcept -> bool = default;
};
