export module carven.driver.report;

import carven.common.source;
import carven.frontend.parser;
import std;

export auto report_errors(std::span<const ParseError> errors, std::string_view source, std::string_view filepath) noexcept -> void {
    if (errors.empty()) return;

    const auto line_offsets = LineOffsets(source);

    for (auto i = 0uz; i < errors.size(); ++i) {
        if (i > 0) std::println();

        const auto& error = errors[i];
        const auto location = line_offsets.location(error.span.start);

        std::println("error: {}:{}: {}", filepath, location, error.message);

        auto line_start = error.span.start;
        while (line_start > 0 && source[line_start - 1] != '\n') --line_start;

        auto line_end = error.span.start;
        while (line_end < source.size() && source[line_end] != '\n') ++line_end;

        const auto line_text = slice(source, { .start = line_start, .end = line_end });

        std::println("{:>4} | {}", location.line, line_text);

        const auto column = static_cast<std::size_t>(location.column) - 1;
        const auto span_len = std::max(1u, error.span.end > error.span.start ? error.span.end - error.span.start : 1u);

        std::print("     | ");
        for (auto col = 0uz; col < column; ++col) std::print(" ");
        for (auto pos = 0uz; pos < span_len && column + pos < line_end - line_start; ++pos) std::print("^");
        std::println();
    }
}
