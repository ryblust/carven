module carven:semantic.analysis.body.locals.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.suggestion;
import :frontend.ast.decl;
import :semantic.analysis.body.context;
import :semantic.analysis.catalog;
import :semantic.analysis.names;
import :semantic.analysis.source.completion;
import :semantic.analysis.types;
import :semantic.analysis.types.display;
import :semantic.semir.body;
import :semantic.semir.decl;
import :semantic.semir.generic;
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
            .unavailable = {},
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

auto BodyElaborator::unavailable_local(std::string_view name) const noexcept
    -> std::optional<SourceSpan> {
    for (const auto& frame : frames | std::views::reverse) {
        if (frame.names.contains(name)) {
            return std::nullopt;
        }
        if (const auto found = frame.unavailable.find(name); found != frame.unavailable.end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

auto BodyElaborator::exclude_statement(ASTStmtID id) noexcept -> bool {
    const auto* query = draft().source_analysis();
    const auto& statement = ast.statement(id);
    if (!query || !query->excludes(locate(ast.source_id(), statement.span))) {
        return false;
    }
    excluded_statement = statement.span;
    if (const auto* binding = std::get_if<ASTVariableDecl>(&statement.value)) {
        const auto visit = [&](this const auto& self,
                               const ASTBindingTarget& target) noexcept -> void {
            if (const auto* named = std::get_if<ASTNamedBindingTarget>(&target)) {
                const auto location = locate(ast.source_id(), named->name_span);
                frames.back().unavailable.insert_or_assign(spelling(named->name_span), location);
                observe_source(named->name_span, location, std::nullopt);
            } else if (const auto* tuple =
                           std::get_if<UniqueIndirect<ASTTupleBindingTarget>>(&target)) {
                for (const auto& element : (*tuple)->elements) {
                    self(element);
                }
            }
        };
        visit(binding->target);
    }
    return true;
}

auto BodyElaborator::complete_names(Span span) const noexcept -> void {
    const auto* query = draft().source_analysis();
    if (!query || !query->requests_completion(locate(ast.source_id(), span))) {
        return;
    }
    auto candidates = std::map<std::string, SourceCompletionCandidate, std::less<>>();
    for (const auto& [name, local] : visible_locals()) {
        if (runtime_local_outside_static_scope(name) || unavailable_local(name)) {
            continue;
        }
        candidates.emplace(
            name,
            SourceCompletionCandidate {
                name,
                SourceCompletionKind::Variable,
                type_display_name(draft(), local.type, catalog())
            }
        );
    }
    // Each selected candidate is moved into the map.
    // NOLINTNEXTLINE(misc-const-correctness)
    for (auto& candidate : source_name_candidates(draft(), catalog(), source_module_id, false)) {
        if (!unavailable_local(candidate.name)) {
            candidates.try_emplace(candidate.name, std::move(candidate));
        }
    }
    for (const auto& frame : frames) {
        for (const auto& [name, location] : frame.unavailable) {
            if (unavailable_local(name)) {
                candidates.insert_or_assign(
                    name,
                    SourceCompletionCandidate {name, SourceCompletionKind::Variable, {}}
                );
            }
        }
    }
    auto result = std::vector<SourceCompletionCandidate>();
    for (auto& [name, candidate] : candidates) {
        result.push_back(std::move(candidate));
    }
    query->complete(result);
}

auto BodyElaborator::observe_binding(Span location, const BodyLocalStorage& local) noexcept
    -> void {
    if (observe_sources && local.storage.binding.owner() == active_builder().identity()) {
        const auto value = active_builder().binding_expression(local.storage.binding);
        source_occurrences.record(
            draft(),
            locate(ast.source_id(), location),
            local.definition,
            local.type,
            std::nullopt,
            active_builder().source_callable(value.expression)
        );
        return;
    }
    observe_source(location, local.definition, local.type);
}

auto BodyElaborator::bind_local(
    Span name_span,
    BodyLocalStorage storage,
    DiagnosticCode duplicate_code
) noexcept -> AnalysisResult<void> {
    auto name = spelling(name_span);
    if (observe_sources && !storage.definition) {
        storage.definition = locate(ast.source_id(), name_span);
    }
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
    observe_binding(name_span, storage);
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

auto BodyElaborator::static_stage() const noexcept -> bool {
    return static_body || !static_scope_frames.empty();
}

auto BodyElaborator::runtime_local_outside_static_scope(std::string_view name) const noexcept
    -> bool {
    if (static_scope_frames.empty()) {
        return false;
    }
    for (auto index = frames.size(); index-- > 0uz;) {
        if (const auto found = frames[index].names.find(name); found != frames[index].names.end()) {
            return index < static_scope_frames.back() && !found->second.static_source;
        }
    }
    const auto inherited = inherited_locals.find(name);
    return inherited != inherited_locals.end() && !inherited->second.static_source;
}

auto BodyElaborator::local_was_used(std::string_view name) const noexcept -> bool {
    const auto* local = find_local(name);
    return local != nullptr && local->used;
}

auto BodyElaborator::find_global(std::string_view name, Span span) noexcept
    -> AnalysisTask<CatalogEntity> {
    const auto candidates = catalog().lookup(source_module_id, name);
    if (candidates.empty()) {
        auto known = catalog().visible_names(source_module_id);
        for (const auto& frame : frames) {
            known.append_range(frame.names.keys());
        }
        known.append_range(inherited_locals.keys());
        const auto suggested = closest_spelling(name, known);
        auto diagnostic = DiagnosticBuilder(
            DiagnosticCode::NameUnresolved,
            std::format(
                "unresolved name '{}'{}",
                name,
                suggested ? std::format("; did you mean '{}'?", *suggested) : std::string()
            )
        );
        const auto location = locate(ast.source_id(), span);
        diagnostic.primary(location);
        if (suggested) {
            diagnostic.fix(std::format("Change to '{}'", *suggested), {{location, *suggested}});
        }
        co_return std::unexpected(draft().diagnostics().error(diagnostic.build()));
    }
    auto selected = select_catalog_entity(
        draft(),
        catalog(),
        import_usage(),
        source_module_id,
        name,
        span,
        observe_sources ? &source_occurrences : nullptr,
        DiagnosticCode::NameUnresolved
    );
    if (!selected) {
        co_return std::unexpected(selected.error());
    }
    if (const auto* id = std::get_if<CatalogSymbolID>(&*selected)) {
        auto completed = co_await batch->requests.ensure_declaration(*id, source_module_id, span);
        if (!completed) {
            co_return std::unexpected(completed.error());
        }
    }
    co_return *selected;
}

auto BodyElaborator::observe_enum_case(Span location, ConstructionEnumCaseRef reference) noexcept
    -> void {
    if (!observe_sources) {
        return;
    }
    const auto [origin, name] = [&] noexcept {
        if (const auto* concrete = std::get_if<EnumCaseID>(&reference)) {
            const auto declaration = draft().construction_enum_case_declaration_copy(*concrete);
            return std::pair(declaration.origin, declaration.name);
        }
        const auto generic = std::get<GenericEnumCaseRef>(reference);
        const auto application =
            std::get<GenericNominalApplication>(draft().generic_type_copy(generic.owner));
        const auto declaration = std::get<GenericEnumDefinition>(
            draft().generic_declaration_copy(application.definition)
        );
        const auto& member = declaration.cases.at(generic.index);
        return std::pair(member.origin, member.name);
    }();
    auto source = draft().source_span(origin);
    // A case declaration begins with its name; its diagnostic origin can also
    // include payload types or a numeric initializer.
    source.span = Span::from_bounds(
        source.span.start(),
        source.span.start() + static_cast<std::uint32_t>(draft().spelling(name).size())
    );
    observe_source(location, source, std::nullopt);
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
    if (std::holds_alternative<UniqueIndirect<ASTTupleBindingTarget>>(source.target)) {
        const auto owning = contract.access == AccessMode::Take;
        auto bindings = bind_tuple_target(
            source.target,
            storage,
            contract.type,
            BodyLocalRole::Parameter,
            contract.stage == ParameterStage::Static,
            owning
        );
        if (!bindings) {
            return std::unexpected(bindings.error());
        }
        if (owning) {
            append_statement(
                SemDecompose {
                    .owner = storage.binding,
                    .initializer = std::nullopt,
                    .bindings = std::move(*bindings)
                },
                origin(target_span)
            );
        }
        return {};
    }
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
            .static_source = contract.stage == ParameterStage::Static,
            .role = BodyLocalRole::Parameter,
            .unused_candidate = std::nullopt,
            .projection = {},
            .definition = std::nullopt,
        },
        DiagnosticCode::NameDuplicateParameter
    );
}

auto BodyElaborator::local_place(const BodyLocalStorage& local, Span span) noexcept
    -> PlaceExpression {
    auto result = active_builder().binding_expression(local.storage.binding);
    for (const auto index : local.projection) {
        const auto elements = tuple_elements(draft(), result.expression.type.construction());
        if (!elements || index >= elements->size()) {
            invariant_violation("local projection exceeds its tuple shape");
        }
        result = active_builder().make_place(
            result.root,
            result.access,
            (*elements)[index],
            SemTupleElement {
                .source = UniqueIndirect(std::move(result.expression)),
                .index = index
            },
            origin(span)
        );
    }
    return result;
}

auto BodyElaborator::bind_tuple_target(
    const ASTBindingTarget& target,
    BoundStorage root,
    ConstructionTypeRef type,
    BodyLocalRole role,
    bool static_source,
    bool owning
) noexcept -> AnalysisResult<std::vector<SemDecompositionBinding>> {
    auto bindings = std::vector<SemDecompositionBinding>();
    auto path = std::vector<std::uint32_t>();
    const auto visit = [&](this const auto& self,
                           const ASTBindingTarget& selected,
                           ConstructionTypeRef selected_type) noexcept -> AnalysisResult<void> {
        if (const auto* tuple = std::get_if<UniqueIndirect<ASTTupleBindingTarget>>(&selected)) {
            const auto elements = tuple_elements(draft(), selected_type);
            if (!elements || elements->size() != (*tuple)->elements.size()) {
                return std::unexpected(fail(
                    binding_target_span(selected),
                    DiagnosticCode::TypeMismatch,
                    "tuple binding target differs from its source shape"
                ));
            }
            for (const auto [index, element] : std::views::enumerate((*tuple)->elements)) {
                path.push_back(static_cast<std::uint32_t>(index));
                auto result = self(element, (*elements)[index]);
                path.pop_back();
                if (!result) {
                    return result;
                }
            }
            return {};
        }
        const auto* named = std::get_if<ASTNamedBindingTarget>(&selected);
        if (!named) {
            return {};
        }
        const auto storage = owning ? body_builder.add_owner_binding(
                                          draft().intern_spelling(spelling(named->name_span)),
                                          selected_type,
                                          frames.back().lifetime,
                                          false,
                                          origin(named->name_span)
                                      )
                                    : root;
        auto result = bind_local(
            named->name_span,
            BodyLocalStorage {
                .storage = storage,
                .type = selected_type,
                .used = false,
                .takeable = owning,
                .static_source = static_source,
                .role = role,
                .unused_candidate = std::nullopt,
                .projection = owning ? std::vector<std::uint32_t>() : path,
                .definition = std::nullopt
            },
            role == BodyLocalRole::Parameter ? DiagnosticCode::NameDuplicateParameter
                                             : DiagnosticCode::NameDuplicateLocal
        );
        if (!result) {
            return result;
        }
        if (owning) {
            bindings.push_back({storage.binding, path});
        }
        return {};
    };
    auto result = visit(target, type);
    if (!result) {
        return std::unexpected(result.error());
    }
    return bindings;
}

auto BodyElaborator::add_capture(
    Span name_span,
    ConstructionTypeRef type,
    CaptureMode mode,
    std::optional<SourceSpan> definition
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
            .static_source = false,
            .role = BodyLocalRole::Capture,
            .unused_candidate = std::nullopt,
            .projection = {},
            .definition = definition,
        },
        DiagnosticCode::LambdaCaptureDuplicate
    );
}
