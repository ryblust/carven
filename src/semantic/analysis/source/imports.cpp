module carven:semantic.analysis.source.imports.impl;

import :frontend.ast.decl;
import :frontend.ast.storage;
import :frontend.lex.token;
import :semantic.analysis.source;
import :semantic.analysis.source.imports;
import :semantic.analysis.types;
import :semantic.visibility;
import :source.identifier;
import :source.standard;
import :source.text;
import :support.visit;
import std;

namespace {

auto semantic_visibility(const ASTDeclarationVisibility& visibility) noexcept
    -> DeclarationVisibility {
    return visibility.visit(
        Overloaded {
            [](const ASTPrivateDeclarationVisibility&) static noexcept {
                return DeclarationVisibility::Module;
            },
            [](const ASTBareDeclarationVisibility&) static noexcept {
                return DeclarationVisibility::ModuleDomain;
            },
            [](const ASTExportDeclarationVisibility&) static noexcept {
                return DeclarationVisibility::Compilation;
            },
        }
    );
}

} // namespace

auto source_declaration_visibility(const ASTItem& item) noexcept -> DeclarationVisibility {
    return item.value.visit(
        Overloaded {
            [](const ASTEnumDecl& value) static noexcept {
                return semantic_visibility(value.visibility);
            },
            [](const ASTRecordDecl& value) static noexcept {
                return semantic_visibility(value.visibility);
            },
            [](const ASTFunctionDecl& value) static noexcept {
                return value.is_implicit_entry ? DeclarationVisibility::Module
                                               : semantic_visibility(value.visibility);
            },
            [](const ASTConstantDecl& value) static noexcept {
                return semantic_visibility(value.visibility);
            },
            [](const ASTTestDecl&) static noexcept { return DeclarationVisibility::Module; },
            [](const ASTConstBlock&) static noexcept { return DeclarationVisibility::Module; },
        }
    );
}

auto source_import_candidates(
    const ParsedDocument& document,
    const CanonicalModulePath& defining,
    const CanonicalModulePath& importer
) noexcept -> std::vector<SourceCompletionCandidate> {
    const auto syntax = document.recovered_syntax();
    auto result = std::vector<SourceCompletionCandidate>();
    if (!syntax || defining == importer) {
        return result;
    }
    for (const auto id : syntax->ast_module().items) {
        const auto& item = syntax->item(id);
        const auto visibility = source_declaration_visibility(item);
        if (visibility == DeclarationVisibility::Module
            || (visibility == DeclarationVisibility::ModuleDomain
                && !same_module_domain(defining, importer))) {
            continue;
        }
        std::visit(
            [&](const auto& form) noexcept {
                using T = std::decay_t<decltype(form)>;
                if constexpr (requires { form.name_span; }) {
                    const auto kind = std::same_as<T, ASTFunctionDecl>
                        ? SourceCompletionKind::Function
                        : std::same_as<T, ASTConstantDecl> ? SourceCompletionKind::Constant
                        : std::same_as<T, ASTEnumDecl>     ? SourceCompletionKind::Enumeration
                                                           : SourceCompletionKind::Structure;
                    result.push_back(
                        {std::string(slice(document.source().text, form.name_span)),
                         kind,
                         std::string(defining.value())}
                    );
                }
            },
            item.value
        );
    }
    return result;
}

auto source_import_candidates(const CanonicalModulePath& defining) noexcept
    -> std::vector<SourceCompletionCandidate> {
    auto result = std::vector<SourceCompletionCandidate>();
    for (const auto& provided : provided_exports()) {
        if (provided.module_local || provided_module_path(provided.provider) != defining.value()) {
            continue;
        }
        const auto type = provided_builtin_type(provided.identity)
            || provided.identity == ProvidedExport::Vector
            || provided.identity == ProvidedExport::Range
            || provided.identity == ProvidedExport::Operation;
        result.push_back(
            {std::string(provided.name),
             type ? SourceCompletionKind::Structure : SourceCompletionKind::Function,
             std::string(defining.value())}
        );
    }
    return result;
}

auto source_import_reference(
    const CanonicalModulePath& importer,
    const CanonicalModulePath& target,
    bool parent_relative
) noexcept -> std::string {
    if (parent_relative) {
        const auto from = importer.components();
        const auto to = target.components();
        if (!same_module_domain(importer, target)
            || to.size() < from.size()
            || !std::ranges::equal(
                from | std::views::take(from.size() - 1uz),
                to | std::views::take(from.size() - 1uz)
            )) {
            return {};
        }
        auto result = std::string(".");
        for (auto index = from.size() - 1uz; index < to.size(); ++index) {
            if (index != from.size() - 1uz) {
                result += '.';
            }
            result += to[index];
        }
        return result;
    }
    auto result = std::string();
    const auto relative = target.domain_relative_components();
    auto begin = 0uz;
    if (!same_module_domain(importer, target)) {
        const auto domain = target.module_domain_prefix();
        const auto craft = domain.craft_name();
        if (!craft) {
            return {};
        }
        if (*craft == "carven" && relative.front() == "std" && relative.size() > 1) {
            result = "std::";
            begin = 1;
        } else {
            result = std::string(*craft) + "::";
        }
    }
    for (auto index = begin; index < relative.size(); ++index) {
        if (index > begin) {
            result += '.';
        }
        result += relative[index];
    }
    return result;
}

auto source_import_completion(const ParsedDocument& document, std::uint32_t offset) noexcept
    -> std::optional<SourceImportCompletion> {
    const auto source = document.source();
    const auto all = document.tokens();
    auto end = 0uz;
    while (end < all.size() && all[end].span.start() < offset) {
        ++end;
    }
    auto begin = end;
    while (begin > 0uz && all[begin - 1uz].kind != TokenKind::Import) {
        --begin;
    }
    if (begin == 0uz) {
        return std::nullopt;
    }
    const auto unavailable = [&]() noexcept {
        return SourceImportCompletion {Span::at(offset), "", {}, {}, false};
    };
    const auto keyword_end = all[begin - 1uz].span.end();
    if (offset <= keyword_end) {
        return unavailable();
    }
    const auto tokens = all.subspan(begin, end - begin);
    if (tokens.empty()) {
        if (slice(source.text, Span::from_bounds(keyword_end, offset)).contains("//")) {
            return unavailable();
        }
        return SourceImportCompletion {Span::at(offset), "", {}, {}, true};
    }
    auto using_at = 0uz;
    while (using_at < tokens.size()) {
        const auto kind = tokens[using_at].kind;
        if (kind == TokenKind::Using
            && using_at > 0uz
            && tokens[using_at - 1uz].kind != TokenKind::Dot
            && tokens[using_at - 1uz].kind != TokenKind::ColonColon) {
            break;
        }
        if (!is_identifier_spelling(slice(source.text, tokens[using_at].span))
            && kind != TokenKind::Dot
            && kind != TokenKind::ColonColon) {
            return std::nullopt;
        }
        ++using_at;
    }
    if (using_at == tokens.size()) {
        const auto finish = std::max(offset, tokens.back().span.end());
        return SourceImportCompletion {
            Span::from_bounds(tokens.front().span.start(), finish),
            std::string(slice(source.text, Span::from_bounds(tokens.front().span.start(), offset))),
            {},
            {},
            true
        };
    }
    const auto path = tokens.first(using_at);
    if (path.empty()) {
        return std::nullopt;
    }
    auto components = std::vector<Span>();
    auto cursor = 0uz;
    const auto relative = path.front().kind == TokenKind::Dot;
    const auto craft = path.size() > 1uz && path[1].kind == TokenKind::ColonColon;
    if (relative) {
        cursor = 1uz;
    } else if (craft) {
        cursor = 2uz;
    }
    while (cursor < path.size()) {
        if (!is_identifier_spelling(slice(source.text, path[cursor].span))) {
            return std::nullopt;
        }
        components.push_back(path[cursor++].span);
        if (cursor < path.size() && path[cursor++].kind != TokenKind::Dot) {
            return std::nullopt;
        }
    }
    if (components.empty() || !is_identifier_spelling(slice(source.text, path.back().span))) {
        return std::nullopt;
    }
    auto reference = ASTModuleReference {
        Span::from_bounds(path.front().span.start(), path.back().span.end()),
        ASTDomainRootModuleReference {components}
    };
    if (relative) {
        reference.value = ASTParentRelativeModuleReference {path.front().span, components};
    } else if (craft) {
        reference.value = ASTCraftQualifiedModuleReference {path[0].span, path[1].span, components};
    }
    auto result = SourceImportCompletion {Span::at(offset), "", std::move(reference), {}, true};
    auto expect_name = true;
    auto alias = false;
    auto list = false;
    auto replacing = false;
    const auto names = tokens.subspan(using_at + 1uz);
    if (names.empty() && offset == tokens[using_at].span.end()) {
        return unavailable();
    }
    for (const auto& token : names) {
        if (token.kind == TokenKind::Identifier) {
            if (alias) {
                if (token.span.end() >= offset) {
                    return unavailable();
                }
                alias = false;
                continue;
            }
            if (!expect_name) {
                return unavailable();
            }
            if (token.span.end() >= offset) {
                result.replacement = token.span;
                replacing = true;
            } else {
                result.selected.emplace_back(slice(source.text, token.span));
            }
            expect_name = false;
        } else if (token.kind == TokenKind::As && !expect_name && !alias) {
            alias = true;
        } else if (token.kind == TokenKind::LeftBrace && &token == names.data()) {
            list = true;
        } else if (token.kind == TokenKind::Comma && list && !expect_name) {
            expect_name = true;
            alias = false;
        } else {
            // A terminator or a following declaration leaves the import island.
            return std::nullopt;
        }
    }
    if (alias || (!expect_name && !replacing)) {
        return unavailable();
    }
    return result;
}
