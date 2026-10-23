module carven:frontend.parse.parser.impl;

import :diagnostics.builder;
import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.interop;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.tree;
import :frontend.ast.type;
import :frontend.lex.token;
import :frontend.parse.builder;
import :frontend.parse.parser;
import :source.identifier;
import :source.text;
import std;

namespace {

auto closing_delimiter(TokenKind kind) noexcept -> TokenKind {
    switch (kind) {
        case TokenKind::InterpolationStart: return TokenKind::InterpolationEnd;
        case TokenKind::InterpolationOpen:  return TokenKind::InterpolationClose;
        case TokenKind::LeftParen:          return TokenKind::RightParen;
        case TokenKind::LeftBracket:        return TokenKind::RightBracket;
        case TokenKind::LeftBrace:          return TokenKind::RightBrace;
        default:                            return TokenKind::Invalid;
    }
}

auto is_closing_delimiter(TokenKind kind) noexcept -> bool {
    return kind == TokenKind::RightParen
        || kind == TokenKind::RightBracket
        || kind == TokenKind::RightBrace
        || kind == TokenKind::InterpolationEnd
        || kind == TokenKind::InterpolationClose;
}

auto starts_item(TokenKind kind) noexcept -> bool {
    return kind == TokenKind::Private
        || kind == TokenKind::Export
        || kind == TokenKind::Enum
        || kind == TokenKind::Struct
        || kind == TokenKind::Class
        || kind == TokenKind::Async
        || kind == TokenKind::Fn
        || kind == TokenKind::Const
        || kind == TokenKind::Test
        || kind == TokenKind::Import
        || kind == TokenKind::CppSourceFragment;
}

} // namespace

Parser::Parser(SourceView source_view, const TokenBuffer& token_buffer) noexcept
    : source(source_view.text),
      source_id(source_view.source_id),
      token_buffer(std::addressof(token_buffer)),
      tokens(token_buffer.tokens()),
      builder(source_view) {}

Parser::DepthGuard::DepthGuard(std::uint32_t* depth, std::uint32_t previous) noexcept
    : depth(depth),
      previous(previous) {}

Parser::DepthGuard::~DepthGuard() noexcept {
    if (depth != nullptr) {
        *depth = previous;
    }
}

Parser::DepthGuard::operator bool() const noexcept {
    return depth != nullptr;
}

auto Parser::set_depth(std::uint32_t& depth, std::uint32_t value) noexcept -> DepthGuard {
    return DepthGuard(&depth, std::exchange(depth, value));
}

auto Parser::enter_depth(std::uint32_t& depth) noexcept -> DepthGuard {
    return set_depth(depth, depth + 1);
}

auto Parser::run() noexcept -> Diagnosed<std::optional<SyntaxTree>> {
    if (!preflight_delimiter_nesting()) {
        return {.value = std::nullopt, .diagnostics = std::move(diagnostics)};
    }

    auto module_imports = std::vector<ASTModuleImportID> {};
    auto cpp_header_imports = std::vector<ASTCppHeaderImport> {};
    auto cpp_source_fragments = std::vector<ASTCppSourceFragment> {};
    auto items = std::vector<ASTItemID> {};
    auto statements = std::vector<ASTStmtID> {};
    auto unavailable_names = std::vector<Span>();

    while (!at_end()) {
        if (match(TokenKind::Invalid)) {
            continue;
        }
        if (!check(TokenKind::Import) || check_next(TokenKind::LeftParen)) {
            break;
        }
        const auto checkpoint = save();
        auto scope_uncertain = false;
        if (check_next(TokenKind::CppAngleHeaderName)
            || check_next(TokenKind::CppQuoteHeaderName)) {
            auto declaration = parse_cpp_header_import();
            scope_uncertain = declaration.scope_uncertain;
            if (!failed) {
                cpp_header_imports.push_back(std::move(declaration.value));
                continue;
            }
        } else {
            const auto declaration = parse_module_import();
            scope_uncertain = declaration.scope_uncertain;
            if (!failed) {
                module_imports.push_back(declaration.value);
                continue;
            }
        }
        recover_import(checkpoint);
        scope_complete = scope_complete && !scope_uncertain;
    }

    while (!failed && !at_end()) {
        if (match(TokenKind::Invalid)) {
            continue;
        }
        if (check(TokenKind::CppSourceFragment)) {
            cpp_source_fragments.push_back(make_cpp_source_fragment(consume().span));
            continue;
        }
        furthest_speculative_failure.reset();
        const auto diagnostic_count = diagnostics.size();
        const auto checkpoint = builder.checkpoint();
        const auto item_start = cursor;
        const auto kind = current().kind;
        const auto is_declaration = kind == TokenKind::Private
            || kind == TokenKind::Export
            || kind == TokenKind::Import
            || kind == TokenKind::Enum
            || kind == TokenKind::Struct
            || kind == TokenKind::Class
            || kind == TokenKind::Async
            || kind == TokenKind::Fn
            || (kind == TokenKind::Const
                && !check_next(TokenKind::If)
                && !check_next(TokenKind::For))
            || kind == TokenKind::Test;
        auto item = std::optional<ASTItemID>();
        auto declaration_name = std::optional<Span>();
        if (is_declaration) {
            const auto declaration = parse_top_level_item();
            item = declaration.value;
            declaration_name = declaration.name;
        } else {
            const auto statement = parse_statement();
            if (statement && !failed) {
                statements.push_back(*statement);
                continue;
            }
        }
        if (item && !failed) {
            items.push_back(*item);
            continue;
        }
        if (furthest_speculative_failure.has_value()
            && (diagnostics.size() == diagnostic_count
                || !diagnostics[diagnostic_count].attachment.primary.has_value()
                || furthest_speculative_failure->span.start()
                    >= diagnostics[diagnostic_count].attachment.primary->span.span.start())) {
            diagnostics.resize(diagnostic_count);
            const auto& failure = *furthest_speculative_failure;
            diagnostics.push_back(DiagnosticBuilder(failure.code, failure.message)
                                      .primary(locate(source_id, failure.span))
                                      .build());
        }
        furthest_speculative_failure.reset();
        builder.rewind(checkpoint);
        if (declaration_name) {
            unavailable_names.push_back(*declaration_name);
        } else if (is_declaration) {
            scope_complete = false;
        }
        failed = false;
        synchronize_top_level_item(item_start);
    }

    if (!statements.empty()) {
        const auto span = join(
            builder.statement(statements.front()).span,
            builder.statement(statements.back()).span
        );
        const auto body = builder.append_block(
            ASTBlock {
                .span = span,
                .statements = std::move(statements),
            }
        );
        items.push_back(builder.append_item(
            ASTItem {
                .span = span,
                .value = ASTFunctionDecl {
                    .is_implicit_entry = true,
                    .visibility = ASTBareDeclarationVisibility {},
                    .cpp_export = std::nullopt,
                    .const_span = std::nullopt,
                    .async_span = std::nullopt,
                    .name_span = Span::at(span.start()),
                    .type_parameters = std::nullopt,
                    .parameters = {},
                    .result_type = std::nullopt,
                    .throw_clause = std::nullopt,
                    .implementation = ASTFunctionBody {.body = body},
                },
            }
        ));
    }

    auto ast_module = ASTModule {
        .span = Span::from_bounds(0, static_cast<std::uint32_t>(source.size())),
        .module_imports = std::move(module_imports),
        .cpp_header_imports = std::move(cpp_header_imports),
        .cpp_source_fragments = std::move(cpp_source_fragments),
        .items = std::move(items),
        .unavailable_names = std::move(unavailable_names),
        .scope_complete = scope_complete,
    };
    return {
        .value = std::move(builder).finish(std::move(ast_module)),
        .diagnostics = std::move(diagnostics),
    };
}

auto Parser::synchronize_top_level_item(std::size_t item_start) noexcept -> void {
    // Braces retain ownership of nested declarations even when an expression
    // delimiter is missing. A new declaration line can recover outside them.
    auto delimiter_depth = 0uz;
    auto brace_depth = 0uz;
    const auto advance_depth = [&](TokenKind kind) noexcept {
        if (kind == TokenKind::LeftBrace || kind == TokenKind::InterpolationOpen) {
            ++brace_depth;
        } else if ((kind == TokenKind::RightBrace || kind == TokenKind::InterpolationClose)
                   && brace_depth != 0) {
            --brace_depth;
        }
        if (closing_delimiter(kind) != TokenKind::Invalid) {
            ++delimiter_depth;
        } else if (is_closing_delimiter(kind) && delimiter_depth != 0) {
            --delimiter_depth;
        }
    };
    for (auto index = item_start; index < cursor && index < tokens.size(); ++index) {
        advance_depth(tokens[index].kind);
    }
    while (!at_end()) {
        if ((starts_item(current().kind) || (starts_unambiguous_statement() && on_new_line()))
            && (delimiter_depth == 0 || (brace_depth == 0 && on_new_line()))) {
            return;
        }
        advance_depth(consume().kind);
    }
}

auto Parser::on_new_line() const noexcept -> bool {
    return cursor > 0
        && !at_end()
        && source
               .substr(
                   tokens[cursor - 1].span.end(),
                   current().span.start() - tokens[cursor - 1].span.end()
               )
               .contains('\n');
}

auto Parser::on_statement_line() const noexcept -> bool {
    return on_new_line() && (starts_unambiguous_statement() || starts_item(current().kind));
}

auto Parser::recover_import(const Checkpoint& checkpoint) noexcept -> void {
    auto errors = std::vector<Diagnostic>(
        diagnostics.begin() + checkpoint.diagnostic_count,
        diagnostics.end()
    );
    restore(checkpoint);
    diagnostics.append_range(std::move(errors));
    failed = false;
    furthest_speculative_failure.reset();
    consume(); // The failed import itself must make progress.
    while (!at_end()) {
        if (on_statement_line()) {
            return;
        }
        if (consume().kind == TokenKind::Semicolon) {
            return;
        }
    }
}

auto Parser::recover_statement(const Checkpoint& checkpoint, std::size_t start) noexcept -> void {
    // Prefer the committed expression failure over a later missing terminator.
    if (furthest_speculative_failure
        && (diagnostics.size() == checkpoint.diagnostic_count
            || !diagnostics[checkpoint.diagnostic_count].attachment.primary
            || furthest_speculative_failure->span.start()
                >= diagnostics[checkpoint.diagnostic_count]
                       .attachment.primary->span.span.start())) {
        diagnostics.resize(checkpoint.diagnostic_count);
        const auto& error = *furthest_speculative_failure;
        diagnostics.push_back(DiagnosticBuilder(error.code, error.message)
                                  .primary(locate(source_id, error.span))
                                  .build());
    }
    if (diagnostics.size() == checkpoint.diagnostic_count) {
        failed = false;
        fail_here("invalid statement");
    }
    const auto failure = cursor;
    auto errors = std::vector<Diagnostic>(
        diagnostics.begin() + checkpoint.diagnostic_count,
        diagnostics.end()
    );
    restore(checkpoint);
    diagnostics.append_range(std::move(errors));
    failed = false;
    furthest_speculative_failure.reset();
    synchronize_statement(start, failure);
}

auto Parser::synchronize_statement(std::size_t start, std::size_t failure) noexcept -> void {
    auto depth = 0uz;
    auto braces = 0uz;
    cursor = start;
    split_right_shift = false;
    while (!at_end()) {
        const auto kind = current().kind;
        if (braces == 0 && kind == TokenKind::RightBrace) {
            return;
        }
        if (cursor > start
            && cursor >= failure
            && braces == 0
            && starts_unambiguous_statement()
            && on_new_line()) {
            return;
        }
        if (kind == TokenKind::LeftBrace || kind == TokenKind::InterpolationOpen) {
            ++braces;
        } else if ((kind == TokenKind::RightBrace || kind == TokenKind::InterpolationClose)
                   && braces != 0) {
            --braces;
        }
        if (closing_delimiter(kind) != TokenKind::Invalid) {
            ++depth;
        } else if (is_closing_delimiter(kind) && depth != 0) {
            --depth;
        }
        const auto position = cursor;
        consume();
        const auto first = tokens[start].kind;
        const auto for_header = first == TokenKind::For
            || (first == TokenKind::Const
                && start + 1 < tokens.size()
                && tokens[start + 1].kind == TokenKind::For);
        if (kind == TokenKind::Semicolon && position >= failure && braces == 0 && !for_header) {
            return;
        }
        if (position >= failure && (depth == 0 || (kind == TokenKind::RightBrace && braces == 0))) {
            if (kind == TokenKind::RightBrace) {
                // A record initializer's closing brace belongs to its statement.
                if (check(TokenKind::Semicolon)) {
                    consume();
                    return;
                }
                if (check(TokenKind::RightBrace) || at_end()) {
                    return;
                }
                if (first == TokenKind::For
                    || first == TokenKind::While
                    || first == TokenKind::If
                    || first == TokenKind::Try
                    || first == TokenKind::Match
                    || first == TokenKind::Const) {
                    return;
                }
            }
        }
    }
}

auto Parser::at_end() const noexcept -> bool {
    return cursor >= tokens.size();
}

auto Parser::current() const noexcept -> Token {
    if (cursor < tokens.size()) {
        if (split_right_shift) {
            return {
                .kind = TokenKind::Greater,
                .span =
                    Span::from_bounds(tokens[cursor].span.start() + 1, tokens[cursor].span.end())
            };
        }
        return tokens[cursor];
    }
    const auto end = static_cast<std::uint32_t>(source.size());
    return {
        .kind = TokenKind::Invalid,
        .span = Span::at(end),
    };
}

auto Parser::check(TokenKind kind) const noexcept -> bool {
    return !at_end() && current().kind == kind;
}

auto Parser::check_next(TokenKind kind) const noexcept -> bool {
    return cursor + 1 < tokens.size() && tokens[cursor + 1].kind == kind;
}

auto Parser::check_optionally_const(TokenKind keyword) const noexcept -> bool {
    return check(keyword) || (check(TokenKind::Const) && check_next(keyword));
}

auto Parser::consume() noexcept -> Token {
    const auto token = current();
    split_right_shift = false;
    if (cursor < tokens.size()) {
        ++cursor;
    }
    return token;
}

auto Parser::match(TokenKind kind) noexcept -> std::optional<Token> {
    if (!check(kind)) {
        return std::nullopt;
    }
    return consume();
}

auto Parser::fail(std::string_view message, Span span, DiagnosticCode code) noexcept -> void {
    if (failed) {
        return;
    }
    failed = true;
    if (speculation_depth == 0) {
        diagnostics.push_back(
            DiagnosticBuilder(code, std::string(message)).primary(locate(source_id, span)).build()
        );
    } else {
        remember_speculative_failure({
            .span = span,
            .message = std::string(message),
            .code = code,
        });
    }
}

auto Parser::fail_here(std::string_view message) noexcept -> void {
    fail(message, current().span, DiagnosticCode::Syntax);
}

auto Parser::expect(TokenKind kind, std::string_view message) noexcept -> Token {
    if (check(kind)) {
        return consume();
    }
    const auto terminator_boundary =
        at_end() || check(TokenKind::RightBrace) || on_statement_line();
    const auto missing_terminator = kind == TokenKind::Semicolon && terminator_boundary;
    const auto missing_delimiter =
        (kind == TokenKind::RightParen || kind == TokenKind::RightBracket)
        && (terminator_boundary
            || check(TokenKind::Semicolon)
            || check(TokenKind::RightParen)
            || check(TokenKind::RightBracket)
            || (kind == TokenKind::RightParen && check(TokenKind::LeftBrace)));
    if (!failed && (missing_terminator || missing_delimiter) && speculation_depth == 0) {
        fail_here(message);
        const auto spelling = kind == TokenKind::Semicolon ? ";"
            : kind == TokenKind::RightParen                ? ")"
                                                           : "]";
        const auto insertion = cursor == 0 ? current().span.start() : tokens[cursor - 1].span.end();
        diagnostics.back().attachment.primary->span = locate(source_id, Span::at(insertion));
        diagnostics.back().attachment.fixes.push_back(
            {std::format("Insert '{}'", spelling),
             {{locate(source_id, Span::at(insertion)), spelling}}}
        );
        failed = false;
        return {.kind = kind, .span = Span::at(current().span.start())};
    }
    fail_here(message);
    return current();
}

auto Parser::expect_name_component(std::string_view message) noexcept -> Token {
    if (is_identifier_spelling(slice(source, current().span))) {
        return consume();
    }
    fail_here(message);
    return current();
}

auto Parser::save() const noexcept -> Checkpoint {
    return {
        .cursor = cursor,
        .split_right_shift = split_right_shift,
        .builder = builder.checkpoint(),
        .failed = failed,
        .diagnostic_count = diagnostics.size(),
        .expression_nesting = expression_nesting,
        .block_boundary_depth = block_boundary_depth,
        .syntax_nesting = syntax_nesting,
    };
}

auto Parser::restore(const Checkpoint& checkpoint) noexcept -> void {
    builder.rewind(checkpoint.builder);
    cursor = checkpoint.cursor;
    split_right_shift = checkpoint.split_right_shift;
    failed = checkpoint.failed;
    diagnostics.resize(checkpoint.diagnostic_count);
    expression_nesting = checkpoint.expression_nesting;
    block_boundary_depth = checkpoint.block_boundary_depth;
    syntax_nesting = checkpoint.syntax_nesting;
}

auto Parser::enter_syntax_nesting() noexcept -> DepthGuard {
    if (syntax_nesting >= maximum_syntax_nesting) {
        fail(
            "syntax nesting exceeds the limit of 512",
            current().span,
            DiagnosticCode::ParseNestingTooDeep
        );
        return DepthGuard(nullptr, 0u);
    }
    return enter_depth(syntax_nesting);
}

auto Parser::preflight_delimiter_nesting() noexcept -> bool {
    struct OpenDelimiter final {
        TokenKind kind;
        Span span;
    };

    auto delimiters = std::vector<OpenDelimiter> {};
    delimiters.reserve(maximum_syntax_nesting);
    constexpr auto spelling = [](TokenKind kind) static noexcept -> std::string_view {
        switch (kind) {
            case TokenKind::LeftParen:    return "(";
            case TokenKind::RightParen:   return ")";
            case TokenKind::LeftBracket:  return "[";
            case TokenKind::RightBracket: return "]";
            case TokenKind::LeftBrace:    return "{";
            case TokenKind::RightBrace:   return "}";
            default:                      return "";
        }
    };

    for (const auto& token : tokens) {
        if (token.kind == TokenKind::Invalid) {
            fail("invalid lexical token", token.span, DiagnosticCode::Lexical);
            failed = false;
            continue;
        }
        const auto opening = closing_delimiter(token.kind) != TokenKind::Invalid;
        if (opening) {
            if (delimiters.size() == maximum_syntax_nesting) {
                fail(
                    "syntax nesting exceeds the limit of 512",
                    token.span,
                    DiagnosticCode::ParseNestingTooDeep
                );
                return false;
            }
            delimiters.push_back({.kind = token.kind, .span = token.span});
            continue;
        }

        if (!is_closing_delimiter(token.kind)) {
            continue;
        }
        if (delimiters.empty()) {
            fail(
                std::format("unexpected closing delimiter '{}'", spelling(token.kind)),
                token.span,
                DiagnosticCode::Syntax
            );
            return false;
        }
        // A closing outer delimiter also bounds an unfinished call or index.
        // Do not cross braces or interpolation boundaries to repair a mismatch.
        auto missing = 0uz;
        while (missing < delimiters.size()
               && closing_delimiter(delimiters[delimiters.size() - missing - 1].kind) != token.kind
               && (delimiters[delimiters.size() - missing - 1].kind == TokenKind::LeftParen
                   || delimiters[delimiters.size() - missing - 1].kind == TokenKind::LeftBracket)) {
            ++missing;
        }
        if (missing < delimiters.size()
            && closing_delimiter(delimiters[delimiters.size() - missing - 1].kind) == token.kind) {
            // Grammar recovery owns missing-token diagnostics and insertion fixes.
            // Preflight only rejects nesting that cannot be recovered safely.
            delimiters.erase(delimiters.end() - missing, delimiters.end());
        }
        const auto expected = closing_delimiter(delimiters.back().kind);
        if (token.kind == expected) {
            delimiters.pop_back();
            continue;
        }
        fail(
            std::format(
                "mismatched closing delimiter '{}'; expected '{}'",
                spelling(token.kind),
                spelling(expected)
            ),
            token.span,
            DiagnosticCode::Syntax
        );
        return false;
    }
    for (const auto& delimiter : delimiters) {
        if (delimiter.kind == TokenKind::LeftBrace
            || delimiter.kind == TokenKind::LeftParen
            || delimiter.kind == TokenKind::LeftBracket) {
            continue; // Parsing owns missing delimiters in recoverable syntax.
        }
        fail(
            std::format(
                "unclosed delimiter '{}'; expected '{}'",
                spelling(delimiter.kind),
                spelling(closing_delimiter(delimiter.kind))
            ),
            delimiter.span,
            DiagnosticCode::Syntax
        );
        return false;
    }
    return true;
}

auto Parser::begin_speculation() noexcept -> Checkpoint {
    const auto checkpoint = save();
    ++speculation_depth;
    speculation_failures.emplace_back();
    return checkpoint;
}

auto Parser::finish_speculation(
    const Checkpoint& checkpoint,
    bool commit,
    bool retain_failure
) noexcept -> void {
    --speculation_depth;
    auto failure = std::move(speculation_failures.back());
    speculation_failures.pop_back();
    if (!commit) {
        restore(checkpoint);
        if (retain_failure && failure.has_value()) {
            remember_speculative_failure(std::move(*failure));
        }
    } else {
        failed = checkpoint.failed;
        diagnostics.resize(checkpoint.diagnostic_count);
    }
}

auto Parser::remember_speculative_failure(ParseFailure failure) noexcept -> void {
    if (!speculation_failures.empty()) {
        auto& current = speculation_failures.back();
        if (!current.has_value() || failure.span.start() >= current->span.start()) {
            current = std::move(failure);
        }
        return;
    }
    if (!furthest_speculative_failure.has_value()
        || failure.span.start() >= furthest_speculative_failure->span.start()) {
        furthest_speculative_failure = std::move(failure);
    }
}

auto Parser::join(Span first, Span last) noexcept -> Span {
    return Span::from_bounds(first.start(), last.end());
}

auto Parser::make_cpp_source_fragment(Span full_span) const noexcept -> ASTCppSourceFragment {
    auto payload_start = full_span.start();
    while (payload_start < full_span.end()
           && source[payload_start] != '\n'
           && source[payload_start] != '\r') {
        ++payload_start;
    }
    if (payload_start < full_span.end() && source[payload_start] == '\r') {
        ++payload_start;
    }
    if (payload_start < full_span.end() && source[payload_start] == '\n') {
        ++payload_start;
    }
    auto payload_end = full_span.end();
    while (payload_end > payload_start
           && source[payload_end - 1] != '\n'
           && source[payload_end - 1] != '\r') {
        --payload_end;
    }
    return {
        .form_span = full_span,
        .payload_span = Span::from_bounds(payload_start, payload_end),
    };
}
