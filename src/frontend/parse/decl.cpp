module carven:frontend.parse.decl.impl;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.imports;
import :frontend.ast.interop;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.type;
import :frontend.lex.token;
import :frontend.parse.builder;
import :frontend.parse.parser;
import :source.identifier;
import :source.text;
import std;

auto Parser::parse_import_item(Span name) noexcept -> ASTImportItem {
    auto alias = std::optional<Span>();
    if (match(TokenKind::As)) {
        alias = expect(TokenKind::Identifier, "expected local import name after 'as'").span;
    }
    return {.name_span = name, .alias_span = alias};
}

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
            if (!is_identifier_spelling(slice(source, current().span))) {
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
        if (!is_identifier_spelling(slice(source, current().span))) {
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

    if (!is_identifier_spelling(slice(source, current().span))) {
        fail_here("expected a module reference after 'import'");
        return failed_reference();
    }

    const auto first = consume();
    if (const auto separator = match(TokenKind::ColonColon)) {
        if (!is_identifier_spelling(slice(source, current().span))) {
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
    auto end = header.span;
    auto using_clause = std::optional<ASTCppUsing>();
    if (const auto using_token = match(TokenKind::Using)) {
        auto prefix = std::vector<Span>();
        const auto list = [&]() noexcept -> ASTImportList {
            expect(TokenKind::LeftBrace, "expected '{' in C++ selection");
            auto names = std::vector<ASTImportItem>();
            do {
                const auto name = expect_name_component("expected a simple C++ name").span;
                names.push_back(parse_import_item(name));
            } while (!failed && match(TokenKind::Comma) && !check(TokenKind::RightBrace));
            end = expect(TokenKind::RightBrace, "expected '}' after C++ selection").span;
            return {.names = std::move(names)};
        };
        auto selection = [&]() noexcept
            -> std::variant<ASTSingleImport, ASTImportList, ASTCppNamespaceSelection> {
            if (check(TokenKind::LeftBrace)) {
                return list();
            }
            auto name = expect_name_component("expected a C++ name after 'using'").span;
            while (!failed && match(TokenKind::ColonColon)) {
                prefix.push_back(name);
                if (check(TokenKind::LeftBrace)) {
                    return list();
                }
                if (const auto star = match(TokenKind::Star)) {
                    end = star->span;
                    return ASTCppNamespaceSelection {.star = star->span};
                }
                name = expect_name_component("expected a C++ name after '::'").span;
            }
            const auto item = parse_import_item(name);
            end = item.local_name_span();
            return ASTSingleImport {.item = item};
        }();
        using_clause = ASTCppUsing {
            .span = join(using_token->span, end),
            .prefix = std::move(prefix),
            .selection = std::move(selection),
        };
    }
    if (using_clause && std::holds_alternative<ASTImportList>(using_clause->selection)) {
        if (check(TokenKind::Semicolon)) {
            fail_here("unexpected ';' after import list");
        }
    } else {
        end = expect(TokenKind::Semicolon, "expected ';' after C++ header import").span;
    }
    return {
        .span = join(start, end),
        .delimiter = header.kind == TokenKind::CppAngleHeaderName
            ? ASTCppHeaderDelimiter::AngleBrackets
            : ASTCppHeaderDelimiter::Quotes,
        .name_span = Span::from_bounds(header.span.start() + 1, header.span.end() - 1),
        .using_clause = std::move(using_clause),
    };
}

auto Parser::parse_module_import() noexcept -> ASTModuleImportID {
    const auto start = expect(TokenKind::Import, "expected 'import'").span;
    auto module_reference = parse_module_reference();
    expect(TokenKind::Using, "expected 'using' in import declaration");

    const auto parse_item = [&]() noexcept -> ASTImportItem {
        const auto name = expect(TokenKind::Identifier, "expected imported name").span;
        return parse_import_item(name);
    };
    auto selection = [&]() noexcept -> ASTImportSelection {
        if (check(TokenKind::Identifier)) {
            const auto item = parse_item();
            return {
                .span = join(item.name_span, item.local_name_span()),
                .value = ASTSingleImport {.item = item},
            };
        }
        if (const auto wildcard = match(TokenKind::Star)) {
            return {
                .span = wildcard->span,
                .value = ASTWildcardImport {},
            };
        }
        if (const auto left = match(TokenKind::LeftBrace)) {
            auto names = std::vector<ASTImportItem> {};
            if (check(TokenKind::RightBrace)) {
                fail_here("an explicit import list must contain at least one name");
            } else {
                while (!failed) {
                    names.push_back(parse_item());
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

    auto end = selection.span;
    if (std::holds_alternative<ASTImportList>(selection.value)) {
        if (check(TokenKind::Semicolon)) {
            fail_here("unexpected ';' after import list");
        }
    } else {
        end = expect(TokenKind::Semicolon, "expected ';' after import declaration").span;
    }
    return builder.append_module_import({
        .span = join(start, end),
        .module_reference = std::move(module_reference),
        .selection = std::move(selection),
    });
}

auto Parser::parse_cpp_declaration_form(Token keyword) noexcept -> Span {
    expect(TokenKind::LeftParen, "expected '(' after declaration form");
    const auto name = expect(TokenKind::Identifier, "expected declaration form name");
    if (!failed && slice(source, name.span) != "cpp") {
        fail("unsupported declaration form; expected 'cpp'", name.span, DiagnosticCode::Syntax);
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
        if (check(TokenKind::LeftParen)) {
            // C++ interface generation leaves the ordinary module-domain audience intact.
            cpp_export = ASTCppExportForm {.span = parse_cpp_declaration_form(*keyword)};
        } else {
            visibility = ASTExportDeclarationVisibility {.keyword_span = keyword->span};
        }
    }
    if (const auto keyword = match(TokenKind::Import)) {
        if (cpp_export) {
            fail(
                "import(cpp) and export(cpp) cannot be combined",
                keyword->span,
                DiagnosticCode::Syntax
            );
            discard_through_semicolon();
            return std::nullopt;
        }
        cpp_import = parse_cpp_declaration_form(*keyword);
    }
    const auto is_bare = std::holds_alternative<ASTBareDeclarationVisibility>(visibility);
    auto async_span = std::optional<Span>();
    if (const auto keyword = match(TokenKind::Async)) {
        async_span = keyword->span;
    }
    auto const_span = std::optional<Span>();
    if (check(TokenKind::Const)
        && cursor + 1 < tokens.size()
        && (tokens[cursor + 1].kind == TokenKind::Fn
            || tokens[cursor + 1].kind == TokenKind::Async
            || (is_bare && tokens[cursor + 1].kind == TokenKind::Test))) {
        const_span = consume().span;
    }

    if (!async_span) {
        if (const auto keyword = match(TokenKind::Async)) {
            async_span = keyword->span;
        }
    }
    if ((cpp_export.has_value() || cpp_import.has_value()) && !check(TokenKind::Fn)) {
        fail_here("import(cpp) and export(cpp) forms must introduce a function");
        if (cpp_import.has_value() && check(TokenKind::Export)) {
            discard_through_semicolon();
        }
        return std::nullopt;
    }

    if (async_span && !check(TokenKind::Fn) && !(is_bare && check(TokenKind::Test))) {
        fail(
            "async modifier must introduce a function or test declaration",
            *async_span,
            DiagnosticCode::Syntax
        );
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
    if (check(TokenKind::Struct) || check(TokenKind::Class)) {
        auto parsed = parse_record(visibility);
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
        auto parsed = parse_function(visibility, cpp_export, cpp_import, const_span, async_span);
        if (!parsed) {
            return std::nullopt;
        }
        auto [end, definition] = std::move(*parsed);
        return builder.append_item({
            .span = join(start, end),
            .value = std::move(definition),
        });
    }
    if (is_bare
        && !cpp_export
        && !cpp_import
        && check(TokenKind::Const)
        && (check_next(TokenKind::LeftBrace) || check_next(TokenKind::StringLiteral))) {
        const auto keyword = consume();
        auto label = parse_block_label();
        const auto body = parse_ordinary_block();
        if (!body) {
            return std::nullopt;
        }
        return builder.append_item({
            .span = join(start, builder.block(*body).span),
            .value = ASTConstBlock {
                .keyword_span = keyword.span,
                .label = std::move(label),
                .body = *body,
            },
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
        auto parsed = parse_test(const_span.has_value(), async_span);
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

auto Parser::parse_test(bool is_const, std::optional<Span> async_span) noexcept
    -> std::optional<std::pair<Span, ASTTestDecl>> {
    const auto keyword = expect(TokenKind::Test, "expected 'test'");
    auto label = parse_block_label();
    const auto body = parse_ordinary_block();
    if (!body) {
        return std::nullopt;
    }
    return {
        std::pair {
            builder.block(*body).span,
            ASTTestDecl {
                .is_const = is_const,
                .async_span = async_span,
                .keyword_span = keyword.span,
                .label = std::move(label),
                .body = *body,
            },
        },
    };
}

auto Parser::parse_block_label() noexcept -> std::optional<ASTBlockLabel> {
    if (!check(TokenKind::StringLiteral)) {
        return std::nullopt;
    }
    auto literal = expect_string_literal("expected block label string");
    return ASTBlockLabel {.span = literal->span, .text = std::move(literal->value.bytes)};
}

auto Parser::parse_type_parameters() noexcept -> std::optional<ASTTypeParameterClause> {
    const auto left = match(TokenKind::Less);
    if (!left) {
        return std::nullopt;
    }
    auto parameters = std::vector<ASTGenericParameter>();
    while (!failed) {
        const auto kind = match(TokenKind::Throw) ? ASTGenericParameterKind::FailureSet
                                                  : ASTGenericParameterKind::Type;
        const auto name = expect(TokenKind::Identifier, "expected generic parameter name");
        auto bounds = std::vector<ASTTypeID>();
        if (match(TokenKind::Colon)) {
            do {
                const auto bound = parse_named_type();
                if (!bound) {
                    return std::nullopt;
                }
                bounds.push_back(*bound);
            } while (match(TokenKind::Plus));
        }
        parameters.push_back({.name_span = name.span, .kind = kind, .bounds = std::move(bounds)});
        if (!match(TokenKind::Comma) || check(TokenKind::Greater)) {
            break;
        }
    }
    const auto right = expect(TokenKind::Greater, "expected '>' after type parameters");
    return ASTTypeParameterClause {
        .span = join(left->span, right.span),
        .parameters = std::move(parameters)
    };
}

auto Parser::parse_enum(ASTDeclarationVisibility visibility) noexcept
    -> std::optional<std::pair<Span, ASTEnumDecl>> {
    expect(TokenKind::Enum, "expected 'enum'");
    const auto name = expect(TokenKind::Identifier, "expected enum name");
    auto type_parameters = parse_type_parameters();
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
                .type_parameters = std::move(type_parameters),
                .underlying_type = underlying,
                .cases = std::move(cases),
            },
        },
    };
}

auto Parser::parse_record(ASTDeclarationVisibility visibility) noexcept
    -> std::optional<std::pair<Span, ASTRecordDecl>> {
    const auto kind =
        consume().kind == TokenKind::Class ? ASTRecordKind::Class : ASTRecordKind::Struct;
    const auto name = expect(TokenKind::Identifier, "expected record name");
    auto type_parameters = parse_type_parameters();
    expect(TokenKind::LeftBrace, "expected '{' after record name");

    auto fields = std::vector<ASTRecordField> {};
    auto operations = std::vector<ASTItemID>();
    while (!failed && !check(TokenKind::RightBrace)) {
        if (kind == ASTRecordKind::Class
            && (check(TokenKind::Fn)
                || check(TokenKind::Private)
                || check(TokenKind::Const)
                || check(TokenKind::Async))) {
            const auto start = current().span;
            auto access = ASTDeclarationVisibility(ASTBareDeclarationVisibility {});
            if (const auto keyword = match(TokenKind::Private)) {
                access = ASTPrivateDeclarationVisibility {.keyword_span = keyword->span};
            }
            auto async_span =
                match(TokenKind::Async).transform([](const Token& token) static noexcept {
                    return token.span;
                });
            const auto const_span =
                match(TokenKind::Const).transform([](const Token& token) static noexcept {
                    return token.span;
                });
            if (!async_span) {
                async_span =
                    match(TokenKind::Async).transform([](const Token& token) static noexcept {
                        return token.span;
                    });
            }
            auto operation =
                parse_function(access, std::nullopt, std::nullopt, const_span, async_span);
            if (!operation) {
                return std::nullopt;
            }
            operations.push_back(builder.append_item(
                {.span = join(start, operation->first), .value = std::move(operation->second)}
            ));
            continue;
        }
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
    const auto right = expect(TokenKind::RightBrace, "expected '}' after record members");
    if (failed) {
        return std::nullopt;
    }
    return {
        std::pair {
            right.span,
            ASTRecordDecl {
                .kind = kind,
                .visibility = visibility,
                .name_span = name.span,
                .type_parameters = std::move(type_parameters),
                .fields = std::move(fields),
                .operations = std::move(operations),
            },
        },
    };
}

auto Parser::parse_function(
    ASTDeclarationVisibility visibility,
    std::optional<ASTCppExportForm> cpp_export,
    std::optional<Span> cpp_import,
    std::optional<Span> const_span,
    std::optional<Span> async_span
) noexcept -> std::optional<std::pair<Span, ASTFunctionDecl>> {
    expect(TokenKind::Fn, "expected 'fn'");
    const auto name = expect(TokenKind::Identifier, "expected function name");
    auto type_parameters = parse_type_parameters();
    expect(TokenKind::LeftParen, "expected '(' after function name");

    auto parameters = std::vector<ASTFunctionParameter> {};
    while (!failed && !check(TokenKind::RightParen)) {
        const auto parameter_const = match(TokenKind::Const);
        auto access = ASTAccessSyntax {.mode = ASTAccessMode::Read, .marker = std::nullopt};
        if (const auto marker = match(TokenKind::Ampersand)) {
            access = {.mode = ASTAccessMode::Write, .marker = marker->span};
        } else if (const auto marker = match(TokenKind::AmpersandAmpersand)) {
            access = {.mode = ASTAccessMode::Take, .marker = marker->span};
        }
        if (!check(TokenKind::Identifier) && !check(TokenKind::LeftParen)) {
            fail_here("expected parameter name");
            return std::nullopt;
        }
        const auto target_start = current().span;
        auto target = parse_binding_target();
        if (!target) {
            return std::nullopt;
        }
        const auto target_span = join(target_start, tokens[cursor - 1uz].span);
        if (parameter_const.has_value() && access.marker.has_value()) {
            fail(
                "const parameter cannot have an access marker",
                parameter_const->span,
                DiagnosticCode::Syntax
            );
        }
        auto type = std::optional<ASTTypeID> {};
        if (match(TokenKind::Colon)) {
            type = parse_type();
        }
        const auto end = type ? builder.type(*type).span : target_span;
        const auto start = parameter_const.has_value() ? parameter_const->span
                                                       : access.marker.value_or(target_span);
        parameters.push_back({
            .span = join(start, end),
            .const_span = parameter_const.transform([](const Token& token) static noexcept {
                return token.span;
            }),
            .access = access,
            .target = std::move(*target),
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
        auto target = std::optional<ASTCppFunctionTarget>();
        if (match(TokenKind::Equal)) {
            const auto root = expect(TokenKind::ColonColon, "expected '::' before C++ target");
            auto components = std::vector<Span>();
            do {
                components.push_back(expect_name_component("expected C++ target name").span);
            } while (!failed && match(TokenKind::ColonColon));
            if (failed) {
                return std::nullopt;
            }
            target = ASTCppFunctionTarget {
                .span = join(root.span, components.back()),
                .components = std::move(components),
            };
        }
        const auto semicolon =
            expect(TokenKind::Semicolon, "expected ';' after import(cpp) declaration");
        implementation = ASTCppImportForm {.span = *cpp_import, .target = std::move(target)};
        end = semicolon.span;
    } else {
        const auto body = parse_callable_body();
        if (!body) {
            return std::nullopt;
        }
        implementation = ASTFunctionBody {.body = *body};
        end = callable_body_span(*body);
        if (std::holds_alternative<ASTExpressionBody>(*body)) {
            end = expect(TokenKind::Semicolon, "expected ';' after expression function body").span;
        }
    }
    return {
        std::pair {
            *end,
            ASTFunctionDecl {
                .is_implicit_entry = false,
                .visibility = visibility,
                .cpp_export = cpp_export,
                .const_span = const_span,
                .async_span = async_span,
                .name_span = name.span,
                .type_parameters = std::move(type_parameters),
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
        fail("a top-level constant requires a named target", name.span, DiagnosticCode::Syntax);
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
