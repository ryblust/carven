module carven:semantic.analysis.source.builder.impl;

import :frontend.ast.control;
import :frontend.ast.expr;
import :frontend.ast.literal;
import :semantic.analysis.program;
import :semantic.analysis.source;
import :semantic.analysis.source.builder;
import std;

auto SourceAnalysisBuilder::declarations() noexcept -> SourceObservation& {
    return declaration_observations;
}

auto SourceAnalysisBuilder::admit_declarations() noexcept -> void {
    declarations_admitted = true;
}

auto SourceAnalysisBuilder::add(SourceOccurrenceDraft occurrence) noexcept -> void {
    const auto key = std::pair(occurrence.location.source_id, occurrence.location.span);
    const auto [found, inserted] = occurrences.try_emplace(key, occurrence);
    if (inserted) {
        return;
    }
    if (occurrence.definition) {
        found->second.definition = occurrence.definition;
    }
    if (occurrence.type) {
        found->second.type = occurrence.type;
        found->second.builtin_type = occurrence.builtin_type;
    }
}

auto SourceAnalysisBuilder::add_body(SourceObservation&& body) noexcept -> void {
    for (const auto& occurrence : body.occurrences) {
        add(occurrence);
    }
}

auto SourceAnalysisBuilder::resolve_types(const TypeResolution& types) noexcept -> void {
    const auto resolve = [&](SourceOccurrenceDraft& occurrence) noexcept {
        if (occurrence.type) {
            occurrence.type = types.resolve(*occurrence.type);
        }
    };
    for (auto& [key, occurrence] : occurrences) {
        resolve(occurrence);
    }
    for (auto& occurrence : declaration_observations.occurrences) {
        resolve(occurrence);
    }
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
            {.location = occurrence.location, .definition = occurrence.definition, .type = type}
        );
    }
    return result;
}

auto SourceObservation::record(
    const ProgramDraft& draft,
    SourceSpan location,
    std::optional<SourceSpan> definition,
    std::optional<ConstructionTypeRef> type
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
        {.location = location, .definition = definition, .type = type, .builtin_type = builtin}
    );
}

auto SourceObservation::merge(SourceObservation&& child) noexcept -> void {
    for (auto& occurrence : child.occurrences) {
        occurrences.push_back(std::move(occurrence));
    }
}

auto SourceObservation::expression(
    const ProgramDraft& draft,
    ASTView syntax,
    ASTExprID id,
    ConstructionTypeRef type
) noexcept -> void {
    while (const auto* group = std::get_if<ASTGroupExpr>(&syntax.expression(id).value)) {
        id = group->expression;
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
    }
}
