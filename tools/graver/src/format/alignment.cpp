module carven:graver.format.alignment.impl;

import :diagnostics.diagnosed;
import :frontend.ast.expr;
import :frontend.ast.tree;
import :frontend.lex;
import :frontend.lex.token;
import :graver.format.alignment;
import :graver.source;
import :source.text;
import std;

namespace {

struct ArrayRow final {
    std::string signature;
    std::vector<std::size_t> columns;
    std::size_t line_start;
    std::size_t line_end;
};

struct Padding final {
    std::size_t offset;
    std::size_t count;
};

} // namespace

namespace graver {

auto align_array_rows(
    const Source& source,
    ASTView syntax,
    std::string output,
    std::size_t width
) noexcept -> std::string {
    const auto scanned =
        lex(SourceView {
            .source_id = source.token_buffer().source_id(),
            .text = output,
            .origin = "graver alignment",
        });
    const auto original = source.token_buffer().tokens();
    const auto rendered = scanned.value.tokens();
    if (has_errors(scanned) || original.size() != rendered.size()) {
        return output; // The format boundary diagnoses invalid output.
    }
    const auto index_at = [&](std::uint32_t offset) noexcept {
        return static_cast<std::size_t>(
            std::ranges::lower_bound(
                original,
                offset,
                {},
                [](const Token& token) static noexcept { return token.span.start(); }
            )
            - original.begin()
        );
    };
    const auto spelling = [&](Span span) noexcept {
        const auto first = index_at(span.start());
        const auto after = index_at(span.end());
        return std::string_view(output).substr(
            rendered[first].span.start(),
            rendered[after - 1uz].span.end() - rendered[first].span.start()
        );
    };
    const auto row_for = [&](ASTExprID id) noexcept -> std::optional<ArrayRow> {
        const auto& expression = syntax.expression(id);
        const auto* construction = std::get_if<ASTConstructionExpr>(&expression.value);
        if (construction == nullptr) {
            return std::nullopt;
        }
        const auto first = index_at(expression.span.start());
        const auto after = index_at(expression.span.end());
        const auto begin = rendered[first].span.start();
        const auto end = rendered[after - 1uz].span.end();
        if (std::string_view(output).substr(begin, end - begin).find_first_of("\r\n")
            != std::string_view::npos) {
            return std::nullopt;
        }
        for (auto index = first + 1uz; index < after; ++index) {
            for (const auto trivia : source.trivia_before(index)) {
                if (trivia.kind == TriviaKind::LineComment) {
                    return std::nullopt;
                }
            }
        }
        const auto trailing = after < original.size() && original[after].kind == TokenKind::Comma
            ? after + 1uz
            : after;
        for (const auto trivia : source.trivia_before(trailing)) {
            if (trivia.kind == TriviaKind::LineEnding) {
                break;
            }
            if (trivia.kind == TriviaKind::LineComment) {
                return std::nullopt;
            }
        }
        const auto newline = output.rfind('\n', begin);
        const auto line_start = newline == std::string::npos ? 0uz : newline + 1uz;
        const auto next_line = output.find('\n', end);
        auto row = ArrayRow {
            .signature = std::string(spelling(construction->type.span)),
            .columns = {},
            .line_start = line_start,
            .line_end = next_line == std::string::npos ? output.size() : next_line,
        };
        std::visit(
            [&](const auto& initializer) noexcept {
                using T = std::decay_t<decltype(initializer)>;
                if constexpr (std::same_as<T, ASTPositionalInitializerList>) {
                    row.signature += "#positional";
                    for (const auto value : initializer.values) {
                        row.columns.push_back(
                            rendered[index_at(syntax.expression(value).span.start())].span.start()
                        );
                    }
                } else if constexpr (std::same_as<T, ASTFieldInitializerList>) {
                    row.signature += "#named";
                    for (const auto& field : initializer.fields) {
                        row.signature += ':';
                        row.signature += spelling(field.name_span);
                        row.columns.push_back(rendered[index_at(field.span.start())].span.start());
                    }
                }
            },
            construction->initializer.value
        );
        if (row.columns.empty()) {
            return std::nullopt;
        }
        row.columns.push_back(rendered[after - 1uz].span.start());
        return row;
    };
    auto padding = std::vector<Padding>();
    const auto align = [&](std::span<const ArrayRow> rows) noexcept {
        if (rows.size() < 2uz) {
            return;
        }
        auto widths = std::vector<std::size_t>(rows.front().columns.size() - 1uz, 0uz);
        for (const auto& row : rows) {
            for (auto column = 0uz; column < widths.size(); ++column) {
                widths[column] =
                    std::max(widths[column], row.columns[column + 1uz] - row.columns[column]);
            }
        }
        for (const auto& row : rows) {
            auto length = row.line_end - row.line_start;
            for (auto column = 0uz; column < widths.size(); ++column) {
                length += widths[column] - (row.columns[column + 1uz] - row.columns[column]);
            }
            if (length > width) {
                return;
            }
        }
        for (const auto& row : rows) {
            for (auto column = 0uz; column < widths.size(); ++column) {
                const auto count =
                    widths[column] - (row.columns[column + 1uz] - row.columns[column]);
                if (count != 0uz) {
                    padding.push_back(
                        Padding {.offset = row.columns[column + 1uz], .count = count}
                    );
                }
            }
        }
    };
    for (const auto& expression : syntax.expressions()) {
        const auto* array = std::get_if<ASTArrayExpr>(&expression.value);
        if (array == nullptr) {
            continue;
        }
        auto rows = std::vector<ArrayRow>();
        for (const auto id : array->element_ids) {
            auto row = row_for(id);
            if (!row
                || (!rows.empty()
                    && (row->line_start != rows.back().line_end + 1uz
                        || row->signature != rows.back().signature
                        || row->columns.size() != rows.back().columns.size()))) {
                align(rows);
                rows.clear();
            }
            if (row) {
                rows.push_back(std::move(*row));
            }
        }
        align(rows);
    }
    if (padding.empty()) {
        return output;
    }
    std::ranges::sort(padding, {}, &Padding::offset);
    auto aligned = std::string();
    auto previous = 0uz;
    for (const auto entry : padding) {
        aligned.append(output, previous, entry.offset - previous);
        aligned.append(entry.count, ' ');
        previous = entry.offset;
    }
    aligned.append(output, previous, output.size() - previous);
    return aligned;
}

}
