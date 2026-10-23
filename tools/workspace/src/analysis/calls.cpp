module carven:workspace.analysis.calls.impl;

import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.stmt;
import :frontend.ast.tree;
import :semantic.analysis.source;
import :source.text;
import :workspace.analysis;
import :workspace.semantic;
import :workspace.symbols;
import std;

namespace {

using CallableKey = std::pair<std::string, Span>;

auto key(const WorkspaceCallable& callable) noexcept -> CallableKey {
    return {callable.location.document, callable.location.range};
}

auto contains(Span outer, Span inner) noexcept -> bool {
    return outer.start() <= inner.start() && inner.end() <= outer.end();
}

auto collect_callables(
    std::map<CallableKey, WorkspaceCallable>& result,
    std::string_view document,
    const WorkspaceDocumentSymbolList& symbols,
    bool member
) noexcept -> void {
    for (const auto& symbol : symbols) {
        if (symbol.kind == WorkspaceSymbolKind::Function) {
            auto callable = WorkspaceCallable {
                .location = {std::string(document), symbol.selection},
                .range = symbol.range,
                .name = symbol.name,
                .detail = {},
                .method = member
            };
            result.emplace(key(callable), std::move(callable));
        }
        collect_callables(result, document, symbol.children, true);
    }
}

// Alias bindings are local reference identities; calls use the resolved entity.
auto resolved_callable(
    const WorkspaceSemanticAnalysis& analysis,
    std::string_view document,
    std::uint32_t offset
) noexcept -> std::optional<CallableKey> {
    const auto hover = analysis.hover(document, offset);
    if (hover && hover->callable && hover->callable->target) {
        const auto target = *hover->callable->target;
        return CallableKey {
            std::string(analysis.sources().view(target.source_id).origin),
            target.span
        };
    }
    const auto* entity =
        hover && hover->entity ? std::get_if<SourceSpan>(&*hover->entity) : nullptr;
    if (!entity) {
        return std::nullopt;
    }
    return CallableKey {
        std::string(analysis.sources().view(entity->source_id).origin),
        entity->span
    };
}

} // namespace

auto WorkspaceAnalysisSnapshot::prepare_call_hierarchy(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document,
    std::uint32_t offset
) const noexcept -> WorkspaceCallHierarchyQuery {
    return call_hierarchy(project, document, offset, std::nullopt);
}

auto WorkspaceAnalysisSnapshot::incoming_calls(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document,
    std::uint32_t offset
) const noexcept -> WorkspaceCallHierarchyQuery {
    return call_hierarchy(project, document, offset, WorkspaceCallDirection::Incoming);
}

auto WorkspaceAnalysisSnapshot::outgoing_calls(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document,
    std::uint32_t offset
) const noexcept -> WorkspaceCallHierarchyQuery {
    return call_hierarchy(project, document, offset, WorkspaceCallDirection::Outgoing);
}

auto WorkspaceAnalysisSnapshot::call_hierarchy(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document,
    std::uint32_t offset,
    std::optional<WorkspaceCallDirection> direction
) const noexcept -> WorkspaceCallHierarchyQuery {
    auto result = WorkspaceCallHierarchyQuery {source_semantic(project), std::nullopt, {}};
    const auto& analysis = *result.analysis.result;
    auto callables = std::map<CallableKey, WorkspaceCallable>();
    auto entry_regions = std::map<std::string, std::vector<Span>, std::less<>>();
    for (const auto& project_module : project) {
        const auto symbols = document_symbols(project_module.document);
        if (symbols && symbols->result) {
            collect_callables(callables, project_module.document, *symbols->result, false);
        }
        const auto parsed = syntax(project_module.document);
        const auto tree = parsed ? parsed->result->recovered_syntax() : std::nullopt;
        if (!tree) {
            continue;
        }
        for (const auto& expression : tree->expressions()) {
            if (!std::holds_alternative<ASTLambdaExpr>(expression.value)) {
                continue;
            }
            const auto start = expression.span.start();
            auto callable = WorkspaceCallable {
                .location = {project_module.document, Span::from_bounds(start, start + 1)},
                .range = expression.span,
                .name = "closure",
                .detail = {},
                .method = false
            };
            callables.emplace(key(callable), std::move(callable));
        }
        for (const auto& item : tree->items()) {
            const auto* function = std::get_if<ASTFunctionDecl>(&item.value);
            if (!function || !function->is_implicit_entry) {
                continue;
            }
            auto callable = WorkspaceCallable {
                .location = {project_module.document, function->name_span},
                .range = item.span,
                .name = "main",
                .detail = "implicit entry",
                .method = false
            };
            callables.emplace(key(callable), std::move(callable));
            const auto* body = std::get_if<ASTFunctionBody>(&function->implementation);
            const auto* block = body ? std::get_if<ASTBlockID>(&body->body) : nullptr;
            if (block) {
                for (const auto statement : tree->block(*block).statements) {
                    entry_regions[project_module.document].push_back(
                        tree->statement(statement).span
                    );
                }
            }
        }
    }
    // Expansion uses the returned declaration anchor. An implicit entry has a
    // zero-width anchor that can coincide with a call in its first statement.
    auto selected = callables.end();
    if (direction) {
        selected = std::ranges::find_if(callables, [&](const auto& entry) {
            return entry.first.first == document && entry.first.second.start() == offset;
        });
    }
    if (selected == callables.end()) {
        const auto target = resolved_callable(analysis, document, offset);
        selected = target ? callables.find(*target) : callables.end();
    }
    if (selected == callables.end()) {
        // A recovered declaration may survive even when its signature has errors.
        selected = std::ranges::find_if(callables, [&](const auto& entry) {
            const auto& location = entry.second.location;
            return location.document == document
                && location.range.start() <= offset
                && offset < location.range.end();
        });
    }
    if (selected == callables.end()) {
        return result;
    }
    const auto describe = [&](WorkspaceCallable callable) {
        if (!callable.detail.empty()) {
            return callable;
        }
        const auto hover =
            analysis.hover(callable.location.document, callable.location.range.start());
        if (hover) {
            callable.detail = hover->type_description;
            if (const auto signature = callable_signature(analysis, *hover, true)) {
                callable.detail = signature->label;
            }
        }
        return callable;
    };
    result.item = describe(selected->second);
    if (!direction) {
        return result;
    }
    auto groups = std::map<CallableKey, std::vector<Span>>();
    for (const auto& project_module : project) {
        if (*direction == WorkspaceCallDirection::Outgoing
            && project_module.document != selected->first.first) {
            continue;
        }
        const auto parsed = syntax(project_module.document);
        const auto tree = parsed ? parsed->result->recovered_syntax() : std::nullopt;
        if (!tree) {
            continue;
        }
        auto owners = std::vector<decltype(callables)::iterator>();
        for (auto item = callables.lower_bound({project_module.document, Span::at(0)});
             item != callables.end() && item->first.first == project_module.document;
             ++item) {
            owners.push_back(item);
        }
        for (const auto& expression : tree->expressions()) {
            const auto* call = std::get_if<ASTCallExpr>(&expression.value);
            if (!call) {
                continue;
            }
            auto callee = call->callee;
            while (true) {
                const auto& value = tree->expression(callee).value;
                if (const auto* group = std::get_if<ASTGroupExpr>(&value)) {
                    callee = group->expression;
                } else if (const auto* application = std::get_if<ASTTypeApplicationExpr>(&value)) {
                    callee = application->operand_id;
                } else {
                    break;
                }
            }
            const auto& value = tree->expression(callee).value;
            auto name = std::optional<Span>();
            if (const auto* named = std::get_if<ASTNameExpr>(&value)) {
                name = named->name_span;
            } else if (const auto* member = std::get_if<ASTMemberExpr>(&value)) {
                name = member->name_span;
            } else if (std::holds_alternative<ASTLambdaExpr>(value)) {
                const auto start = tree->expression(callee).span.start();
                name = Span::from_bounds(start, start + 1);
            }
            const auto observed =
                analysis.call_target(project_module.document, tree->expression(call->callee).span);
            const auto target = observed
                ? std::optional(CallableKey {observed->document, observed->range})
                : name ? resolved_callable(analysis, project_module.document, name->start())
                       : std::nullopt;
            const auto called = target ? callables.find(*target) : callables.end();
            if (called == callables.end()) {
                continue;
            }
            auto caller = callables.end();
            for (const auto item : owners) {
                if (!contains(item->second.range, expression.span)) {
                    continue;
                }
                // Top-level statements may straddle declarations in source order.
                // Only statements retained in the actual entry body belong to it.
                if (item->first.second.size() == 0
                    && std::ranges::none_of(
                        entry_regions.at(project_module.document),
                        [&](Span region) { return contains(region, expression.span); }
                    )) {
                    continue;
                }
                if (caller == callables.end()
                    || item->second.range.size() < caller->second.range.size()) {
                    caller = item;
                }
            }
            if (caller == callables.end()) {
                continue;
            }
            if (*direction == WorkspaceCallDirection::Incoming && called == selected) {
                groups[caller->first].push_back(name.value_or(tree->expression(callee).span));
            } else if (*direction == WorkspaceCallDirection::Outgoing && caller == selected) {
                groups[called->first].push_back(name.value_or(tree->expression(callee).span));
            }
        }
    }
    for (auto& [identity, ranges] : groups) {
        std::ranges::sort(ranges);
        ranges.erase(std::unique(ranges.begin(), ranges.end()), ranges.end());
        result.calls.push_back({describe(callables.at(identity)), std::move(ranges)});
    }
    return result;
}
