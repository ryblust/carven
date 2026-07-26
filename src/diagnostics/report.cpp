module carven:diagnostics.report.impl;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :diagnostics.report;
import :source.location;
import :source.manager;
import :source.text;
import :support.utf8;
import std;

namespace {

constexpr auto tab_width = 4uz;

struct NormalizedSpan final {
    std::size_t start;
    std::size_t end;
};

struct SourceLine final {
    std::size_t begin;
    std::size_t end;
    std::size_t number;
};

struct DisplayLine final {
    std::string text;
    std::vector<std::size_t> columns;
};

struct LabelFrame final {
    const DiagnosticLabel* label;
    NormalizedSpan span;
    char marker;
    std::size_t order;
    std::size_t first_line;
    std::size_t last_line;
};

auto normalize_span(Span span, std::size_t source_size) noexcept -> NormalizedSpan {
    const auto start = std::min<std::size_t>(span.start(), source_size);
    const auto end = std::min<std::size_t>(span.end(), source_size);
    return {.start = start, .end = std::max(start, end)};
}

auto source_lines(std::string_view text) noexcept -> std::vector<SourceLine> {
    auto lines = std::vector<SourceLine> {};
    lines.reserve(text.size() / 40 + 1);

    auto begin = 0uz;
    auto number = 1uz;
    for (auto index = 0uz; index < text.size(); ++index) {
        if (text[index] != '\n') {
            continue;
        }

        auto end = index;
        if (end > begin && text[end - 1] == '\r') {
            --end;
        }
        lines.push_back({.begin = begin, .end = end, .number = number++});
        begin = index + 1;
    }

    lines.push_back({.begin = begin, .end = text.size(), .number = number});
    return lines;
}

auto line_containing(std::span<const SourceLine> lines, std::size_t offset) noexcept
    -> std::size_t {
    auto result = 0uz;
    for (auto index = 1uz; index < lines.size(); ++index) {
        if (lines[index].begin > offset) {
            break;
        }
        result = index;
    }
    return result;
}

auto display_line(std::string_view text) noexcept -> DisplayLine {
    static constexpr auto hexadecimal = std::string_view("0123456789abcdef");

    auto result = DisplayLine {
        .text = {},
        .columns = std::vector<std::size_t>(text.size() + 1),
    };
    result.text.reserve(text.size());

    auto index = 0uz;
    auto column = 0uz;
    while (index < text.size()) {
        result.columns[index] = column;
        const auto byte = static_cast<unsigned char>(text[index]);
        if (byte == '\t') {
            const auto width = tab_width - column % tab_width;
            result.text.append(width, ' ');
            column += width;
            ++index;
            result.columns[index] = column;
            continue;
        }

        const auto sequence = UTF8Decoder::decode(text, index);
        if (!sequence.valid) {
            result.text += "\\x";
            result.text += hexadecimal[byte >> 4];
            result.text += hexadecimal[byte & 0x0f];
            column += 4;
            ++index;
            result.columns[index] = column;
            continue;
        }

        if (sequence.scalar < 0x20 || sequence.scalar == 0x7f) {
            result.text += "\\x";
            result.text += hexadecimal[byte >> 4];
            result.text += hexadecimal[byte & 0x0f];
            column += 4;
            ++index;
            result.columns[index] = column;
            continue;
        }

        result.text.append(text.substr(index, sequence.width));
        for (auto continuation = 1uz; continuation < sequence.width; ++continuation) {
            result.columns[index + continuation] = column;
        }
        index += sequence.width;
        ++column;
        result.columns[index] = column;
    }

    result.columns[text.size()] = column;
    return result;
}

auto decimal_width(std::size_t value) noexcept -> std::size_t {
    auto width = 1uz;
    while (value >= 10) {
        value /= 10;
        ++width;
    }
    return width;
}

auto append_separator(std::string& output, std::size_t gutter_width) noexcept -> void {
    output.append(gutter_width, ' ');
    output += " |\n";
}

auto append_ellipsis(std::string& output, std::size_t gutter_width) noexcept -> void {
    output.append(gutter_width, ' ');
    output += " | ...\n";
}

auto append_source_line(
    std::string& output,
    std::string_view text,
    const SourceLine& line,
    std::size_t gutter_width,
    std::size_t marker_begin,
    std::size_t marker_end,
    char marker,
    std::string_view message
) noexcept -> void {
    const auto raw = text.substr(line.begin, line.end - line.begin);
    const auto displayed = display_line(raw);
    const auto number = std::to_string(line.number);

    output.append(gutter_width - number.size(), ' ');
    output += number;
    output += " | ";
    output += displayed.text;
    output += '\n';

    const auto relative_begin = std::min(marker_begin - line.begin, raw.size());
    const auto relative_end = std::min(marker_end - line.begin, raw.size());
    const auto first_column = displayed.columns[relative_begin];
    const auto last_column = displayed.columns[std::max(relative_begin, relative_end)];

    output.append(gutter_width, ' ');
    output += " | ";
    output.append(first_column, ' ');
    output.append(std::max<std::size_t>(1, last_column - first_column), marker);
    if (!message.empty()) {
        output += ' ';
        output += message;
    }
    output += '\n';
}

auto append_frame(
    std::string& output,
    std::string_view text,
    std::span<const SourceLine> lines,
    const LabelFrame& frame,
    std::size_t gutter_width
) noexcept -> void {
    const auto& first = lines[frame.first_line];
    if (frame.first_line == frame.last_line) {
        append_source_line(
            output,
            text,
            first,
            gutter_width,
            frame.span.start,
            frame.span.end,
            frame.marker,
            frame.label->message
        );
        return;
    }

    append_source_line(
        output,
        text,
        first,
        gutter_width,
        frame.span.start,
        first.end,
        frame.marker,
        {}
    );
    if (frame.last_line > frame.first_line + 1) {
        append_ellipsis(output, gutter_width);
    }

    const auto& last = lines[frame.last_line];
    append_source_line(
        output,
        text,
        last,
        gutter_width,
        last.begin,
        frame.span.end,
        frame.marker,
        frame.label->message
    );
}

} // namespace

auto render_diagnostic(const Diagnostic& diagnostic, const SourceManager& sources) noexcept
    -> std::string {
    const auto& finding = diagnostic.finding;
    const auto& attachment = diagnostic.attachment;
    const auto* severity = finding.severity == DiagnosticSeverity::Warning ? "warning" : "error";
    if (!attachment.primary.has_value()) {
        auto output = std::format(
            "{} [{}]: {}\n",
            severity,
            diagnostic_code_info(finding.code).name,
            finding.message
        );
        for (const auto& note : attachment.notes) {
            output += std::format("note: {}\n", note.message);
        }
        return output;
    }
    const auto& primary = *attachment.primary;
    auto source_order = std::vector<SourceID> {primary.span.source_id};
    for (const auto& label : attachment.related) {
        if (!std::ranges::contains(source_order, label.span.source_id)) {
            source_order.push_back(label.span.source_id);
        }
    }

    auto output = std::format(
        "{} [{}]: {}\n",
        severity,
        diagnostic_code_info(finding.code).name,
        finding.message
    );
    for (auto source_index = 0uz; source_index < source_order.size(); ++source_index) {
        const auto source_id = source_order[source_index];
        const auto source = sources.view(source_id);
        const auto lines = source_lines(source.text);
        auto frames = std::vector<LabelFrame> {};
        frames.reserve(attachment.related.size() + 1);

        const auto collect_frame =
            [&](const DiagnosticLabel& label, char marker, std::size_t order) noexcept {
                if (label.span.source_id != source_id) {
                    return;
                }
                const auto span = normalize_span(label.span.span, source.text.size());
                const auto last_offset = span.end > span.start ? span.end - 1 : span.start;
                frames.push_back({
                    .label = &label,
                    .span = span,
                    .marker = marker,
                    .order = order,
                    .first_line = line_containing(lines, span.start),
                    .last_line = line_containing(lines, last_offset),
                });
            };
        collect_frame(primary, '^', 0);
        for (auto index = 0uz; index < attachment.related.size(); ++index) {
            collect_frame(attachment.related[index], '-', index + 1);
        }
        std::ranges::sort(
            frames,
            [](const LabelFrame& left, const LabelFrame& right) static noexcept -> bool {
                if (left.span.start != right.span.start) {
                    return left.span.start < right.span.start;
                }
                if (left.span.end != right.span.end) {
                    return left.span.end < right.span.end;
                }
                return left.order < right.order;
            }
        );

        const auto anchor = source_index == 0 ? primary.span : frames.front().label->span;
        const auto location = sources.location(anchor);

        if (source_index == 0) {
            output += std::format(" --> {}:{}:{}\n", source.origin, location.line, location.column);
        } else {
            output += std::format(" ::: {}:{}:{}\n", source.origin, location.line, location.column);
        }

        auto largest_line = 1uz;
        for (const auto& frame : frames) {
            largest_line = std::max(largest_line, lines[frame.last_line].number);
        }
        const auto gutter_width = decimal_width(largest_line);
        append_separator(output, gutter_width);
        for (auto index = 0uz; index < frames.size(); ++index) {
            if (index > 0) {
                append_separator(output, gutter_width);
            }
            append_frame(output, source.text, lines, frames[index], gutter_width);
        }
    }
    for (const auto& note : attachment.notes) {
        output += std::format("note: {}\n", note.message);
    }
    return output;
}

auto render_diagnostics(
    std::span<const Diagnostic> diagnostics,
    const SourceManager& sources
) noexcept -> std::string {
    auto output = std::string {};
    for (auto index = 0uz; index < diagnostics.size(); ++index) {
        if (index > 0) {
            output += '\n';
        }
        output += render_diagnostic(diagnostics[index], sources);
    }
    return output;
}
