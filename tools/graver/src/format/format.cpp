module carven:graver.format.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.interop;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :frontend.ast.type;
import :frontend.lex.token;
import :frontend.lex;
import :frontend.parse;
import :graver.format.alignment;
import :graver.format;
import :graver.layout.document;
import :graver.source;
import :source.manager;
import :source.text;
import :support.invariant;
import std;

namespace {

constexpr auto line_width = 100uz;
constexpr auto indent_width = 4uz;

using graver::DocID;
using graver::Document;
using graver::Source;
using graver::TriviaKind;

auto failure(SourceID source_id, Span span, std::string message) noexcept -> Diagnostics {
    auto result = Diagnostics();
    result.push_back(DiagnosticBuilder(DiagnosticCode::Syntax, std::move(message))
                         .primary(locate(source_id, span))
                         .build());
    return result;
}

enum class Separation { None, Space, SoftEmpty, SoftSpace, Hard };
// Block keeps an empty body inline; Expanded also breaks an empty body.
enum class BlockLayout { None, Block, Compact, Expanded };

class SyntaxFormatter final {
public:
    SyntaxFormatter(const Source& source_value, ASTView syntax_value) noexcept;
    auto format() noexcept -> std::string;

private:
    const Source& source;
    ASTView syntax;
    std::span<const Token> tokens;
    std::vector<bool> prefix_operators;
    std::vector<bool> infix_operators;
    std::vector<bool> continuation_groups;
    std::vector<BlockLayout> block_layouts;
    std::vector<std::optional<std::size_t>> header_starts;
    std::vector<std::size_t> minimum_breaks;

    struct BranchLayout final {
        std::size_t first;
        std::size_t last;
        std::optional<std::size_t> block;
    };

    std::vector<std::vector<BranchLayout>> branch_groups;
    std::vector<std::optional<Separation>> separation_before;
    std::vector<std::size_t> closing_indices;
    std::vector<std::size_t> opening_indices;
    std::vector<std::size_t> group_end_exclusive;
    Document document;
    const ASTBlock* top_level_body = nullptr;
    auto render() noexcept -> std::string;
    auto block_open(Span span) const noexcept -> std::size_t;
    auto consider_compact(Span span, std::size_t header) noexcept -> void;
    auto select_compact_blocks() noexcept -> void;
    auto token_at(std::uint32_t offset) const noexcept -> std::size_t;

    // AST end offsets can point inside the original >> token.
    auto token_covering(std::uint32_t offset) const noexcept -> std::size_t;
    auto mark_block(Span span) noexcept -> void;
    auto mark_access(const ASTAccessSyntax& access) noexcept -> void;
    auto mark_named(const ASTNamedType& named, Span span) noexcept -> void;
    auto mark_function_type(const ASTFunctionType& function) noexcept -> void;

    template<typename Form>
    auto mark_control(const Form& form) noexcept -> void {
        if constexpr (std::same_as<Form, ASTIfForm>) {
            auto branches = std::vector<BranchLayout>();
            for (const auto& branch : form.branches) {
                const auto open = block_open(syntax.branch_block(branch.body).span);
                header_starts[open] = token_at(branch.keyword_span.start());
                branches.push_back(
                    BranchLayout {
                        .first = *header_starts[open],
                        .last = closing_indices[open],
                        .block = open,
                    }
                );
            }
            if (form.else_branch) {
                const auto open = block_open(syntax.branch_block(*form.else_branch).span);
                header_starts[open] = open - 1uz;
                branches.push_back(
                    BranchLayout {
                        .first = *header_starts[open],
                        .last = closing_indices[open],
                        .block = open,
                    }
                );
            }
            branch_groups.push_back(std::move(branches));
            mark_group(form.span);
            continuation_groups[token_at(form.span.start())] = false;
        } else if constexpr (std::same_as<Form, ASTMatchForm> || std::same_as<Form, ASTTryForm>) {
            mark_block(form.span);
            block_layouts[block_open(form.span)] = BlockLayout::Expanded;
            if constexpr (std::same_as<Form, ASTTryForm>) {
                block_layouts[block_open(syntax.branch_block(form.body).span)] =
                    BlockLayout::Expanded;
            }
            auto branches = std::vector<BranchLayout>();
            for (const auto& arm : form.arms) {
                auto open = std::optional<std::size_t>();
                if (const auto* id = std::get_if<ASTBranchBlockID>(&arm.body.value)) {
                    open = block_open(syntax.branch_block(*id).span);
                    header_starts[*open] = token_at(arm.span.start());
                }
                branches.push_back(
                    BranchLayout {
                        .first = token_at(arm.span.start()),
                        .last = token_covering(arm.span.end() - 1u),
                        .block = open,
                    }
                );
                separation_before[token_at(arm.span.start())] = Separation::Hard;
                if constexpr (std::same_as<Form, ASTTryForm>) {
                    for (const auto pipe : arm.pattern.pipe_spans) {
                        infix_operators[token_at(pipe.start())] = true;
                    }
                    mark_group(arm.pattern.span);
                    continuation_groups[token_at(arm.pattern.span.start())] = false;
                }
            }
            branch_groups.push_back(std::move(branches));
        }
    }

    auto mark_group(Span span) noexcept -> void;
    auto annotate() noexcept -> void;
    auto separation(std::size_t index) const noexcept -> Separation;
    auto separator(Separation value) noexcept -> DocID;
    auto needs_separator(std::size_t index) const noexcept -> bool;
    auto gap(std::size_t index, Separation desired, bool closing) noexcept -> DocID;
    auto sequence(
        std::size_t start,
        std::size_t end,
        Separation first,
        bool inside_group = false
    ) noexcept -> DocID;
};

SyntaxFormatter::SyntaxFormatter(const Source& source_value, ASTView syntax_value) noexcept
    : source(source_value),
      syntax(syntax_value),
      tokens(source.token_buffer().tokens()),
      prefix_operators(tokens.size(), false),
      infix_operators(tokens.size(), false),
      continuation_groups(tokens.size(), true),
      block_layouts(tokens.size(), BlockLayout::None),
      header_starts(tokens.size()),
      minimum_breaks(tokens.size() + 1uz, 0),
      separation_before(tokens.size(), std::nullopt),
      closing_indices(tokens.size(), 0),
      opening_indices(tokens.size(), 0),
      group_end_exclusive(tokens.size(), 0) {
    for (const auto& item : syntax.items()) {
        if (const auto* function = std::get_if<ASTFunctionDecl>(&item.value);
            function != nullptr && function->is_implicit_entry) {
            const auto* body = std::get_if<ASTFunctionBody>(&function->implementation);
            const auto* block = body == nullptr ? nullptr : std::get_if<ASTBlockID>(&body->body);
            if (block == nullptr) {
                invariant_violation("Graver implicit entry requires a block body");
            }
            top_level_body = &syntax.block(*block);
        }
    }
}

auto SyntaxFormatter::format() noexcept -> std::string {
    auto stack = std::vector<std::size_t>();
    for (auto i = 0uz; i < tokens.size(); ++i) {
        const auto kind = tokens[i].kind;
        if (kind == TokenKind::LeftParen
            || kind == TokenKind::LeftBracket
            || kind == TokenKind::LeftBrace
            || kind == TokenKind::InterpolationStart
            || kind == TokenKind::InterpolationOpen) {
            stack.push_back(i);
        } else if (kind == TokenKind::RightParen
                   || kind == TokenKind::RightBracket
                   || kind == TokenKind::RightBrace
                   || kind == TokenKind::InterpolationEnd
                   || kind == TokenKind::InterpolationClose) {
            if (stack.empty()) {
                invariant_violation("Graver parsed syntax has unmatched delimiters");
            }
            closing_indices[stack.back()] = i;
            opening_indices[i] = stack.back();
            stack.pop_back();
        }
    }
    if (!stack.empty()) {
        invariant_violation("Graver parsed syntax has unclosed delimiters");
    }
    annotate();
    select_compact_blocks();
    // Decisions only move from compact to expanded. Observe actual layout,
    // including the header's column, before adding declaration separators.
    for (;;) {
        auto output = render();
        const auto laid_out =
            lex(SourceView {
                .source_id = source.token_buffer().source_id(),
                .text = output,
                .origin = "graver layout"
            });
        if (has_errors(laid_out) || laid_out.value.tokens().size() != tokens.size()) {
            return output; // The outer format boundary validates generated output.
        }
        const auto rendered = laid_out.value.tokens();
        const auto multiline = [&](std::size_t first, std::size_t last) noexcept {
            const auto begin = rendered[first].span.start();
            const auto end = rendered[last].span.end();
            return std::string_view(output).substr(begin, end - begin).contains('\n');
        };
        auto changed = false;
        for (auto open = 0uz; open < tokens.size(); ++open) {
            if (block_layouts[open] == BlockLayout::Compact
                && closing_indices[open] > open + 1uz
                && multiline(header_starts[open].value_or(open), closing_indices[open])) {
                block_layouts[open] = BlockLayout::Expanded;
                changed = true;
            }
        }
        for (const auto& branches : branch_groups) {
            const auto expanded =
                std::ranges::any_of(branches, [&](const BranchLayout& branch) noexcept {
                    return multiline(branch.first, branch.last)
                        || (branch.block && block_layouts[*branch.block] != BlockLayout::Compact);
                });
            if (expanded) {
                for (const auto& branch : branches) {
                    if (!branch.block) {
                        continue;
                    }
                    const auto open = *branch.block;
                    if (block_layouts[open] == BlockLayout::Compact
                        && closing_indices[open] > open + 1uz) {
                        block_layouts[open] = BlockLayout::Expanded;
                        changed = true;
                    }
                }
            }
        }
        if (changed) {
            continue;
        }

        struct TopLevelEntry final {
            std::size_t first;
            std::size_t last;
            std::size_t category;
        };

        auto entries = std::vector<TopLevelEntry>();
        const auto add = [&](Span span, std::size_t category) noexcept {
            entries.push_back(
                TopLevelEntry {
                    .first = token_at(span.start()),
                    .last = token_covering(span.end() - 1u),
                    .category = category,
                }
            );
        };
        for (const auto& item : syntax.items()) {
            const auto* function = std::get_if<ASTFunctionDecl>(&item.value);
            if (function == nullptr || !function->is_implicit_entry) {
                add(item.span, item.value.index());
            }
        }
        if (top_level_body != nullptr) {
            for (const auto id : top_level_body->statements) {
                add(syntax.statement(id).span, std::variant_size_v<decltype(ASTItem::value)> + 2uz);
            }
        }
        constexpr auto import_category = std::variant_size_v<decltype(ASTItem::value)>;
        for (const auto& item : syntax.module_imports()) {
            add(item.span, import_category);
        }
        for (const auto& item : syntax.ast_module().cpp_header_imports) {
            add(item.span, import_category);
        }
        for (const auto& item : syntax.ast_module().cpp_source_fragments) {
            add(item.form_span, import_category + 1uz);
        }
        std::ranges::sort(entries, {}, &TopLevelEntry::first);
        for (auto i = 1uz; i < entries.size(); ++i) {
            const auto& previous = entries[i - 1uz];
            const auto& current = entries[i];
            if (previous.category != current.category
                || multiline(previous.first, previous.last)
                || multiline(current.first, current.last)) {
                minimum_breaks[current.first] = 2uz;
            }
        }
        return render();
    }
}

auto SyntaxFormatter::render() noexcept -> std::string {
    document = Document();
    auto parts = std::vector<DocID> {sequence(0, tokens.size(), Separation::None)};
    parts.push_back(
        gap(tokens.size(), tokens.empty() ? Separation::None : Separation::Hard, false)
    );
    return document.render(document.concat(std::move(parts)), line_width, indent_width);
}

auto SyntaxFormatter::block_open(Span span) const noexcept -> std::size_t {
    return opening_indices[token_covering(span.end() - 1u)];
}

auto SyntaxFormatter::consider_compact(Span span, std::size_t header) noexcept -> void {
    const auto open = block_open(span);
    const auto close = closing_indices[open];
    if (!header_starts[open]) {
        header_starts[open] = header;
    }
    if (block_layouts[open] == BlockLayout::Expanded) {
        return;
    }
    for (auto i = open + 1uz; i <= close; ++i) {
        auto endings = 0uz;
        for (const auto trivia : source.trivia_before(i)) {
            if (trivia.kind == TriviaKind::LineComment) {
                return;
            }
            endings += trivia.kind == TriviaKind::LineEnding ? 1uz : 0uz;
        }
        if (endings > 1uz || (i < close && block_layouts[i] != BlockLayout::None)) {
            return;
        }
    }
    block_layouts[open] = BlockLayout::Compact;
}

auto SyntaxFormatter::select_compact_blocks() noexcept -> void {
    const auto simple = [&](const auto& statements) noexcept {
        return statements.empty()
            || (statements.size() == 1uz
                && !std::holds_alternative<ASTVariableDecl>(
                    syntax.statement(statements.front()).value
                ));
    };
    for (const auto& block : syntax.blocks()) {
        if (&block == top_level_body) {
            continue;
        }
        if (block.statements.empty()) {
            consider_compact(block.span, block_open(block.span));
        }
    }
    for (const auto& block : syntax.branch_blocks()) {
        if (simple(block.statements) && (!block.result || block.statements.empty())) {
            consider_compact(block.span, block_open(block.span));
        }
    }
    for (const auto& item : syntax.items()) {
        std::visit(
            [&](const auto& value) noexcept {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::same_as<T, ASTFunctionDecl>) {
                    if (value.is_implicit_entry) {
                        return;
                    }
                    if (const auto* body = std::get_if<ASTFunctionBody>(&value.implementation)) {
                        if (const auto* id = std::get_if<ASTBlockID>(&body->body)) {
                            header_starts[block_open(syntax.block(*id).span)] =
                                token_at(item.span.start());
                        }
                    }
                }
            },
            item.value
        );
    }
    for (const auto& expression : syntax.expressions()) {
        if (const auto* lambda = std::get_if<ASTLambdaExpr>(&expression.value)) {
            if (const auto* id = std::get_if<ASTBlockID>(&lambda->body)) {
                header_starts[block_open(syntax.block(*id).span)] =
                    token_at(expression.span.start());
            }
        }
    }
}

auto SyntaxFormatter::token_at(std::uint32_t offset) const noexcept -> std::size_t {
    const auto found =
        std::ranges::lower_bound(tokens, offset, {}, [](const Token& token) static noexcept {
            return token.span.start();
        });
    if (found == tokens.end() || found->span.start() != offset) {
        invariant_violation("Graver expected a token boundary");
    }
    return static_cast<std::size_t>(found - tokens.begin());
}

auto SyntaxFormatter::token_covering(std::uint32_t offset) const noexcept -> std::size_t {
    const auto after =
        std::ranges::upper_bound(tokens, offset, {}, [](const Token& token) static noexcept {
            return token.span.start();
        });
    if (after == tokens.begin() || (after - 1)->span.end() <= offset) {
        invariant_violation("Graver expected a covered source offset");
    }
    return static_cast<std::size_t>(after - tokens.begin() - 1);
}

auto SyntaxFormatter::mark_block(Span span) noexcept -> void {
    const auto close = token_covering(span.end() - 1u);
    auto& layout = block_layouts[opening_indices[close]];
    if (layout == BlockLayout::None) {
        layout = BlockLayout::Block;
    }
}

auto SyntaxFormatter::mark_access(const ASTAccessSyntax& access) noexcept -> void {
    if (access.marker) {
        prefix_operators[token_at(access.marker->start())] = true;
    }
}

auto SyntaxFormatter::mark_named(const ASTNamedType& named, Span span) noexcept -> void {
    if (named.arguments.empty()) {
        return;
    }
    const auto open = token_at(named.components.back().name_span.start()) + 1uz;
    const auto close = token_covering(span.end() - 1u);
    separation_before[open] = Separation::None;
    separation_before[open + 1uz] = Separation::None;
    separation_before[close] = Separation::None;
    // Do not break inside angle lists yet: a single lexer token may close
    // several nested types. Other containers still wrap around the type.
    for (auto i = open + 1uz; i < close; ++i) {
        if (tokens[i].kind == TokenKind::Comma) {
            separation_before[i + 1uz] = Separation::Space;
        }
    }
}

auto SyntaxFormatter::mark_function_type(const ASTFunctionType& function) noexcept -> void {
    for (const auto& parameter : function.parameters) {
        mark_access(parameter.access);
    }
}

auto SyntaxFormatter::mark_group(Span span) noexcept -> void {
    const auto start = token_at(span.start());
    group_end_exclusive[start] =
        std::max(group_end_exclusive[start], token_covering(span.end() - 1u) + 1uz);
}

auto SyntaxFormatter::annotate() noexcept -> void {
    const auto structured = [&](ASTExprID id) noexcept {
        const auto* element = &syntax.expression(id);
        while (const auto* group = std::get_if<ASTGroupExpr>(&element->value)) {
            element = &syntax.expression(group->expression);
        }
        return std::holds_alternative<ASTConstructionExpr>(element->value)
            || std::holds_alternative<ASTArrayExpr>(element->value);
    };
    for (const auto& fragment : syntax.ast_module().cpp_source_fragments) {
        separation_before[token_at(fragment.form_span.start())] = Separation::Hard;
    }
    for (const auto& type : syntax.types()) {
        std::visit(
            [&](const auto& value) noexcept {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::same_as<T, ASTNamedType>) {
                    mark_named(value, type.span);
                } else if constexpr (std::same_as<T, ASTPointerType>) {
                    const auto start = token_at(type.span.start());
                    const auto close = token_covering(type.span.end() - 1u);
                    separation_before[start + 1uz] = Separation::None;
                    separation_before[start + 2uz] = Separation::None;
                    separation_before[close] = Separation::None;
                    mark_access(value.access);
                } else if constexpr (std::same_as<T, ASTArrayType>) {
                    separation_before[token_at(syntax.expression(value.extent).span.start())] =
                        Separation::Space;
                } else if constexpr (std::same_as<T, ASTFunctionType>) {
                    mark_function_type(value);
                }
            },
            type.value
        );
    }
    for (const auto& item : syntax.items()) {
        if (const auto* function = std::get_if<ASTFunctionDecl>(&item.value);
            function != nullptr && function->is_implicit_entry) {
            continue;
        }
        separation_before[token_at(item.span.start())] = Separation::Hard;
        mark_group(item.span);
        continuation_groups[token_at(item.span.start())] = false;
        std::visit(
            [&](const auto& value) noexcept {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::same_as<T, ASTStructDecl> || std::same_as<T, ASTEnumDecl>) {
                    mark_block(item.span);
                    const auto mark_members = [&](const auto& members) noexcept {
                        for (const auto& member : members) {
                            separation_before[token_at(member.span.start())] = Separation::Hard;
                        }
                    };
                    if constexpr (std::same_as<T, ASTStructDecl>) {
                        mark_members(value.fields);
                    } else {
                        mark_members(value.cases);
                    }
                } else if constexpr (std::same_as<T, ASTTestDecl>) {
                    block_layouts[block_open(syntax.block(value.body).span)] =
                        BlockLayout::Expanded;
                } else if constexpr (std::same_as<T, ASTFunctionDecl>) {
                    for (const auto& parameter : value.parameters) {
                        mark_access(parameter.access);
                    }
                }
            },
            item.value
        );
    }
    for (const auto& statement : syntax.statements()) {
        separation_before[token_at(statement.span.start())] = Separation::Hard;
        std::visit(
            [&](const auto& value) noexcept {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::same_as<T, ASTAssignment>) {
                    const auto operation = token_at(value.operator_span.start());
                    separation_before[operation] = Separation::Space;
                    separation_before[operation + 1uz] = Separation::Space;
                } else if constexpr (std::same_as<T, ASTForStmt>) {
                    mark_group(value.header.span);
                    const auto end = token_covering(value.header.span.end() - 1u);
                    for (auto i = token_at(value.header.span.start()); i < end; ++i) {
                        if (closing_indices[i] != 0) {
                            i = closing_indices[i];
                            continue;
                        }
                        if (tokens[i].kind == TokenKind::Semicolon
                            || tokens[i].kind == TokenKind::Comma) {
                            separation_before[i + 1uz] = Separation::SoftSpace;
                        }
                    }
                    if (const auto* range = std::get_if<ASTRangeForHeader>(&value.header.value)) {
                        if (range->write_marker) {
                            prefix_operators[token_at(range->write_marker->start())] = true;
                        }
                    }
                }
                if constexpr (std::same_as<T, ASTForStmt> || std::same_as<T, ASTWhileStmt>) {
                    block_layouts[block_open(syntax.block(value.body).span)] =
                        BlockLayout::Expanded;
                }
                mark_control(value);
            },
            statement.value
        );
    }
    for (const auto& block : syntax.blocks()) {
        if (&block == top_level_body) {
            continue;
        }
        mark_block(block.span);
    }
    for (const auto& block : syntax.branch_blocks()) {
        mark_block(block.span);
        if (block.result) {
            separation_before[token_at(syntax.expression(*block.result).span.start())] =
                Separation::Hard;
        }
    }
    for (const auto& expression : syntax.expressions()) {
        std::visit(
            [&](const auto& value) noexcept {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::same_as<T, ASTPrefixExpr>) {
                    prefix_operators[token_at(value.operator_span.start())] = true;
                } else if constexpr (std::same_as<T, ASTAccessExpr>) {
                    prefix_operators[token_at(value.marker_span.start())] = true;
                } else if constexpr (std::same_as<T, ASTBinaryExpr>
                                     || std::same_as<T, ASTCastExpr>) {
                    infix_operators[token_at(value.operator_span.start())] = true;
                    mark_group(expression.span);
                } else if constexpr (std::same_as<T, ASTLambdaExpr>) {
                    for (const auto& parameter : value.parameters) {
                        mark_access(parameter.access);
                    }
                    for (const auto& capture : value.captures) {
                        if (capture.write_marker) {
                            prefix_operators[token_at(capture.write_marker->start())] = true;
                        }
                    }
                } else if constexpr (std::same_as<T, ASTArrayExpr>) {
                    if (std::ranges::any_of(value.element_ids, structured)) {
                        block_layouts[token_at(expression.span.start())] = BlockLayout::Expanded;
                        for (const auto id : value.element_ids) {
                            separation_before[token_at(syntax.expression(id).span.start())] =
                                Separation::Hard;
                        }
                    }
                } else if constexpr (std::same_as<T, ASTConstructionExpr>) {
                    std::visit(
                        [&](const auto& initializer) noexcept {
                            using U = std::decay_t<decltype(initializer)>;
                            if constexpr (!std::same_as<U, std::monostate>) {
                                const auto values = [&]() noexcept {
                                    if constexpr (std::same_as<U, ASTPositionalInitializerList>) {
                                        return std::span(initializer.values);
                                    } else {
                                        return initializer.fields
                                            | std::views::transform(
                                                   [](
                                                       const ASTFieldInitializer& field
                                                   ) static noexcept { return field.value; }
                                            );
                                    }
                                }();
                                const auto expanded = [&]() noexcept {
                                    const auto nested = std::ranges::any_of(values, structured);
                                    if constexpr (std::same_as<U, ASTFieldInitializerList>) {
                                        return nested || initializer.fields.size() > 1uz;
                                    } else {
                                        return nested;
                                    }
                                }();
                                if (expanded) {
                                    block_layouts[block_open(expression.span)] =
                                        BlockLayout::Expanded;
                                    if constexpr (std::same_as<U, ASTPositionalInitializerList>) {
                                        for (const auto id : values) {
                                            separation_before[token_at(syntax.expression(id)
                                                                           .span.start())] =
                                                Separation::Hard;
                                        }
                                    } else {
                                        for (const auto& field : initializer.fields) {
                                            separation_before[token_at(field.span.start())] =
                                                Separation::Hard;
                                        }
                                    }
                                }
                            }
                        },
                        value.initializer.value
                    );
                    std::visit(
                        [&](const auto& type) noexcept {
                            using U = std::decay_t<decltype(type)>;
                            if constexpr (std::same_as<U, ASTNamedType>) {
                                mark_named(type, value.type.span);
                            } else {
                                mark_function_type(type);
                            }
                        },
                        value.type.value
                    );
                }
                mark_control(value);
            },
            expression.value
        );
    }
    for (const auto& pattern : syntax.patterns()) {
        if (const auto* negative = std::get_if<ASTNegativeNumberPattern>(&pattern.value)) {
            prefix_operators[token_at(negative->minus_span.start())] = true;
        } else if (const auto* alternatives = std::get_if<ASTOrPattern>(&pattern.value)) {
            for (const auto pipe : alternatives->pipe_spans) {
                infix_operators[token_at(pipe.start())] = true;
            }
            mark_group(pattern.span);
        } else if (const auto* constraint = std::get_if<ASTConstraintPattern>(&pattern.value)) {
            if (const auto* array = std::get_if<ASTArrayType>(&constraint->operand.value)) {
                separation_before[token_at(syntax.expression(array->extent).span.start())] =
                    Separation::Space;
            }
        }
    }
}

auto SyntaxFormatter::separation(std::size_t index) const noexcept -> Separation {
    const auto left = tokens[index - 1uz].kind;
    const auto right = tokens[index].kind;
    using enum TokenKind;
    if (right == Semicolon || right == Comma) {
        return left == For ? Separation::Space : Separation::None;
    }
    if (separation_before[index]) {
        return *separation_before[index];
    }
    if (left == InterpolationStart
        || right == InterpolationEnd
        || left == InterpolationText
        || right == InterpolationText
        || left == InterpolationOpen
        || right == InterpolationClose
        || left == InterpolationSpec
        || right == InterpolationSpec
        || right == InterpolationOpen) {
        return Separation::None;
    }
    if (left == Semicolon || left == CppSourceFragment) {
        return Separation::Hard;
    }
    if (left == Comma) {
        return Separation::SoftSpace;
    }
    if (right == LeftBrace) {
        return left == ColonColon ? Separation::None : Separation::Space;
    }
    if (left == DotDot || right == DotDot) {
        return Separation::None;
    }
    if (infix_operators[index]) {
        return Separation::SoftSpace;
    }
    if (left == Equal
        || right == Equal
        || left == Arrow
        || right == Arrow
        || left == FatArrow
        || right == FatArrow
        || infix_operators[index - 1uz]) {
        return Separation::Space;
    }
    if (left == Return
        || left == Throw
        || left == In
        || left == If
        || left == While
        || left == Match
        || left == For
        || left == Else
        || left == Catch
        || left == Is
        || left == Using
        || (left == Import && right != LeftParen)) {
        return Separation::Space;
    }
    if (left == PlusPlus || left == MinusMinus) {
        return Separation::None;
    }
    if (right == Colon
        || right == Dot
        || left == Dot
        || right == ColonColon
        || left == ColonColon
        || right == Question
        || right == PlusPlus
        || right == MinusMinus) {
        return Separation::None;
    }
    if (left == Colon) {
        return Separation::Space;
    }
    if (prefix_operators[index - 1uz]) {
        return Separation::None;
    }
    if (right == LeftParen || right == LeftBracket) {
        return Separation::None;
    }
    return Separation::Space;
}

auto SyntaxFormatter::separator(Separation value) noexcept -> DocID {
    switch (value) {
        case Separation::None:      return document.text("");
        case Separation::Space:     return document.text(" ");
        case Separation::SoftEmpty: return document.line(false);
        case Separation::SoftSpace: return document.line(true);
        case Separation::Hard:      return document.hardline();
    }
    std::unreachable();
}

auto SyntaxFormatter::needs_separator(std::size_t index) const noexcept -> bool {
    if (index == 0 || index == tokens.size() || source.trivia_before(index).empty()) {
        return false;
    }
    const auto interpolation = [](TokenKind kind) static noexcept {
        return kind >= TokenKind::InterpolationStart && kind <= TokenKind::InterpolationSpec;
    };
    if (interpolation(tokens[index - 1uz].kind) || interpolation(tokens[index].kind)) {
        return false;
    }
    const auto left = source.spelling(tokens[index - 1uz].span);
    const auto right = source.spelling(tokens[index].span);
    const auto joined = std::string(left) + std::string(right);
    const auto scanned =
        lex(SourceView {
            .source_id = source.token_buffer().source_id(),
            .text = joined,
            .origin = "graver token boundary"
        });
    const auto pair = scanned.value.tokens();
    return has_errors(scanned)
        || pair.size() != 2uz
        || pair[0].kind != tokens[index - 1uz].kind
        || pair[1].kind != tokens[index].kind
        || pair[0].span.size() != left.size();
}

auto SyntaxFormatter::gap(std::size_t index, Separation desired, bool closing) noexcept -> DocID {
    auto parts = std::vector<DocID>();
    auto endings = 0uz;
    auto comment_count = 0uz;
    auto required = minimum_breaks[index];
    const auto breaks = [&](std::size_t count) noexcept {
        for (auto i = 0uz; i < count; ++i) {
            parts.push_back(document.hardline());
        }
    };
    for (const auto trivia : source.trivia_before(index)) {
        if (trivia.kind == TriviaKind::LineEnding) {
            ++endings;
        } else if (trivia.kind == TriviaKind::LineComment) {
            if (endings != 0uz) {
                breaks(std::max(endings, required));
                required = 0uz;
            } else if (index != 0uz || comment_count != 0uz) {
                parts.push_back(document.text(" "));
            }
            parts.push_back(document.text(source.spelling(trivia.span)));
            ++comment_count;
            endings = 0uz;
        }
    }
    if (comment_count == 0uz) {
        if ((desired == Separation::None || desired == Separation::SoftEmpty)
            && needs_separator(index)) {
            desired = desired == Separation::None ? Separation::Space : Separation::SoftSpace;
        }
        if (index == 0uz) {
            breaks(endings);
        } else if (endings > 1uz || required != 0uz) {
            breaks(std::max(endings, required));
        } else {
            parts.push_back(separator(desired));
        }
        return document.concat(std::move(parts));
    }
    auto body = document.concat(std::move(parts));
    if (closing) {
        body = document.indent(body);
    }
    parts = {body};
    breaks(std::max({1uz, endings, required}));
    return document.concat(std::move(parts));
}

auto SyntaxFormatter::sequence(
    std::size_t start,
    std::size_t end,
    Separation first,
    bool inside_group
) noexcept -> DocID {
    auto result = std::vector<DocID>();
    for (auto index = start; index < end; ++index) {
        result.push_back(
            inside_group && index == start
                ? document.text("")
                : gap(index, index == start ? first : separation(index), false)
        );
        if (!(inside_group && index == start)
            && group_end_exclusive[index] > index + 1uz
            && group_end_exclusive[index] <= end) {
            const auto last = group_end_exclusive[index];
            result.push_back(document.group(sequence(index, last, Separation::None, true)));
            index = last - 1uz;
            continue;
        }
        if (closing_indices[index] != 0) {
            const auto close = closing_indices[index];
            const auto layout = block_layouts[index];
            const auto nonempty_block = layout != BlockLayout::None && close != index + 1uz;
            const auto expanded_block =
                layout == BlockLayout::Expanded || (layout == BlockLayout::Block && nonempty_block);
            const auto interpolation = tokens[index].kind == TokenKind::InterpolationStart
                || tokens[index].kind == TokenKind::InterpolationOpen;
            const auto brace_list = tokens[index].kind == TokenKind::LeftBrace
                && (index == 0uz
                    || (tokens[index - 1uz].kind != TokenKind::ColonColon
                        && tokens[index - 1uz].kind != TokenKind::Using))
                && layout == BlockLayout::None
                && close != index + 1uz;
            const auto boundary = interpolation ? Separation::None
                : expanded_block                ? Separation::Hard
                : nonempty_block                ? Separation::SoftSpace
                : brace_list                    ? Separation::SoftSpace
                                                : Separation::SoftEmpty;
            auto parts = std::vector<DocID> {document.text(source.spelling(tokens[index].span))};
            const auto inner = sequence(index + 1uz, close, boundary);
            parts.push_back(
                tokens[index].kind == TokenKind::InterpolationStart ? inner : document.indent(inner)
            );
            parts.push_back(gap(close, boundary, true));
            parts.push_back(document.text(source.spelling(tokens[close].span)));
            const auto body = document.concat(std::move(parts));
            result.push_back(expanded_block ? body : document.group(body));
            index = close;
        } else {
            result.push_back(document.verbatim(source.spelling(tokens[index].span)));
        }
    }
    if (inside_group && continuation_groups[start] && result.size() > 2uz) {
        auto rest = std::vector<DocID>(result.begin() + 2, result.end());
        return document.concat(
            {result[0], result[1], document.indent(document.concat(std::move(rest)))}
        );
    }
    return document.concat(std::move(result));
}

auto comments(const Source& source, std::size_t index) noexcept -> std::vector<std::string_view> {
    auto result = std::vector<std::string_view>();
    for (const auto trivia : source.trivia_before(index)) {
        if (trivia.kind == TriviaKind::LineComment) {
            result.push_back(source.spelling(trivia.span));
        }
    }
    return result;
}

auto same_tokens_and_comments(const Source& before, const Source& after) noexcept -> bool {
    const auto left = before.token_buffer().tokens();
    const auto right = after.token_buffer().tokens();
    if (left.size() != right.size()) {
        return false;
    }
    for (auto index = 0uz; index <= left.size(); ++index) {
        if (comments(before, index) != comments(after, index)) {
            return false;
        }
        if (index < left.size()
            && (left[index].kind != right[index].kind
                || before.spelling(left[index].span) != after.spelling(right[index].span))) {
            return false;
        }
    }
    return true;
}

} // namespace

namespace graver {

auto format(const SourceManager& sources, SourceID source_id) noexcept
    -> std::expected<std::string, Diagnostics> {
    const auto input = Source::scan(sources.view(source_id));
    if (!input) {
        return std::unexpected(input.error());
    }
    const auto syntax = parse(sources, input->token_buffer());
    if (!syntax) {
        return std::unexpected(syntax.error());
    }
    auto result = SyntaxFormatter(*input, syntax->view()).format();
    result = align_array_rows(*input, syntax->view(), std::move(result), line_width);
    auto output_sources = SourceManager();
    const auto output_id = output_sources.append_virtual("graver output", result);
    if (!output_id) {
        return std::unexpected(
            failure(source_id, Span::at(0), "graver: output exceeds source size limit")
        );
    }
    const auto output = Source::scan(output_sources.view(*output_id));
    if (!output
        || !same_tokens_and_comments(*input, *output)
        || !parse(output_sources, output->token_buffer())) {
        return std::unexpected(failure(
            source_id,
            Span::at(0),
            "graver: output validation failed; no formatted output was produced"
        ));
    }
    return result;
}

}
