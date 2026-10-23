module carven:semantic.analysis.source.builder.impl;

import :frontend.ast.control;
import :frontend.ast.expr;
import :frontend.ast.literal;
import :semantic.analysis.catalog;
import :semantic.analysis.program;
import :semantic.analysis.source;
import :semantic.analysis.source.builder;
import :semantic.analysis.types.display;
import :semantic.semir.decl;
import std;

SourceAnalysisBuilder::SourceAnalysisBuilder(
    SourceCompletionRequest completion,
    bool source_query
) noexcept
    : completion(completion),
      query_only(source_query) {}

auto SourceAnalysisBuilder::closure(CallableID callable, SourceSpan source) noexcept -> void {
    closures.insert_or_assign(callable, source);
}

auto SourceAnalysisBuilder::callable_source(
    const ProgramDraft& draft,
    const AnalysisCatalogView& catalog,
    CallableID callable
) const noexcept -> std::optional<SourceSpan> {
    if (const auto found = closures.find(callable); found != closures.end()) {
        return found->second;
    }
    if (const auto function = draft.function_for_callable(callable)) {
        return catalog.declaration_location(draft, catalog.function_symbol(*function));
    }
    return std::nullopt;
}

auto SourceAnalysisBuilder::describe(
    ProgramDraft& draft,
    const AnalysisCatalogView& catalog
) noexcept -> void {
    if (!declarations_admitted || completion.position) {
        return;
    }

    struct Description final {
        std::string type;
        std::optional<SourceCallableInformation> callable;
    };

    // Many tokens share one type; format each resolved type once per analysis.
    auto descriptions = std::map<ConstructionTypeRef, Description>();
    const auto describe = [&](SourceOccurrenceDraft& occurrence) noexcept {
        if (!occurrence.type) {
            return;
        }
        auto [entry, inserted] = descriptions.try_emplace(*occurrence.type, Description {{}, {}});
        if (inserted) {
            auto& description = entry->second;
            description.type = type_display_name(draft, *occurrence.type, catalog);
            if (const auto contract = draft.callable_contract(*occurrence.type)) {
                auto callable = SourceCallableInformation {
                    .parameters = {},
                    .result = type_display_name(draft, contract->result, catalog),
                    .target = std::nullopt
                };
                for (const auto& parameter : contract->parameters) {
                    auto label = parameter.stage == ParameterStage::Static ? std::string("const ")
                                                                           : std::string();
                    label += parameter.access == AccessMode::Write ? "&"
                        : parameter.access == AccessMode::Take     ? "&&"
                                                                   : "";
                    label += type_display_name(draft, parameter.type, catalog);
                    callable.parameters.push_back(std::move(label));
                }
                if (const auto* concrete = std::get_if<TypeID>(&*occurrence.type)) {
                    const auto type = draft.type_copy(*concrete);
                    if (const auto* function = std::get_if<FunctionTypeValue>(&type.value)) {
                        callable.target = callable_source(draft, catalog, function->callable);
                    } else if (const auto* closure = std::get_if<ClosureTypeValue>(&type.value)) {
                        callable.target = callable_source(draft, catalog, closure->callable);
                    }
                }
                description.callable = std::move(callable);
            }
        }
        occurrence.type_description = entry->second.type;
        occurrence.callable = entry->second.callable;
        if (occurrence.callable && occurrence.call_target) {
            occurrence.callable->target = callable_source(draft, catalog, *occurrence.call_target);
        }
    };
    for (auto& [key, occurrence] : occurrences) {
        describe(occurrence);
    }
    for (auto& occurrence : declaration_observations.occurrences) {
        describe(occurrence);
    }
}

auto SourceAnalysisBuilder::is_source_query() const noexcept -> bool {
    return query_only;
}

auto SourceAnalysisBuilder::excludes(SourceSpan location) const noexcept -> bool {
    return query_only && std::ranges::contains(completion.excluded, location);
}

auto SourceAnalysisBuilder::requests_completion(SourceSpan location) const noexcept -> bool {
    return completion.output
        && completion.position
        && completion.position->source_id == location.source_id
        && location.span.start() <= completion.position->span.start()
        && completion.position->span.start() < location.span.end();
}

auto SourceAnalysisBuilder::complete(
    std::span<const SourceCompletionCandidate> candidates,
    SourceCompletionContext context
) const noexcept -> void {
    if (completion.output) {
        completion.output(candidates, context);
    }
}

auto SourceAnalysisBuilder::declarations() noexcept -> SourceObservation& {
    return declaration_observations;
}

auto SourceAnalysisBuilder::admit_declarations() noexcept -> void {
    declarations_admitted = true;
}

auto SourceAnalysisBuilder::discard() noexcept -> void {
    declarations_admitted = false;
}

auto SourceAnalysisBuilder::add(SourceOccurrenceDraft occurrence) noexcept -> void {
    const auto key = std::pair(occurrence.location.source_id, occurrence.location.span);
    const auto [found, inserted] = occurrences.try_emplace(key, occurrence);
    if (inserted) {
        return;
    }
    if (occurrence.entity) {
        found->second.entity = occurrence.entity;
    }
    if (!occurrence.documentation.empty()) {
        found->second.documentation = std::move(occurrence.documentation);
        if (!occurrence.type_description.empty()) {
            found->second.type_description = std::move(occurrence.type_description);
        }
    }
    if (occurrence.call_target) {
        found->second.call_target = occurrence.call_target;
    }
    if (occurrence.external_binding) {
        found->second.external_binding = occurrence.external_binding;
    }
    if (occurrence.alias) {
        found->second.alias = occurrence.alias;
    }
    if (occurrence.type) {
        found->second.type = occurrence.type;
        found->second.builtin_type = occurrence.builtin_type;
        found->second.type_description = std::move(occurrence.type_description);
    }
}

auto SourceAnalysisBuilder::add_body(SourceObservation&& body) noexcept -> void {
    for (const auto& occurrence : body.occurrences) {
        add(occurrence);
    }
    body.occurrences.clear();
}

auto SourceAnalysisBuilder::resolve_types(const TypeResolution& types) noexcept -> void {
    const auto resolve = [&](SourceOccurrenceDraft& occurrence) noexcept {
        if (occurrence.type) {
            occurrence.type = types.normalize(*occurrence.type);
        }
    };
    for (auto& [key, occurrence] : occurrences) {
        resolve(occurrence);
    }
    for (auto& occurrence : declaration_observations.occurrences) {
        resolve(occurrence);
    }
}

auto SourceAnalysisBuilder::complete_construction() noexcept -> void {
    construction_completed = true;
}

auto SourceAnalysisBuilder::construction_complete() const noexcept -> bool {
    return construction_completed;
}

auto SourceAnalysisBuilder::finish(bool published) && noexcept -> std::vector<SourceOccurrence> {
    auto result = std::vector<SourceOccurrence>();
    if (!declarations_admitted) {
        return result;
    }
    add_body(std::move(declaration_observations));
    result.reserve(occurrences.size());
    for (const auto& [key, occurrence] : occurrences) {
        auto type = std::optional<SourceType>();
        const auto* resolved =
            published && occurrence.type ? std::get_if<TypeID>(&*occurrence.type) : nullptr;
        if (resolved != nullptr) {
            type = *resolved;
        } else if (occurrence.builtin_type) {
            type = *occurrence.builtin_type;
        }
        result.push_back(
            {.location = occurrence.location,
             .type = type,
             .entity = occurrence.entity,
             .alias = occurrence.alias,
             .type_description = occurrence.type_description,
             .callable = occurrence.callable,
             .documentation = occurrence.documentation,
             .external_binding = occurrence.external_binding}
        );
    }
    return result;
}

auto SourceObservation::record(
    const ProgramDraft& draft,
    SourceSpan location,
    std::optional<SourceEntity> entity,
    std::optional<ConstructionTypeRef> type,
    std::optional<SourceSpan> alias,
    std::optional<CallableID> target
) noexcept -> void {
    if (location.span.empty()) {
        return;
    }
    auto builtin = std::optional<BuiltinType>();
    if (type) {
        if (const auto* concrete = std::get_if<TypeID>(&*type)) {
            const auto canonical = draft.type_copy(*concrete);
            if (const auto* value = std::get_if<BuiltinTypeValue>(&canonical.value)) {
                builtin = value->kind;
            }
        }
    }
    occurrences.push_back(
        {.location = location,
         .type = type,
         .builtin_type = builtin,
         .entity = entity,
         .alias = alias,
         .type_description = {},
         .callable = std::nullopt,
         .call_target = target,
         .documentation = {},
         .external_binding = std::nullopt}
    );
}

auto SourceObservation::call(
    const ProgramDraft& draft,
    ASTView syntax,
    ASTExprID callee,
    ConstructionTypeRef type,
    std::optional<CallableID> target
) noexcept -> void {
    const auto span = syntax.expression(callee).span;
    if (span.empty()) {
        return;
    }
    record(draft, locate(syntax.source_id(), span), std::nullopt, type);
    occurrences.back().call_target = target;
}

auto SourceObservation::external(
    SourceSpan location,
    std::string path,
    std::string documentation,
    std::optional<SourceSpan> binding
) noexcept -> void {
    occurrences.push_back(
        {.location = location,
         .type = std::nullopt,
         .builtin_type = std::nullopt,
         .entity = std::nullopt,
         .alias = std::nullopt,
         .type_description = std::move(path),
         .callable = std::nullopt,
         .call_target = std::nullopt,
         .documentation = std::move(documentation),
         .external_binding = binding}
    );
}

auto SourceObservation::merge(SourceObservation&& child) noexcept -> void {
    for (const auto& occurrence : child.occurrences) {
        occurrences.push_back(occurrence);
    }
    child.occurrences.clear();
}

auto SourceObservation::expression(
    const ProgramDraft& draft,
    ASTView syntax,
    ASTExprID id,
    ConstructionTypeRef type
) noexcept -> void {
    while (true) {
        const auto& expression = syntax.expression(id).value;
        if (const auto* group = std::get_if<ASTGroupExpr>(&expression)) {
            id = group->expression;
        } else if (const auto* application = std::get_if<ASTTypeApplicationExpr>(&expression)) {
            id = application->operand_id;
        } else {
            break;
        }
    }
    // Operation types attach to their own tokens, never to arbitrary offsets
    // within operands, bodies, or annotations.
    const auto anchor = syntax.expression(id).value.visit(
        [](const auto& form) static noexcept -> std::optional<Span> {
            using Form = std::remove_cvref_t<decltype(form)>;
            if constexpr (std::same_as<Form, ASTLiteral>) {
                return form.span;
            } else if constexpr (requires { form.name_span; }) {
                return form.name_span;
            } else if constexpr (requires { form.operator_span; }) {
                return form.operator_span;
            } else if constexpr (std::same_as<Form, ASTAccessExpr>) {
                return form.marker_span;
            } else if constexpr (std::same_as<Form, ASTIfForm>) {
                return form.branches.front().keyword_span;
            } else if constexpr (std::same_as<Form, ASTMatchForm>) {
                return form.keyword_span;
            } else if constexpr (std::same_as<Form, ASTTryForm>) {
                return form.try_span;
            } else {
                return std::nullopt;
            }
        }
    );
    if (anchor) {
        record(draft, locate(syntax.source_id(), *anchor), std::nullopt, type);
    } else if (std::holds_alternative<ASTLambdaExpr>(syntax.expression(id).value)) {
        const auto start = syntax.expression(id).span.start();
        record(
            draft,
            locate(syntax.source_id(), Span::from_bounds(start, start + 1)),
            std::nullopt,
            type
        );
    }
}
