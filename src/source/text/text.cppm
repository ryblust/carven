module carven:source.text;

import :support.invariant;
import :support.typed_id;
import std;

struct SourceIDTag final {};

using SourceID = TypedID<SourceIDTag>;

struct SourceView final {
    SourceID source_id;
    std::string_view text;
    std::string_view origin;
};

class Span final {
public:
    static constexpr auto at(std::uint32_t offset) noexcept -> Span { return Span(offset, offset); }

    static constexpr auto from_bounds(std::uint32_t start, std::uint32_t end) noexcept -> Span {
        if (start > end) {
            invariant_violation("source span starts after it ends");
        }
        return Span(start, end);
    }

    constexpr auto start() const noexcept -> std::uint32_t { return span_start; }

    constexpr auto end() const noexcept -> std::uint32_t { return span_end; }

    constexpr auto size() const noexcept -> std::uint32_t { return span_end - span_start; }

    constexpr auto empty() const noexcept -> bool { return span_start == span_end; }

    constexpr auto operator<=>(const Span&) const noexcept = default;

private:
    explicit constexpr Span(std::uint32_t start, std::uint32_t end) noexcept
        : span_start(start),
          span_end(end) {}

    std::uint32_t span_start;
    std::uint32_t span_end;
};

struct SourceSpan final {
    SourceID source_id;
    Span span;
};

struct SourceLoadError final {
    std::string origin;
    std::string message;
};

auto locate(SourceID source_id, Span span) noexcept -> SourceSpan;

auto try_slice(std::string_view text, Span span) noexcept -> std::optional<std::string_view>;

auto slice(std::string_view text, Span span) noexcept -> std::string_view;
