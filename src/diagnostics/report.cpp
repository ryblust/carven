module carven:diagnostics.report.impl;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :diagnostics.report;
import :source.location;
import :source.manager;
import :source.text;
import :support.terminal;
import :support.utf8;
import std;

namespace {

constexpr auto tab_width = 4uz;

struct NormalizedSpan final {
    std::size_t start;
    std::size_t end;
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
    std::uint32_t first_line;
    std::uint32_t last_line;
};

auto normalize_span(Span span, std::size_t source_size) noexcept -> NormalizedSpan {
    const auto start = std::min<std::size_t>(span.start(), source_size);
    const auto end = std::min<std::size_t>(span.end(), source_size);
    return {.start = start, .end = std::max(start, end)};
}

// Estimate terminal cells for common wide and combining scalars.
auto terminal_columns(char32_t scalar) noexcept -> std::size_t {
    struct ScalarRange final {
        char32_t first;
        char32_t last;
    };

    static constexpr auto combining = std::array {
        ScalarRange {.first = 0x0300, .last = 0x036f},
        ScalarRange {.first = 0x200b, .last = 0x200d},
        ScalarRange {.first = 0xfe00, .last = 0xfe0f},
    };
    static constexpr auto wide = std::array {
        ScalarRange {.first = 0x1100, .last = 0x115f},
        ScalarRange {.first = 0x2e80, .last = 0xa4cf},
        ScalarRange {.first = 0xac00, .last = 0xd7a3},
        ScalarRange {.first = 0xf900, .last = 0xfaff},
        ScalarRange {.first = 0xfe30, .last = 0xfe4f},
        ScalarRange {.first = 0xff00, .last = 0xff60},
        ScalarRange {.first = 0xffe0, .last = 0xffe6},
        ScalarRange {.first = 0x1f300, .last = 0x1faff},
        ScalarRange {.first = 0x20000, .last = 0x3fffd},
    };
    const auto contains = [scalar](const ScalarRange& range) noexcept -> bool {
        return scalar >= range.first && scalar <= range.last;
    };
    if (std::ranges::any_of(combining, contains)) {
        return 0uz;
    }
    return std::ranges::any_of(wide, contains) ? 2uz : 1uz;
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
        column += terminal_columns(sequence.scalar);
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
    Span line,
    std::uint32_t line_number,
    std::size_t gutter_width,
    std::size_t marker_begin,
    std::size_t marker_end,
    char marker,
    std::string_view message,
    bool show_source
) noexcept -> void {
    auto raw = slice(text, line);
    if (raw.ends_with('\n')) {
        raw.remove_suffix(1);
        if (raw.ends_with('\r')) {
            raw.remove_suffix(1);
        }
    }
    const auto displayed = display_line(raw);
    const auto number = std::to_string(line_number);

    if (show_source) {
        output.append(gutter_width - number.size(), ' ');
        output += number;
        output += " | ";
        output += displayed.text;
        output += '\n';
    }

    const auto relative_begin = std::min(marker_begin - line.start(), raw.size());
    const auto relative_end = std::min(marker_end - line.start(), raw.size());
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
    const SourceManager& sources,
    SourceID source_id,
    const LabelFrame& frame,
    std::size_t gutter_width,
    bool show_source
) noexcept -> void {
    const auto first = sources.line_span(source_id, frame.first_line);
    if (frame.first_line == frame.last_line) {
        append_source_line(
            output,
            text,
            first,
            frame.first_line,
            gutter_width,
            frame.span.start,
            frame.span.end,
            frame.marker,
            frame.label->message,
            show_source
        );
        return;
    }

    append_source_line(
        output,
        text,
        first,
        frame.first_line,
        gutter_width,
        frame.span.start,
        first.end(),
        frame.marker,
        {},
        true
    );
    if (frame.last_line > frame.first_line + 1) {
        append_ellipsis(output, gutter_width);
    }

    const auto last = sources.line_span(source_id, frame.last_line);
    append_source_line(
        output,
        text,
        last,
        frame.last_line,
        gutter_width,
        last.start(),
        frame.span.end,
        frame.marker,
        frame.label->message,
        true
    );
}

auto append_advice(
    std::string& output,
    const DiagnosticAttachment& attachment,
    const TerminalStyler& styler
) noexcept -> void {
    for (const auto& note : attachment.notes) {
        output += std::format("{} {}\n", styler.bold_cyan("note:"), note.message);
    }
    for (const auto& help : attachment.helps) {
        output += std::format("{} {}\n", styler.bold_green("help:"), help);
    }
}

} // namespace

auto render_diagnostic(
    const Diagnostic& diagnostic,
    const SourceManager& sources,
    bool use_color
) noexcept -> std::string {
    const auto& finding = diagnostic.finding;
    const auto& attachment = diagnostic.attachment;
    const auto* severity = finding.severity == DiagnosticSeverity::Warning ? "warning" : "error";
    const auto styler = TerminalStyler(use_color);
    const auto styled_severity = finding.severity == DiagnosticSeverity::Warning
        ? styler.bold_yellow(severity)
        : styler.bold_red(severity);
    const auto styled_code = styler.bold(diagnostic_code_info(finding.code).name);
    if (!attachment.primary.has_value()) {
        auto output = std::format("{} [{}]: {}\n", styled_severity, styled_code, finding.message);
        append_advice(output, attachment, styler);
        return output;
    }
    const auto& primary = *attachment.primary;
    auto source_order = std::vector<SourceID> {primary.span.source_id};
    for (const auto& label : attachment.related) {
        if (!std::ranges::contains(source_order, label.span.source_id)) {
            source_order.push_back(label.span.source_id);
        }
    }

    auto output = std::format("{} [{}]: {}\n", styled_severity, styled_code, finding.message);
    for (auto source_index = 0uz; source_index < source_order.size(); ++source_index) {
        const auto source_id = source_order[source_index];
        const auto source = sources.view(source_id);
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
                    .first_line =
                        sources
                            .location(
                                locate(source_id, Span::at(static_cast<std::uint32_t>(span.start)))
                            )
                            .line,
                    .last_line =
                        sources
                            .location(
                                locate(source_id, Span::at(static_cast<std::uint32_t>(last_offset)))
                            )
                            .line,
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
            output += std::format(
                " {} {}:{}:{}\n",
                styler.bold_cyan("-->"),
                source.origin,
                location.line,
                location.column
            );
        } else {
            output += std::format(
                " {} {}:{}:{}\n",
                styler.bold_cyan(":::"),
                source.origin,
                location.line,
                location.column
            );
        }

        auto largest_line = 1uz;
        for (const auto& frame : frames) {
            largest_line = std::max(largest_line, static_cast<std::size_t>(frame.last_line));
        }
        const auto gutter_width = decimal_width(largest_line);
        append_separator(output, gutter_width);
        for (auto index = 0uz; index < frames.size(); ++index) {
            // Labels on one source line share its text and stack their markers.
            const auto single_line = [](const LabelFrame& frame) static noexcept -> bool {
                return frame.first_line == frame.last_line;
            };
            const auto shares_line = index > 0
                && single_line(frames[index])
                && single_line(frames[index - 1])
                && frames[index].first_line == frames[index - 1].first_line;
            if (index > 0 && !shares_line) {
                append_separator(output, gutter_width);
            }
            append_frame(
                output,
                source.text,
                sources,
                source_id,
                frames[index],
                gutter_width,
                !shares_line
            );
        }
    }
    append_advice(output, attachment, styler);
    return output;
}

auto render_diagnostics(
    std::span<const Diagnostic> diagnostics,
    const SourceManager& sources,
    bool use_color
) noexcept -> std::string {
    auto output = std::string {};
    for (auto index = 0uz; index < diagnostics.size(); ++index) {
        if (index > 0) {
            output += '\n';
        }
        output += render_diagnostic(diagnostics[index], sources, use_color);
    }
    return output;
}
