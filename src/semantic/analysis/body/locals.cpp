module carven:semantic.analysis.body.locals.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.decl;
import :semantic.analysis.body.context;
import :semantic.analysis.catalog;
import :semantic.semir.body;
import :semantic.semir.decl;
import :support.invariant;
import std;

auto BodyElaborator::push_frame(Span span) noexcept -> void {
    const auto frame_origin = origin(span);
    const auto parent_lifetime = active_full_expression.value_or(frames.back().lifetime);
    frames.push_back(
        BodyLocalFrame {
            .lifetime = body_builder.add_lifetime_region(
                parent_lifetime,
                LifetimeRegionKind::Lexical,
                frame_origin
            ),
            .names = {},
        }
    );
}

auto BodyElaborator::pop_frame(bool diagnose) noexcept -> void {
    if (frames.size() <= 1uz) {
        invariant_violation("body producer popped its root frame");
    }
    if (diagnose) {
        collect_unused_locals(frames.back());
    }
    frames.pop_back();
}

auto BodyElaborator::collect_unused_locals(const BodyLocalFrame& frame) noexcept -> void {
    for (const auto& [name, storage] : frame.names) {
        static_cast<void>(name);
        if (!storage.used && storage.unused_candidate && storage.role != BodyLocalRole::Capture) {
            const auto span = *storage.unused_candidate;
            const auto parameter = storage.role == BodyLocalRole::Parameter;
            batch->unused_locals.try_emplace(
                std::pair(ast.source_id(), span),
                DiagnosticBuilder(
                    parameter ? DiagnosticCode::LintUnusedParameter
                              : DiagnosticCode::LintUnusedLocal,
                    parameter ? "unused function parameter" : "unused local binding"
                )
                    .primary(locate(ast.source_id(), span))
                    .build()
            );
        }
    }
}

auto BodyElaborator::visible_locals() const noexcept -> BodyLocalNames {
    auto names = BodyLocalNames();
    for (const auto& frame : frames | std::views::reverse) {
        for (const auto& [name, storage] : frame.names) {
            names.try_emplace(name, storage);
        }
    }
    for (const auto& [name, storage] : inherited_locals) {
        names.try_emplace(name, storage);
    }
    return names;
}

auto BodyElaborator::bind_local(
    Span name_span,
    BodyLocalStorage storage,
    DiagnosticCode duplicate_code
) noexcept -> AnalysisResult<void> {
    auto name = spelling(name_span);
    if (reachable && reference_path_reachable) {
        storage.unused_candidate = name_span;
    }
    if (!frames.back().names.emplace(name, storage).second) {
        return std::unexpected(fail(
            name_span,
            duplicate_code,
            std::format("local name '{}' is already defined in this scope", name)
        ));
    }
    return {};
}

auto BodyElaborator::find_local(std::string_view name) const noexcept -> const BodyLocalStorage* {
    for (auto iterator = frames.rbegin(); iterator != frames.rend(); ++iterator) {
        if (const auto found = iterator->names.find(name); found != iterator->names.end()) {
            return std::addressof(found->second);
        }
    }
    const auto inherited = inherited_locals.find(name);
    return inherited == inherited_locals.end() ? nullptr : std::addressof(inherited->second);
}

auto BodyElaborator::use_local(std::string_view name) noexcept -> BodyLocalStorage* {
    const auto mark = [&](BodyLocalStorage& storage) noexcept -> BodyLocalStorage* {
        if (reachable && reference_path_reachable) {
            storage.used = true;
            if (storage.unused_candidate) {
                batch->used_locals.emplace(ast.source_id(), *storage.unused_candidate);
            }
        }
        return std::addressof(storage);
    };
    for (auto iterator = frames.rbegin(); iterator != frames.rend(); ++iterator) {
        if (const auto found = iterator->names.find(name); found != iterator->names.end()) {
            return mark(found->second);
        }
    }
    const auto inherited = inherited_locals.find(name);
    return inherited == inherited_locals.end() ? nullptr : mark(inherited->second);
}

auto BodyElaborator::local_was_used(std::string_view name) const noexcept -> bool {
    const auto* local = find_local(name);
    return local != nullptr && local->used;
}

auto BodyElaborator::find_global(std::string_view name, Span span) noexcept
    -> AnalysisTask<const CatalogSymbol*> {
    const auto candidates = catalog().lookup(source_module_id, name);
    if (candidates.empty()) {
        co_return std::unexpected(
            fail(span, DiagnosticCode::NameUnresolved, std::format("unresolved name '{}'", name))
        );
    }
    if (candidates.size() != 1uz) {
        co_return std::unexpected(fail(
            span,
            DiagnosticCode::NameAmbiguous,
            std::format("name '{}' is provided by more than one import", name)
        ));
    }
    if (candidates.front().import_binding.has_value()) {
        import_usage().record(*candidates.front().import_binding);
    }
    const auto* result = catalog().symbol(candidates.front().symbol_id);
    if (result == nullptr) {
        invariant_violation("catalog lookup returned an invalid symbol");
    }

    auto completed =
        (co_await batch->requests.ensure_declaration(result->symbol_id, source_module_id, span));
    if (!completed) {
        co_return std::unexpected(completed.error());
    }

    co_return result;
}

auto BodyElaborator::add_parameter(
    const ASTFunctionParameter& source,
    const ConstructionCallableParameter& contract
) noexcept -> AnalysisResult<void> {
    const auto* named = std::get_if<ASTNamedBindingTarget>(&source.target);
    const auto target_span = binding_target_span(source.target);
    const auto name = named == nullptr ? std::string("_") : spelling(named->name_span);
    const auto storage = body_builder.add_parameter(
        draft().intern_spelling(name),
        contract.type,
        frames.front().lifetime,
        contract.access,
        origin(target_span)
    );
    if (named == nullptr) {
        return {};
    }
    return bind_local(
        named->name_span,
        BodyLocalStorage {
            .storage = storage,
            .type = contract.type,
            .used = false,
            .takeable = contract.access == AccessMode::Take,
            .role = BodyLocalRole::Parameter,
            .unused_candidate = std::nullopt,
        },
        DiagnosticCode::NameDuplicateParameter
    );
}

auto BodyElaborator::add_capture(
    Span name_span,
    ConstructionTypeRef type,
    CaptureMode mode
) noexcept -> AnalysisResult<void> {
    const auto name = spelling(name_span);
    const auto storage = body_builder.add_capture(
        draft().intern_spelling(name),
        type,
        frames.front().lifetime,
        mode,
        origin(name_span)
    );
    return bind_local(
        name_span,
        BodyLocalStorage {
            .storage = storage,
            .type = type,
            .used = false,
            .takeable = false,
            .role = BodyLocalRole::Capture,
            .unused_candidate = std::nullopt,
        },
        DiagnosticCode::LambdaCaptureDuplicate
    );
}
