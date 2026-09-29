module carven:frontend.dump.text.impl;

import :frontend.dump.text;
import :support.quote;
import std;

auto append_dump_line(
    std::string& output,
    std::string_view prefix,
    bool is_last,
    std::string_view label
) noexcept -> void {
    output += prefix;
    output += is_last ? "└─ " : "├─ ";
    output += label;
    output += '\n';
}

auto format_dump_span(Span span) noexcept -> std::string {
    return std::format("[{}, {})", span.start(), span.end());
}

auto format_source_label(std::string_view source_text, Span span) noexcept -> std::string {
    return std::format("{} {}", format_dump_span(span), quote_text(slice(source_text, span)));
}
