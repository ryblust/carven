module carven:workspace.decorations.impl;

import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.tree;
import :frontend.lex.token;
import :semantic.analysis.types;
import :source.standard;
import :source.text;
import :workspace.analysis;
import :workspace.decorations;
import std;

namespace {

using Kind = WorkspaceHighlightKind;
using Modifier = WorkspaceHighlightModifier;

constexpr auto flag(Modifier modifier) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(modifier);
}

struct Declaration final {
    Kind kind;
    std::uint32_t modifiers;
};

using Key = std::pair<std::string, Span>;

class DecorationCollector final {
public:
    DecorationCollector(
        const WorkspaceAnalysisSnapshot& snapshot,
        std::span<const WorkspaceProjectModule> project,
        const WorkspaceSemanticAnalysis& analysis
    ) noexcept;
    auto collect(std::string_view document, Span visible) const noexcept -> WorkspaceDecorations;

private:
    auto add(std::string_view document, Span name, Kind kind, std::uint32_t modifiers = 0) noexcept
        -> void;
    auto binding(
        std::string_view document,
        const ASTBindingTarget& target,
        Kind kind,
        std::uint32_t modifiers
    ) noexcept -> void;
    auto parameters(
        std::string_view document,
        std::span<const ASTFunctionParameter> values
    ) noexcept -> void;
    auto type_parameters(
        std::string_view document,
        const std::optional<ASTTypeParameterClause>& values
    ) noexcept -> void;
    auto classify(const SourceOccurrence& fact) const noexcept -> std::optional<Declaration>;
    auto key(SourceSpan source) const noexcept -> Key;
    auto callee(ASTView syntax, ASTExprID id) const noexcept -> std::pair<Span, bool>;

    const WorkspaceSemanticAnalysis& analysis;
    std::map<std::string, std::shared_ptr<const ParsedDocument>, std::less<>> documents;
    std::map<Key, Declaration> declarations;
    std::map<Key, const ASTFunctionDecl*> functions;
    std::set<std::string, std::less<>> standard;
};

auto DecorationCollector::add(
    std::string_view document,
    Span name,
    Kind kind,
    std::uint32_t modifiers
) noexcept -> void {
    declarations.insert_or_assign(Key(std::string(document), name), Declaration {kind, modifiers});
}

auto DecorationCollector::binding(
    std::string_view document,
    const ASTBindingTarget& target,
    Kind kind,
    std::uint32_t modifiers
) noexcept -> void {
    if (const auto* named = std::get_if<ASTNamedBindingTarget>(&target)) {
        add(document, named->name_span, kind, modifiers);
    } else if (const auto* tuple = std::get_if<UniqueIndirect<ASTTupleBindingTarget>>(&target)) {
        for (const auto& element : (*tuple)->elements) {
            binding(document, element, kind, modifiers);
        }
    }
}

auto DecorationCollector::parameters(
    std::string_view document,
    std::span<const ASTFunctionParameter> values
) noexcept -> void {
    for (const auto& parameter : values) {
        binding(
            document,
            parameter.target,
            Kind::Parameter,
            (parameter.access.mode == ASTAccessMode::Read ? flag(Modifier::Readonly) : 0)
                | (parameter.const_span ? flag(Modifier::Static) : 0)
        );
    }
}

auto DecorationCollector::type_parameters(
    std::string_view document,
    const std::optional<ASTTypeParameterClause>& values
) noexcept -> void {
    if (values) {
        for (const auto& parameter : values->parameters) {
            add(document, parameter.name_span, Kind::TypeParameter);
        }
    }
}

DecorationCollector::DecorationCollector(
    const WorkspaceAnalysisSnapshot& snapshot,
    std::span<const WorkspaceProjectModule> project,
    const WorkspaceSemanticAnalysis& analysis
) noexcept
    : analysis(analysis) {
    for (const auto& project_module : project) {
        if (!analysis.source(project_module.document)) {
            continue;
        }
        const auto parsed = snapshot.syntax(project_module.document);
        if (!parsed) {
            continue;
        }
        documents.insert_or_assign(project_module.document, parsed->result);
        if (project_module.module_path.value().starts_with("crafts.carven.")) {
            standard.insert(project_module.document);
        }
        const auto syntax = parsed->result->recovered_syntax();
        if (!syntax) {
            continue;
        }
        auto methods = std::set<Span>();
        for (const auto& item : syntax->items()) {
            if (const auto* record = std::get_if<ASTRecordDecl>(&item.value)) {
                add(project_module.document,
                    record->name_span,
                    record->kind == ASTRecordKind::Class ? Kind::Class : Kind::Structure);
                type_parameters(project_module.document, record->type_parameters);
                for (const auto& field : record->fields) {
                    add(project_module.document, field.name_span, Kind::Property);
                }
                for (const auto operation : record->operations) {
                    const auto* function =
                        std::get_if<ASTFunctionDecl>(&syntax->item(operation).value);
                    if (function) {
                        methods.insert(function->name_span);
                    }
                }
            } else if (const auto* enumeration = std::get_if<ASTEnumDecl>(&item.value)) {
                add(project_module.document, enumeration->name_span, Kind::Enumeration);
                type_parameters(project_module.document, enumeration->type_parameters);
                for (const auto& entry : enumeration->cases) {
                    add(project_module.document,
                        entry.name_span,
                        Kind::EnumMember,
                        flag(Modifier::Readonly));
                }
            } else if (const auto* constant = std::get_if<ASTConstantDecl>(&item.value)) {
                add(project_module.document,
                    constant->name_span,
                    Kind::Variable,
                    flag(Modifier::Readonly) | flag(Modifier::Static));
            }
        }
        for (const auto& item : syntax->items()) {
            if (const auto* function = std::get_if<ASTFunctionDecl>(&item.value);
                function && !function->is_implicit_entry) {
                add(project_module.document,
                    function->name_span,
                    methods.contains(function->name_span) ? Kind::Method : Kind::Function,
                    (function->async_span ? flag(Modifier::Async) : 0)
                        | (function->const_span ? flag(Modifier::Static) : 0));
                functions.emplace(Key(project_module.document, function->name_span), function);
                type_parameters(project_module.document, function->type_parameters);
                parameters(project_module.document, function->parameters);
            }
        }
        const auto variable = [&](const ASTVariableDecl& declaration) noexcept {
            binding(
                project_module.document,
                declaration.target,
                Kind::Variable,
                (declaration.kind != ASTBindingKind::Var ? flag(Modifier::Readonly) : 0)
                    | (declaration.kind == ASTBindingKind::Const ? flag(Modifier::Static) : 0)
            );
        };
        for (const auto& statement : syntax->statements()) {
            if (const auto* declaration = std::get_if<ASTVariableDecl>(&statement.value)) {
                variable(*declaration);
            }
            if (const auto* loop = std::get_if<ASTForStmt>(&statement.value)) {
                if (const auto* range = std::get_if<ASTRangeForHeader>(&loop->header.value)) {
                    binding(
                        project_module.document,
                        range->target,
                        Kind::Variable,
                        range->write_marker ? 0 : flag(Modifier::Readonly)
                    );
                } else if (const auto* cstyle =
                               std::get_if<ASTCStyleForHeader>(&loop->header.value)) {
                    if (const auto* declaration =
                            std::get_if<ASTVariableDecl>(&cstyle->initializer.value)) {
                        variable(*declaration);
                    }
                }
            }
        }
        for (const auto& pattern : syntax->patterns()) {
            if (const auto* binding = std::get_if<ASTBindingPattern>(&pattern.value)) {
                add(project_module.document,
                    binding->name_span,
                    Kind::Variable,
                    binding->mode == ASTAccessMode::Read ? flag(Modifier::Readonly) : 0);
            }
        }
        for (const auto& expression : syntax->expressions()) {
            if (const auto* lambda = std::get_if<ASTLambdaExpr>(&expression.value)) {
                parameters(project_module.document, lambda->parameters);
            }
        }
    }
}

auto DecorationCollector::key(SourceSpan source) const noexcept -> Key {
    return {std::string(analysis.sources().view(source.source_id).origin), source.span};
}

auto DecorationCollector::classify(const SourceOccurrence& fact) const noexcept
    -> std::optional<Declaration> {
    if (!fact.entity) {
        return std::nullopt;
    }
    if (const auto* source = std::get_if<SourceSpan>(&*fact.entity)) {
        const auto identity = key(*source);
        const auto found = declarations.find(identity);
        if (found != declarations.end()) {
            auto declaration = found->second;
            if (standard.contains(identity.first)) {
                declaration.modifiers |= flag(Modifier::DefaultLibrary);
            }
            return declaration;
        }
    } else if (const auto* provided = std::get_if<ProvidedExport>(&*fact.entity)) {
        const auto type = provided_builtin_type(*provided).has_value()
            || *provided == ProvidedExport::Vector
            || *provided == ProvidedExport::Range
            || *provided == ProvidedExport::Operation;
        return Declaration {type ? Kind::Type : Kind::Function, flag(Modifier::DefaultLibrary)};
    }
    return std::nullopt;
}

auto DecorationCollector::callee(ASTView syntax, ASTExprID id) const noexcept
    -> std::pair<Span, bool> {
    while (true) {
        const auto& value = syntax.expression(id).value;
        if (const auto* group = std::get_if<ASTGroupExpr>(&value)) {
            id = group->expression;
        } else if (const auto* application = std::get_if<ASTTypeApplicationExpr>(&value)) {
            id = application->operand_id;
        } else if (const auto* name = std::get_if<ASTNameExpr>(&value)) {
            return {name->name_span, false};
        } else if (const auto* member = std::get_if<ASTMemberExpr>(&value)) {
            return {member->name_span, member->op != ASTMemberOperator::Scope};
        } else {
            return {Span::at(0), false};
        }
    }
}

auto DecorationCollector::collect(std::string_view document, Span visible) const noexcept
    -> WorkspaceDecorations {
    auto result = WorkspaceDecorations {{}, {}};
    const auto found = documents.find(document);
    if (found == documents.end()) {
        return result;
    }
    const auto parsed = found->second;
    const auto syntax = parsed->recovered_syntax();
    if (!syntax) {
        return result;
    }
    auto identifiers = std::set<Span>();
    for (const auto& token : parsed->tokens()) {
        if (token.kind == TokenKind::Identifier) {
            identifiers.insert(token.span);
        }
    }
    auto facts = std::map<Span, const SourceOccurrence*>();
    for (const auto& fact : analysis.occurrences(document)) {
        facts.emplace(fact.location.span, &fact);
    }
    auto highlights = std::map<Span, Declaration>();
    for (const auto& [identity, declaration] : declarations) {
        if (identity.first == document) {
            highlights.emplace(
                identity.second,
                Declaration {declaration.kind, declaration.modifiers | flag(Modifier::Declaration)}
            );
        }
    }
    for (const auto& [range, fact] : facts) {
        if (const auto declaration = classify(*fact)) {
            auto value = *declaration;
            if (fact->location == fact->definition()) {
                value.modifiers |= flag(Modifier::Declaration);
            }
            highlights.insert_or_assign(range, value);
        }
    }
    for (const auto& [range, declaration] : highlights) {
        if (identifiers.contains(range)
            && visible.start() <= range.start()
            && range.end() <= visible.end()) {
            result.highlights.push_back({range, declaration.kind, declaration.modifiers});
        }
    }
    const auto hint_type = [&](const ASTVariableDecl& declaration) noexcept {
        if (declaration.type) {
            return;
        }
        const auto visit = [&](this const auto& visit,
                               const ASTBindingTarget& target) noexcept -> void {
            if (const auto* named = std::get_if<ASTNamedBindingTarget>(&target)) {
                const auto fact = facts.find(named->name_span);
                if (fact == facts.end() || fact->second->type_description.empty()) {
                    return;
                }
                const auto position = named->name_span.end();
                if (visible.start() <= position && position < visible.end()) {
                    result.hints.push_back(
                        {position, ": " + fact->second->type_description, WorkspaceHintKind::Type}
                    );
                }
            } else if (const auto* tuple =
                           std::get_if<UniqueIndirect<ASTTupleBindingTarget>>(&target)) {
                for (const auto& element : (*tuple)->elements) {
                    visit(element);
                }
            }
        };
        visit(declaration.target);
    };
    for (const auto& statement : syntax->statements()) {
        if (const auto* declaration = std::get_if<ASTVariableDecl>(&statement.value)) {
            hint_type(*declaration);
        }
        if (const auto* loop = std::get_if<ASTForStmt>(&statement.value)) {
            if (const auto* cstyle = std::get_if<ASTCStyleForHeader>(&loop->header.value)) {
                if (const auto* declaration =
                        std::get_if<ASTVariableDecl>(&cstyle->initializer.value)) {
                    hint_type(*declaration);
                }
            }
        }
    }
    for (const auto& expression : syntax->expressions()) {
        const auto* call = std::get_if<ASTCallExpr>(&expression.value);
        if (!call) {
            continue;
        }
        const auto [name, bound] = callee(*syntax, call->callee);
        const auto fact = facts.find(name);
        if (fact == facts.end() || !fact->second->entity) {
            continue;
        }
        const auto* source = std::get_if<SourceSpan>(&*fact->second->entity);
        if (!source) {
            continue;
        }
        const auto function = functions.find(key(*source));
        if (function == functions.end()) {
            continue;
        }
        const auto& parameters = function->second->parameters;
        const auto declaration = analysis.sources().view(source->source_id);
        const auto* first = parameters.empty()
            ? nullptr
            : std::get_if<ASTNamedBindingTarget>(&parameters.front().target);
        const auto offset =
            bound && first && slice(declaration.text, first->name_span) == "self" ? 1uz : 0uz;
        for (auto index = 0uz; index < call->arguments.size() && index + offset < parameters.size();
             ++index) {
            const auto* parameter =
                std::get_if<ASTNamedBindingTarget>(&parameters[index + offset].target);
            if (!parameter) {
                continue;
            }
            const auto& argument = syntax->expression(call->arguments[index]);
            const auto position = argument.span.start();
            if (position < visible.start() || visible.end() <= position) {
                continue;
            }
            const auto label = slice(declaration.text, parameter->name_span);
            if (label == "self" || label == "_" || label.empty()) {
                continue;
            }
            if (const auto* named = std::get_if<ASTNameExpr>(&argument.value);
                named && slice(parsed->source().text, named->name_span) == label) {
                continue;
            }
            result.hints.push_back(
                {position, std::string(label) + ':', WorkspaceHintKind::Parameter}
            );
        }
    }
    std::ranges::sort(result.hints, [](const auto& a, const auto& b) noexcept {
        return std::pair(a.position, a.kind) < std::pair(b.position, b.kind);
    });
    result.hints.erase(
        std::unique(
            result.hints.begin(),
            result.hints.end(),
            [](const auto& a, const auto& b) noexcept {
                return a.position == b.position && a.label == b.label && a.kind == b.kind;
            }
        ),
        result.hints.end()
    );
    return result;
}

} // namespace

auto collect_workspace_decorations(
    const WorkspaceAnalysisSnapshot& snapshot,
    std::span<const WorkspaceProjectModule> project,
    std::string_view document,
    Span visible,
    const WorkspaceSemanticAnalysis& analysis
) noexcept -> WorkspaceDecorations {
    return DecorationCollector(snapshot, project, analysis).collect(document, visible);
}
