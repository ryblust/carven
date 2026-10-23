module carven:workspace.analysis.signature.impl;

import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.tree;
import :frontend.lex.token;
import :source.text;
import :workspace.analysis;
import std;

namespace {

struct CallSite final {
    Span name;
};

struct ArgumentFrame final {
    TokenKind closing;
    std::optional<CallSite> call;
    std::uint32_t argument;
};

auto closing(TokenKind kind) noexcept -> TokenKind {
    switch (kind) {
        case TokenKind::LeftParen:          return TokenKind::RightParen;
        case TokenKind::LeftBracket:        return TokenKind::RightBracket;
        case TokenKind::LeftBrace:          return TokenKind::RightBrace;
        case TokenKind::InterpolationStart: return TokenKind::InterpolationEnd;
        case TokenKind::InterpolationOpen:  return TokenKind::InterpolationClose;
        default:                            return TokenKind::Invalid;
    }
}

} // namespace

auto WorkspaceAnalysisSnapshot::callable_signature(
    const WorkspaceSemanticAnalysis& analysis,
    const WorkspaceHoverInformation& hover,
    bool declaration
) const noexcept -> std::optional<WorkspaceSignatureInformation> {
    const auto source = analysis.source(hover.location.document);
    if (!source) {
        return std::nullopt;
    }
    auto signature = WorkspaceSignatureInformation {
        .label = {},
        .parameters = {},
        .active_parameter = std::nullopt
    };
    const auto name = std::string(slice(source->text, hover.location.range));
    if (name == "[") {
        // Anonymous callable anchors use the compiler's detached type label.
        return std::nullopt;
    }
    auto prefix = std::string();
    auto generics = std::string();
    auto suffix = std::string();
    auto named = false;
    auto bound = false;
    if (const auto document = syntax(hover.location.document)) {
        if (const auto tree = document->result->recovered_syntax()) {
            for (const auto& expression : tree->expressions()) {
                if (const auto* member = std::get_if<ASTMemberExpr>(&expression.value)) {
                    if (member->name_span == hover.location.range
                        && member->op != ASTMemberOperator::Scope) {
                        bound = true;
                        break;
                    }
                }
            }
        }
    }
    const auto* entity = hover.entity ? std::get_if<SourceSpan>(&*hover.entity) : nullptr;
    if (entity) {
        const auto target = analysis.sources().view(entity->source_id);
        const auto parsed = syntax(target.origin);
        const auto tree = parsed ? parsed->result->recovered_syntax() : std::nullopt;
        if (tree) {
            for (const auto& item : tree->items()) {
                const auto* function = std::get_if<ASTFunctionDecl>(&item.value);
                if (!function || function->name_span != entity->span) {
                    continue;
                }
                named = true;
                if (declaration && function->const_span) {
                    prefix += "const ";
                }
                if (function->async_span) {
                    prefix += "async ";
                }
                if (function->type_parameters) {
                    generics = slice(target.text, function->type_parameters->span);
                }
                for (const auto& parameter : function->parameters) {
                    const auto* binding = std::get_if<ASTNamedBindingTarget>(&parameter.target);
                    if (bound
                        && &parameter == &function->parameters.front()
                        && binding
                        && slice(target.text, binding->name_span) == "self") {
                        continue;
                    }
                    signature.parameters.emplace_back(slice(target.text, parameter.span));
                }
                if (function->result_type) {
                    suffix = " -> "
                        + std::string(slice(target.text, tree->type(*function->result_type).span));
                } else {
                    if (hover.callable) {
                        suffix = " -> " + hover.callable->result;
                    }
                }
                if (function->throw_clause) {
                    suffix += " " + std::string(slice(target.text, function->throw_clause->span));
                }
                break;
            }
        }
    }
    if (!named) {
        if (!hover.callable) {
            return std::nullopt;
        }
        signature.parameters = hover.callable->parameters;
        suffix = " -> " + hover.callable->result;
    }
    signature.label = prefix + (declaration ? "fn " : "") + name + generics + '(';
    for (auto index = 0uz; index < signature.parameters.size(); ++index) {
        if (index != 0) {
            signature.label += ", ";
        }
        signature.label += signature.parameters[index];
    }
    signature.label += ')' + suffix;
    return signature;
}

auto WorkspaceAnalysisSnapshot::signature_help(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document,
    std::uint32_t offset
) const noexcept -> WorkspaceSignatureQuery {
    auto result = WorkspaceSignatureQuery {{nullptr, {}}, std::nullopt};
    const auto parsed = syntax(document);
    if (!parsed || offset > parsed->result->source().text.size()) {
        return result;
    }
    const auto ast = parsed->result->recovered_syntax();
    if (!ast) {
        return result;
    }
    const auto source = parsed->result->source();
    const auto tokens = parsed->result->tokens();
    auto sites = std::map<std::uint32_t, CallSite>();
    auto type_arguments = std::vector<Span>();
    for (const auto& expression : ast->expressions()) {
        if (const auto* application = std::get_if<ASTTypeApplicationExpr>(&expression.value)) {
            type_arguments.push_back(
                Span::from_bounds(
                    ast->expression(application->operand_id).span.end(),
                    expression.span.end()
                )
            );
        }
        const auto* call = std::get_if<ASTCallExpr>(&expression.value);
        if (!call) {
            continue;
        }
        const auto& callee = ast->expression(call->callee);
        const auto open =
            std::ranges::lower_bound(tokens, callee.span.end(), {}, [](const auto& token) {
                return token.span.start();
            });
        if (open == tokens.end() || open->kind != TokenKind::LeftParen) {
            continue;
        }
        auto id = call->callee;
        while (true) {
            const auto& form = ast->expression(id).value;
            if (const auto* group = std::get_if<ASTGroupExpr>(&form)) {
                id = group->expression;
            } else if (const auto* application = std::get_if<ASTTypeApplicationExpr>(&form)) {
                id = application->operand_id;
            } else {
                break;
            }
        }
        const auto& form = ast->expression(id).value;
        if (const auto* name = std::get_if<ASTNameExpr>(&form)) {
            sites.emplace(open->span.start(), CallSite {name->name_span});
        } else if (const auto* member = std::get_if<ASTMemberExpr>(&form)) {
            sites.emplace(open->span.start(), CallSite {member->name_span});
        }
    }
    auto frames = std::vector<ArgumentFrame>();
    auto preceding = 0u;
    for (const auto& token : tokens) {
        if (token.span.start() >= offset) {
            break;
        }
        if (token.span.start() < offset
            && offset < token.span.end()
            && (token.kind == TokenKind::StringLiteral
                || token.kind == TokenKind::CharLiteral
                || token.kind == TokenKind::CStringLiteral
                || token.kind == TokenKind::InterpolationText
                || token.kind == TokenKind::CppSourceFragment
                || token.kind == TokenKind::Invalid)) {
            return result;
        }
        preceding = token.span.end();
        if (const auto end = closing(token.kind); end != TokenKind::Invalid) {
            const auto found = sites.find(token.span.start());
            frames.push_back(
                {.closing = end,
                 .call = found == sites.end() ? std::nullopt : std::optional(found->second),
                 .argument = 0}
            );
        } else if (!frames.empty() && token.kind == frames.back().closing) {
            frames.pop_back();
        } else if (token.kind == TokenKind::Comma
                   && !frames.empty()
                   && frames.back().call
                   && std::ranges::none_of(type_arguments, [&](Span range) {
                          return range.start() <= token.span.start()
                              && token.span.end() <= range.end();
                      })) {
            ++frames.back().argument;
        }
    }
    // Discarded line comments have no token. A cursor inside their trivia must
    // not ask for the signature of an earlier open call.
    if (preceding <= offset) {
        const auto trivia = source.text.substr(preceding, offset - preceding);
        const auto comment = trivia.rfind("//");
        if (comment != std::string_view::npos
            && trivia.substr(comment).find_first_of("\r\n") == std::string_view::npos) {
            return result;
        }
    }
    for (const auto& frame : frames | std::views::reverse) {
        if (!frame.call) {
            continue;
        }
        const auto hover = this->hover(project, document, frame.call->name.start());
        if (!hover.result) {
            continue;
        }
        auto described = callable_signature(*hover.analysis.result, *hover.result, false);
        if (!described) {
            continue;
        }
        auto signature = std::move(*described);
        if (!signature.parameters.empty()) {
            signature.active_parameter = std::min(
                frame.argument,
                static_cast<std::uint32_t>(signature.parameters.size() - 1)
            );
        }
        result.analysis = hover.analysis;
        result.result = std::move(signature);
        return result;
    }
    return result;
}
