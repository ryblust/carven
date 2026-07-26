module carven:frontend.dump.text;

import :source.text;
import std;

auto escape_dump_text(std::string_view text) noexcept -> std::string;
auto quote_dump_text(std::string_view text) noexcept -> std::string;
auto format_dump_span(Span span) noexcept -> std::string;
auto format_source_label(std::string_view source_text, Span span) noexcept -> std::string;
auto append_dump_line(
    std::string& output,
    std::string_view prefix,
    bool is_last,
    std::string_view label
) noexcept -> void;
