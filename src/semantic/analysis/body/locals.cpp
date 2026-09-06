module carven:semantic.analysis.body.locals.impl;

import :diagnostics.code;
import :frontend.ast.decl;
import :semantic.analysis.body.context;
import :semantic.analysis.catalog;
import :semantic.semir.body;
import :semantic.semir.decl;
import :support.invariant;
import std;

namespace body_elaboration {

auto BodyElaborator::push_frame(Span span) noexcept -> void {
    const auto frame_origin = origin(span);
    const auto parent_lifetime = active_full_expression.value_or(frames.back().lifetime);
    frames.push_back(
        LocalFrame {
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
        diagnose_unused(frames.back());
    }
    frames.pop_back();
}

auto BodyElaborator::diagnose_unused(const LocalFrame& frame) noexcept -> void {
    auto candidates = std::vector<const LocalStorage*>();
    for (const auto& [name, storage] : frame.names) {
        static_cast<void>(name);
        if (!storage.used
            && storage.unused_candidate.has_value()
            && storage.role != LocalRole::Capture) {
            candidates.push_back(std::addressof(storage));
        }
    }
    std::ranges::sort(candidates, {}, [](const LocalStorage* storage) noexcept {
        return storage->unused_candidate->start();
    });
    for (const auto* storage : candidates) {
        const auto parameter = storage->role == LocalRole::Parameter;
        warn(
            *storage->unused_candidate,
            parameter ? DiagnosticCode::LintUnusedParameter : DiagnosticCode::LintUnusedLocal,
            parameter ? "unused function parameter" : "unused local binding"
        );
    }
}

auto BodyElaborator::bind_local(
    Span name_span,
    LocalStorage storage,
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

auto BodyElaborator::find_local(std::string_view name) const noexcept -> const LocalStorage* {
    for (auto iterator = frames.rbegin(); iterator != frames.rend(); ++iterator) {
        if (const auto found = iterator->names.find(name); found != iterator->names.end()) {
            return std::addressof(found->second);
        }
    }
    return nullptr;
}

auto BodyElaborator::use_local(std::string_view name) noexcept -> LocalStorage* {
    for (auto iterator = frames.rbegin(); iterator != frames.rend(); ++iterator) {
        if (const auto found = iterator->names.find(name); found != iterator->names.end()) {
            found->second.used |= reachable && reference_path_reachable;
            return std::addressof(found->second);
        }
    }
    return nullptr;
}

auto BodyElaborator::local_was_used(std::string_view name) const noexcept -> bool {
    const auto* local = find_local(name);
    return local != nullptr && local->used;
}

auto BodyElaborator::find_global(std::string_view name, Span span) noexcept
    -> AnalysisResult<const CatalogSymbol*> {
    const auto candidates = catalog().lookup(source_module_id, name);
    if (candidates.empty()) {
        return std::unexpected(
            fail(span, DiagnosticCode::NameUnresolved, std::format("unresolved name '{}'", name))
        );
    }
    if (candidates.size() != 1uz) {
        return std::unexpected(fail(
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
    return result;
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
        LocalStorage {
            .storage = storage,
            .type = contract.type,
            .takeable = contract.access == AccessMode::Take,
            .role = LocalRole::Parameter,
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
        LocalStorage {
            .storage = storage,
            .type = type,
            .takeable = false,
            .role = LocalRole::Capture,
            .unused_candidate = std::nullopt,
        },
        DiagnosticCode::LambdaCaptureDuplicate
    );
}

} // namespace body_elaboration
