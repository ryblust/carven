module carven:frontend.parse.grammar.decl.impl;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.interop;
import :frontend.ast.stmt;
import :frontend.ast.type;
import :frontend.lex.token;
import :frontend.parse.builder;
import :frontend.parse.context;
import :source.text;
import std;

auto Parser::parse_module_reference() noexcept -> ASTModuleReference {
    const auto start = current().span;
    const auto failed_reference = [&]() noexcept -> ASTModuleReference {
        return {
            .span = start,
            .value = ASTDomainRootModuleReference {.components = {}},
        };
    };
    const auto finish_module_path =
        [&](std::vector<Span> components) noexcept -> std::optional<std::vector<Span>> {
        while (match(TokenKind::Dot)) {
            if (!check(TokenKind::Identifier)) {
                fail_here("expected a module name after '.'");
                return std::nullopt;
            }
            components.push_back(consume().span);
        }
        if (check(TokenKind::DotDot)) {
            fail_here("'..' is not supported in module references");
            return std::nullopt;
        }
        return components;
    };

    if (check(TokenKind::DotDot)) {
        fail_here("'..' is not supported in module references");
        return failed_reference();
    }
    if (check(TokenKind::ColonColon)) {
        fail_here("expected a craft name before '::'");
        return failed_reference();
    }

    if (const auto dot = match(TokenKind::Dot)) {
        if (!check(TokenKind::Identifier)) {
            fail_here("expected a module name after '.'");
            return failed_reference();
        }
        auto components = finish_module_path({consume().span});
        if (!components) {
            return failed_reference();
        }
        return {
            .span = join(start, components->back()),
            .value = ASTParentRelativeModuleReference {
                .prefix_span = dot->span,
                .components = std::move(*components),
            },
        };
    }

    if (!check(TokenKind::Identifier)) {
        fail_here("expected a module reference after 'import'");
        return failed_reference();
    }

    const auto first = consume();
    if (const auto separator = match(TokenKind::ColonColon)) {
        if (!check(TokenKind::Identifier)) {
            fail_here("expected a module name after '::'");
            return failed_reference();
        }
        auto components = finish_module_path({consume().span});
        if (!components) {
            return failed_reference();
        }
        return {
            .span = join(start, components->back()),
            .value = ASTCraftQualifiedModuleReference {
                .name_span = first.span,
                .separator_span = separator->span,
                .components = std::move(*components),
            },
        };
    }

    auto components = finish_module_path({first.span});
    if (!components) {
        return failed_reference();
    }
    return {
        .span = join(start, components->back()),
        .value = ASTDomainRootModuleReference {
            .components = std::move(*components),
        },
    };
}

auto Parser::parse_cpp_header_import() noexcept -> ASTCppHeaderImport {
    const auto start = expect(TokenKind::Import, "expected 'import'").span;
    const auto header = consume();
    const auto semicolon = expect(TokenKind::Semicolon, "expected ';' after C++ header import");
    return {
        .span = join(start, semicolon.span),
        .delimiter = header.kind == TokenKind::CppAngleHeaderName
            ? ASTCppHeaderDelimiter::AngleBrackets
            : ASTCppHeaderDelimiter::Quotes,
        .name_span = Span::from_bounds(header.span.start() + 1, header.span.end() - 1),
    };
}

auto Parser::parse_module_import() noexcept -> ASTModuleImportID {
    const auto start = expect(TokenKind::Import, "expected 'import'").span;
    auto module_reference = parse_module_reference();
    expect(TokenKind::Using, "expected 'using' in import declaration");

    auto selection = [&]() noexcept -> ASTImportSelection {
        if (const auto name = match(TokenKind::Identifier)) {
            return {
                .span = name->span,
                .value = ASTSingleImport {.name_span = name->span},
            };
        }
        if (const auto wildcard = match(TokenKind::Star)) {
            return {
                .span = wildcard->span,
                .value = ASTWildcardImport {},
            };
        }
        if (const auto left = match(TokenKind::LeftBrace)) {
            auto names = std::vector<Span> {};
            if (check(TokenKind::RightBrace)) {
                fail_here("an explicit import list must contain at least one name");
            } else {
                while (!failed) {
                    names.push_back(expect(TokenKind::Identifier, "expected imported name").span);
                    if (!match(TokenKind::Comma)) {
                        break;
                    }
                    if (check(TokenKind::RightBrace)) {
                        break;
                    }
                }
            }
            const auto right = expect(TokenKind::RightBrace, "expected '}' after import list");
            return {
                .span = join(left->span, right.span),
                .value = ASTImportList {.names = std::move(names)},
            };
        }
        fail_here("expected import selection");
        return {
            .span = current().span,
            .value = ASTWildcardImport {},
        };
    }();

    const auto semicolon = expect(TokenKind::Semicolon, "expected ';' after import declaration");
    return builder.append_module_import({
        .span = join(start, semicolon.span),
        .module_reference = std::move(module_reference),
        .selection = std::move(selection),
    });
}

auto Parser::parse_cpp_declaration_form(Token keyword) noexcept -> Span {
    expect(TokenKind::LeftParen, "expected '(' after declaration form");
    const auto name = expect(TokenKind::Identifier, "expected declaration form name");
    if (!failed && slice(source, name.span) != "cpp") {
        fail("unsupported declaration form; expected 'cpp'", name.span);
    }
    const auto right = expect(TokenKind::RightParen, "expected ')' after declaration form");
    return join(keyword.span, right.span);
}

auto Parser::parse_top_level_item() noexcept -> std::optional<ASTItemID> {
    const auto start = current().span;
    const auto discard_through_semicolon = [&]() noexcept {
        while (!at_end()) {
            if (consume().kind == TokenKind::Semicolon) {
                return;
            }
        }
    };
    auto visibility = ASTDeclarationVisibility {ASTBareDeclarationVisibility {}};
    auto cpp_export = std::optional<ASTCppExportForm> {};
    auto cpp_import = std::optional<Span> {};
    if (const auto keyword = match(TokenKind::Private)) {
        visibility = ASTPrivateDeclarationVisibility {.keyword_span = keyword->span};
    } else if (const auto keyword = match(TokenKind::Export)) {
        visibility = ASTExportDeclarationVisibility {.keyword_span = keyword->span};
        if (check(TokenKind::LeftParen)) {
            cpp_export = ASTCppExportForm {.span = parse_cpp_declaration_form(*keyword)};
        }
    }
    if (const auto keyword = match(TokenKind::Import)) {
        if (std::holds_alternative<ASTExportDeclarationVisibility>(visibility)) {
            fail("an import(cpp) declaration cannot be exported directly", keyword->span);
            discard_through_semicolon();
            return std::nullopt;
        }
        cpp_import = parse_cpp_declaration_form(*keyword);
    }
    const auto is_bare = std::holds_alternative<ASTBareDeclarationVisibility>(visibility);

    if ((cpp_export.has_value() || cpp_import.has_value()) && !check(TokenKind::Fn)) {
        fail_here("import(cpp) and export(cpp) forms must introduce a function");
        if (cpp_import.has_value() && check(TokenKind::Export)) {
            discard_through_semicolon();
        }
        return std::nullopt;
    }

    if (check(TokenKind::Enum)) {
        auto parsed = parse_enum(visibility);
        if (!parsed) {
            return std::nullopt;
        }
        auto [end, declaration] = std::move(*parsed);
        return builder.append_item({
            .span = join(start, end),
            .value = std::move(declaration),
        });
    }
    if (check(TokenKind::Struct)) {
        auto parsed = parse_struct(visibility);
        if (!parsed) {
            return std::nullopt;
        }
        auto [end, declaration] = std::move(*parsed);
        return builder.append_item({
            .span = join(start, end),
            .value = std::move(declaration),
        });
    }
    if (check(TokenKind::Fn)) {
        auto parsed = parse_function(visibility, cpp_export, cpp_import);
        if (!parsed) {
            return std::nullopt;
        }
        auto [end, definition] = std::move(*parsed);
        return builder.append_item({
            .span = join(start, end),
            .value = std::move(definition),
        });
    }
    if (check(TokenKind::Const)) {
        auto parsed = parse_constant(visibility);
        if (!parsed) {
            return std::nullopt;
        }
        auto [end, declaration] = std::move(*parsed);
        return builder.append_item({
            .span = join(start, end),
            .value = declaration,
        });
    }
    if (is_bare && check(TokenKind::Test)) {
        auto parsed = parse_test();
        if (!parsed) {
            return std::nullopt;
        }
        auto [end, declaration] = std::move(*parsed);
        return builder.append_item({
            .span = join(start, end),
            .value = std::move(declaration),
        });
    }
    fail_here(
        is_bare ? "expected top-level declaration or test"
                : "expected enum, struct, function, or const after visibility modifier"
    );
    return std::nullopt;
}

auto Parser::parse_test() noexcept -> std::optional<std::pair<Span, ASTTestDecl>> {
    const auto keyword = expect(TokenKind::Test, "expected 'test'");
    auto name = expect_string_literal("expected test name string");
    if (!name.has_value()) {
        return std::nullopt;
    }
    const auto test_context = enter_test_statement_context(true);
    const auto body = parse_ordinary_block();
    if (!body) {
        return std::nullopt;
    }
    return {
        std::pair {
            builder.block(*body).span,
            ASTTestDecl {
                .keyword_span = keyword.span,
                .name_span = name->span,
                .name = std::move(name->value.bytes),
                .body = *body,
            },
        },
    };
}

auto Parser::parse_enum(ASTDeclarationVisibility visibility) noexcept
    -> std::optional<std::pair<Span, ASTEnumDecl>> {
    expect(TokenKind::Enum, "expected 'enum'");
    const auto name = expect(TokenKind::Identifier, "expected enum name");
    auto underlying = std::optional<ASTTypeID> {};
    if (match(TokenKind::Colon)) {
        underlying = parse_type();
    }
    expect(TokenKind::LeftBrace, "expected '{' after enum name");

    auto cases = std::vector<ASTEnumCase> {};
    while (!failed && !check(TokenKind::RightBrace)) {
        const auto member_name = expect(TokenKind::Identifier, "expected enum case name");
        auto payload_types = std::vector<ASTTypeID> {};
        auto initializer = std::optional<ASTExprID> {};
        auto end = member_name.span;
        if (match(TokenKind::LeftParen)) {
            if (check(TokenKind::RightParen)) {
                fail_here("an enum payload list must contain at least one type");
            } else {
                while (!failed) {
                    const auto type = parse_type();
                    if (!type) {
                        return std::nullopt;
                    }
                    payload_types.push_back(*type);
                    if (!match(TokenKind::Comma)) {
                        break;
                    }
                    if (check(TokenKind::RightParen)) {
                        break;
                    }
                }
            }
            const auto right =
                expect(TokenKind::RightParen, "expected ')' after enum payload types");
            end = right.span;
        }
        if (match(TokenKind::Equal)) {
            initializer = parse_expression();
            if (!initializer) {
                return std::nullopt;
            }
            end = builder.expression(*initializer).span;
        }
        cases.push_back({
            .span = join(member_name.span, end),
            .name_span = member_name.span,
            .payload_types = std::move(payload_types),
            .initializer = initializer,
        });
        if (!match(TokenKind::Comma)) {
            break;
        }
        if (check(TokenKind::RightBrace)) {
            break;
        }
    }
    const auto right = expect(TokenKind::RightBrace, "expected '}' after enum cases");
    if (failed) {
        return std::nullopt;
    }
    return {
        std::pair {
            right.span,
            ASTEnumDecl {
                .visibility = visibility,
                .name_span = name.span,
                .underlying_type = underlying,
                .cases = std::move(cases),
            },
        },
    };
}

auto Parser::parse_struct(ASTDeclarationVisibility visibility) noexcept
    -> std::optional<std::pair<Span, ASTStructDecl>> {
    expect(TokenKind::Struct, "expected 'struct'");
    const auto name = expect(TokenKind::Identifier, "expected struct name");
    expect(TokenKind::LeftBrace, "expected '{' after struct name");

    auto fields = std::vector<ASTStructField> {};
    while (!failed && !check(TokenKind::RightBrace)) {
        const auto field_name = expect(TokenKind::Identifier, "expected field name");
        expect(TokenKind::Colon, "expected ':' after field name");
        const auto type = parse_type();
        if (!type) {
            return std::nullopt;
        }
        fields.push_back({
            .span = join(field_name.span, builder.type(*type).span),
            .name_span = field_name.span,
            .type = *type,
        });
        if (!match(TokenKind::Comma)) {
            break;
        }
        if (check(TokenKind::RightBrace)) {
            break;
        }
    }
    const auto right = expect(TokenKind::RightBrace, "expected '}' after struct fields");
    if (failed) {
        return std::nullopt;
    }
    return {
        std::pair {
            right.span,
            ASTStructDecl {
                .visibility = visibility,
                .name_span = name.span,
                .fields = std::move(fields),
            },
        },
    };
}

auto Parser::parse_function(
    ASTDeclarationVisibility visibility,
    std::optional<ASTCppExportForm> cpp_export,
    std::optional<Span> cpp_import
) noexcept -> std::optional<std::pair<Span, ASTFunctionDecl>> {
    expect(TokenKind::Fn, "expected 'fn'");
    const auto name = expect(TokenKind::Identifier, "expected function name");
    expect(TokenKind::LeftParen, "expected '(' after function name");

    auto parameters = std::vector<ASTFunctionParameter> {};
    while (!failed && !check(TokenKind::RightParen)) {
        auto access = ASTAccessSyntax {.mode = ASTAccessMode::Read, .marker = std::nullopt};
        if (const auto marker = match(TokenKind::Ampersand)) {
            access = {.mode = ASTAccessMode::Write, .marker = marker->span};
        } else if (const auto marker = match(TokenKind::AmpersandAmpersand)) {
            access = {.mode = ASTAccessMode::Take, .marker = marker->span};
        }
        const auto parameter_name = expect(TokenKind::Identifier, "expected parameter name");
        auto type = std::optional<ASTTypeID> {};
        if (match(TokenKind::Colon)) {
            type = parse_type();
        }
        const auto start = access.marker.value_or(parameter_name.span);
        parameters.push_back({
            .span = type.has_value() ? join(start, builder.type(*type).span)
                                     : join(start, parameter_name.span),
            .access = access,
            .target = slice(source, parameter_name.span) == "_"
                ? ASTBindingTarget {ASTDiscardBindingTarget {
                      .underscore_span = parameter_name.span,
                  }}
                : ASTBindingTarget {ASTNamedBindingTarget {
                      .name_span = parameter_name.span,
                  }},
            .type = type,
        });
        if (!match(TokenKind::Comma)) {
            break;
        }
        if (check(TokenKind::RightParen)) {
            break;
        }
    }
    expect(TokenKind::RightParen, "expected ')' after parameters");

    auto result_type = std::optional<ASTTypeID> {};
    if (match(TokenKind::Arrow)) {
        result_type = parse_type();
    }
    auto throw_clause = std::optional<ASTThrowClause> {};
    if (check(TokenKind::Throw)) {
        throw_clause = parse_throw_clause();
    }
    auto implementation = std::optional<ASTFunctionImplementation> {};
    auto end = std::optional<Span> {};
    if (cpp_import.has_value()) {
        const auto semicolon =
            expect(TokenKind::Semicolon, "expected ';' after import(cpp) declaration");
        implementation = ASTCppImportForm {.span = *cpp_import};
        end = semicolon.span;
    } else {
        const auto body = parse_ordinary_block();
        if (!body) {
            return std::nullopt;
        }
        implementation = ASTFunctionBody {.body = *body};
        end = builder.block(*body).span;
    }
    return {
        std::pair {
            *end,
            ASTFunctionDecl {
                .visibility = visibility,
                .cpp_export = cpp_export,
                .name_span = name.span,
                .parameters = std::move(parameters),
                .result_type = result_type,
                .throw_clause = std::move(throw_clause),
                .implementation = *implementation,
            },
        },
    };
}

auto Parser::parse_constant(ASTDeclarationVisibility visibility) noexcept
    -> std::optional<std::pair<Span, ASTConstantDecl>> {
    expect(TokenKind::Const, "expected 'const'");
    const auto name = expect(TokenKind::Identifier, "expected constant name");
    if (!failed && slice(source, name.span) == "_") {
        fail("a top-level constant requires a named target", name.span);
        return std::nullopt;
    }

    auto type = std::optional<ASTTypeID> {};
    if (match(TokenKind::Colon)) {
        type = parse_type();
        if (!type) {
            return std::nullopt;
        }
    }
    expect(TokenKind::Equal, "expected '=' before constant initializer");
    const auto initializer = parse_expression();
    if (!initializer) {
        return std::nullopt;
    }
    const auto semicolon = expect(TokenKind::Semicolon, "expected ';' after constant declaration");
    if (failed) {
        return std::nullopt;
    }
    return {
        std::pair {
            semicolon.span,
            ASTConstantDecl {
                .visibility = visibility,
                .name_span = name.span,
                .type = type,
                .initializer = *initializer,
            },
        },
    };
}
