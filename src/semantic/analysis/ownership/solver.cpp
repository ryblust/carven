module carven:semantic.analysis.ownership.solver.impl;

import :semantic.analysis.ownership.context;
import :semantic.semir.callable;
import :semantic.semir.program;
import std;

OwnershipBatchAnalyzer::OwnershipBatchAnalyzer(
    const AnalysisModelView& model,
    AnalysisDiagnostics diagnostics,
    ReadPassingFacts* passing
) noexcept
    : program(model.program),
      model(model),
      diagnostics(diagnostics),
      passing(passing) {
    auto preparation = prepare_ownership_analysis(model);
    body_facts = std::move(preparation.body_facts);
    recursion_components = std::move(preparation.recursion_components);
}

auto OwnershipBatchAnalyzer::facts_for_body(AnalysisBodyID id) const noexcept
    -> const OwnershipBodyFacts& {
    return body_facts.at(id);
}

auto OwnershipBatchAnalyzer::contents(ConstructionTypeRef type) const noexcept -> TypeContents {
    return model.contents(type);
}

auto OwnershipBatchAnalyzer::same_recursion_component(
    AnalysisBodyID source,
    AnalysisBodyID target
) const noexcept -> bool {
    return recursion_components.at(source) == recursion_components.at(target);
}

auto OwnershipBatchAnalyzer::region_site(
    AnalysisBodyID body,
    ConstructionTypeRef type,
    OwnershipRegionDescriptor descriptor
) noexcept -> OwnershipStorageSite {
    const auto [found, inserted] = region_sites.try_emplace(
        std::tuple(body, type, std::move(descriptor)),
        region_sites.size()
    );
    static_cast<void>(inserted);
    return {
        .body = body,
        .slot = found->second,
        .kind = OwnershipStorageSiteKind::Region,
        .element_selection = std::nullopt
    };
}

auto OwnershipBatchAnalyzer::body(AnalysisBodyID id) const noexcept -> const AnalysisBodyView& {
    return model.body(id);
}

auto OwnershipBatchAnalyzer::diagnose(
    DiagnosticCode code,
    std::string message,
    ProgramOriginID origin,
    std::optional<ProgramOriginID> related,
    std::string related_label,
    std::string help
) noexcept -> void {
    if (failure.has_value()) {
        return;
    }
    auto diagnostic = DiagnosticBuilder(code, std::move(message));
    diagnostic.primary(program.provenance().source_span(origin));
    if (related.has_value()) {
        diagnostic.related(program.provenance().source_span(*related), std::move(related_label));
    }
    if (!help.empty()) {
        diagnostic.help(std::move(help));
    }
    failure = diagnostics.error(diagnostic.build());
}

auto OwnershipBatchAnalyzer::commit_diagnosis(
    std::unique_ptr<OwnershipDiagnosisRecord> diagnosis,
    const OwnershipEscape* escape
) noexcept -> void {
    if (diagnosis) {
        // A return copy is reported only when every diagnosed context admits Take.
        for (const auto [origin, transferable] : diagnosis->returned_copies) {
            const auto [found, inserted] = returned_copies.emplace(origin, transferable);
            if (!inserted) {
                found->second = found->second && transferable;
            }
        }
        if (diagnosis->error.has_value()) {
            auto& error = *diagnosis->error;
            diagnose(
                error.code,
                std::move(error.message),
                error.origin,
                error.related,
                std::move(error.related_label),
                std::move(error.help)
            );
        }
    }
    if (escape != nullptr && !failure.has_value()) {
        diagnose(
            DiagnosticCode::AccessBorrowConflict,
            escape->message,
            escape->origin,
            escape->related,
            "related storage or access",
            {}
        );
    }
}

auto OwnershipBatchAnalyzer::enqueue(std::size_t index) noexcept -> void {
    auto& query = *queries[index];
    query.evaluation_is_closed = false;
    if (!query.queued) {
        query.queued = true;
        pending_queries.emplace(recursion_components.at(query.input.body_id), index);
    }
}

auto OwnershipBatchAnalyzer::same_input_partition(
    const OwnershipCallInput& left,
    const OwnershipCallInput& right
) noexcept -> bool {
    if (left.body_id != right.body_id
        || left.parameters != right.parameters
        || left.captures != right.captures
        || left.objects.size() != right.objects.size()
        || left.state != right.state
        || left.outlives != right.outlives
        || left.accesses != right.accesses
        || left.storage_readers != right.storage_readers) {
        return false;
    }
    for (const auto [a, b] : std::views::zip(left.objects, right.objects)) {
        if (a.type != b.type || a.site != b.site) {
            return false;
        }
    }
    return true;
}

auto OwnershipBatchAnalyzer::includes_input_topology(
    const OwnershipCallInput& destination,
    const OwnershipCallInput& source
) noexcept -> bool {
    if (!std::ranges::includes(destination.owns, source.owns)
        || !std::ranges::includes(destination.possible_aliases, source.possible_aliases)) {
        return false;
    }
    for (const auto [previous, incoming] : std::views::zip(destination.objects, source.objects)) {
        if ((incoming.many && !previous.many) || (incoming.feedback && !previous.feedback)) {
            return false;
        }
    }
    return true;
}

auto OwnershipBatchAnalyzer::query(OwnershipCallInput input, bool recursive) noexcept
    -> const OwnershipCallQuery& {
    normalize_storage_loans(input.storage_readers);
    for (auto&& [binding, parameter] :
         std::views::zip(body(input.body_id).inputs().parameters, input.parameters)) {
        const auto& source = body(input.body_id).binding(binding);
        const auto* mode = std::get_if<ParameterBindingStorage>(&source.storage);
        if (mode
            && mode->access == AccessMode::Read
            && contents(source.type).read_borrows_storage()) {
            if (parameter.alias) {
                parameter.storage.push_back(*parameter.alias);
                parameter.alias.reset();
            }
        }
    }
    for (auto& parameter : input.parameters) {
        normalize_relationships(parameter.value);
        std::ranges::sort(parameter.storage);
        parameter.storage.erase(
            std::ranges::unique(parameter.storage).begin(),
            parameter.storage.end()
        );
    }
    for (auto& capture : input.captures) {
        normalize_relationships(capture.value);
        std::ranges::sort(capture.storage);
        capture.storage.erase(std::ranges::unique(capture.storage).begin(), capture.storage.end());
    }
    // Only a body in the callee's recursion component can allocate at a local
    // site again; other sites need only their equality within this input.
    auto canonical_sites = std::flat_map<OwnershipStorageSite, OwnershipStorageSite>();
    for (auto& state : input.state.objects) {
        normalize_relationships(state.relationships);
    }
    for (auto& object : input.objects) {
        // Sites within the SCC can recur; external histories need only their
        // equality within this input, including regions from a prior interface.
        if (same_recursion_component(object.site.body, input.body_id)) {
            continue;
        }
        const auto canonical = OwnershipStorageSite {
            .body = input.body_id,
            .slot = canonical_sites.size(),
            .kind = OwnershipStorageSiteKind::Input,
            .element_selection = std::nullopt
        };
        object.site = canonical_sites.emplace(object.site, canonical).first->second;
    }
    auto& candidates = body_queries[input.body_id];
    const auto found = std::ranges::find_if(candidates, [&](std::size_t index) noexcept {
        const auto& query = *queries[index];
        return query.recursive == recursive
            && (recursive ? same_input_partition(query.input, input) : query.input == input);
    });
    auto index = queries.size();
    if (found != candidates.end()) {
        index = *found;
        auto& query = *queries[index];
        const auto& candidate = query.pending_input ? *query.pending_input : query.input;
        if (recursive && !includes_input_topology(candidate, input)) {
            if (!query.pending_input) {
                query.pending_input = query.input;
            }
            auto& joined = *query.pending_input;
            joined.possible_aliases.append_range(input.possible_aliases);
            std::ranges::sort(joined.possible_aliases);
            joined.possible_aliases.erase(
                std::ranges::unique(joined.possible_aliases).begin(),
                joined.possible_aliases.end()
            );
            joined.owns.append_range(input.owns);
            std::ranges::sort(joined.owns);
            joined.owns.erase(std::ranges::unique(joined.owns).begin(), joined.owns.end());
            for (auto&& [previous, incoming] : std::views::zip(joined.objects, input.objects)) {
                previous.many |= incoming.many;
                previous.feedback |= incoming.feedback;
            }
            ++query.input_revision;
            enqueue(index);
            for (const auto consumer : query.consumers) {
                enqueue(consumer);
            }
        }
    } else {
        candidates.push_back(index);
        queries.push_back(
            std::make_unique<OwnershipCallQuery>(OwnershipCallQuery {
                .input = std::move(input),
                .recursive = recursive,
                .pending_input = std::nullopt,
                .input_revision = 0uz,
                .answer =
                    OwnershipCallSummary {
                        .completions = {},
                        .referents = {},
                        .owns = {},
                        .effects = {}
                    },
                .consumers = {},
                .queued = false,
                .evaluation_is_closed = false,
                .diagnosis = nullptr,
                .topology = OwnershipStorageTopology {
                    .revision = 0uz,
                    .propagated_revision = std::nullopt,
                    .objects = {},
                    .owns = {},
                    .possible_aliases = {},
                    .roots = {},
                    .anchors = {},
                    .feedback = {},
                    .region_revision = std::nullopt,
                    .regions = nullptr
                }
            })
        );
        enqueue(index);
    }
    auto& query = *queries[index];
    if (active_query.has_value()) {
        query.consumers.insert(*active_query);
    }
    return query;
}

auto OwnershipBatchAnalyzer::symbolic_input(const AnalysisBodyView& source) const noexcept
    -> OwnershipCallInput {
    auto result = OwnershipCallInput {
        .body_id = source.id(),
        .parameters = {},
        .captures = {},
        .objects = {},
        .state = OwnershipState {.objects = {}},
        .outlives = {},
        .accesses = {},
        .storage_readers = {},
        .owns = {},
        .possible_aliases = {}
    };
    auto input_role = 0uz;
    const auto abstract_value =
        [&](this const auto& self,
            ConstructionTypeRef type,
            ProgramOriginID origin,
            OwnershipProjectionPath source_path = {}) noexcept -> OwnershipRelationships {
        auto relationships = OwnershipRelationships {};
        const auto facts = contents(type);
        if (!facts.contains_closure_owner
            && !facts.contains_callable_view
            && !facts.contains_parametric_value) {
            return relationships;
        }
        const auto& value = model.shape(type);
        if (std::holds_alternative<GenericTypeParameter>(value)) {
            relationships.edit().formal_bundles.push_back(
                OwnershipFormalBundle {
                    .holder = {},
                    .body = source.id(),
                    .input = input_role,
                    .source = std::move(source_path),
                    .origin = origin
                }
            );
            return relationships;
        }
        const auto element_value = [&](ConstructionTypeRef element,
                                       std::optional<std::uint64_t> index) noexcept {
            auto path = source_path;
            path.push_back(index);
            return self(element, origin, std::move(path));
        };
        if (std::holds_alternative<GenericNominalApplication>(value)) {
            const auto generic = std::get<GenericTypeID>(type);
            if (const auto record = model.generics.records.find(generic);
                record != model.generics.records.end()) {
                for (const auto [index, field] : std::views::enumerate(record->second)) {
                    merge_relationships(
                        relationships,
                        nest_relationships(element_value(field, index), {index})
                    );
                }
            } else {
                for (const auto& payload : model.generics.enum_payloads.at(generic)) {
                    for (const auto [index, field] : std::views::enumerate(payload)) {
                        merge_relationships(
                            relationships,
                            nest_relationships(element_value(field, index), {index})
                        );
                    }
                }
            }
            return relationships;
        }

        if (std::holds_alternative<ConstructionCallableViewTypeValue>(value)) {
            relationships.edit().callable_loans.push_back(
                OwnershipCallableLoan {
                    .holder = {},
                    .backing = std::nullopt,
                    .callable = std::nullopt,
                    .origin = origin,
                    .direct_only = false
                }
            );
        } else if (std::holds_alternative<ClosureTypeValue>(value)
                   || std::holds_alternative<GenericClosureType>(value)) {
            const auto& target = body(*model.callable_body(*model.callable_identity(type)));
            for (const auto [index, id] : std::views::enumerate(target.inputs().captures)) {
                const auto& capture = target.binding(id);
                auto captured = self(capture.type, capture.origin);
                if (std::get<CaptureBindingStorage>(capture.storage).mode == CaptureMode::Write) {
                    const auto object = result.objects.size();
                    result.objects.push_back(
                        OwnershipStorageObject {
                            .type = capture.type,
                            .origin = capture.origin,
                            .site =
                                OwnershipStorageSite {
                                    .body = source.id(),
                                    .slot = object,
                                    .kind = OwnershipStorageSiteKind::Input,
                                    .element_selection = std::nullopt
                                },
                            .many = false,
                            .feedback = false
                        }
                    );
                    result.state.objects.push_back(
                        OwnershipObjectState {
                            .available = true,
                            .taken = std::nullopt,
                            .relationships = std::move(captured),
                            .modified = false,
                            .child_intent = std::nullopt
                        }
                    );
                    relationships.edit().captures.push_back(
                        OwnershipCapture {
                            .holder = OwnershipProjectionPath {index},
                            .target = OwnershipPlace {.object = object, .path = {}},
                            .origin = capture.origin
                        }
                    );
                } else {
                    merge_relationships(
                        relationships,
                        nest_relationships(std::move(captured), OwnershipProjectionPath {index})
                    );
                }
            }
        } else if (const auto* tuple = std::get_if<ConstructionTupleTypeValue>(&value)) {
            for (const auto [index, element] : std::views::enumerate(tuple->elements)) {
                merge_relationships(
                    relationships,
                    nest_relationships(
                        element_value(element, index),
                        OwnershipProjectionPath {index}
                    )
                );
            }
        } else if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&value);
                   array != nullptr
                   && (contents(type).contains_callable_view
                       || contents(type).contains_closure_owner
                       || contents(type).contains_parametric_value)) {
            relationships = nest_relationships(
                element_value(array->element, std::nullopt),
                OwnershipProjectionPath {std::nullopt}
            );
        }

        if (const auto* slice = std::get_if<ConstructionSliceTypeValue>(&value);
            slice != nullptr && contents(type).contains_callable_view) {
            auto elements = nest_relationships(self(slice->element, origin), {std::nullopt});
            const auto backing = result.objects.size();
            result.objects.push_back(
                OwnershipStorageObject {
                    .type = type,
                    .origin = origin,
                    .site =
                        OwnershipStorageSite {
                            .body = source.id(),
                            .slot = backing,
                            .kind = OwnershipStorageSiteKind::Input,
                            .element_selection = std::nullopt
                        },
                    .many = false,
                    .feedback = false
                }
            );
            result.state.objects.push_back(
                OwnershipObjectState {
                    .available = true,
                    .taken = std::nullopt,
                    .relationships = std::move(elements),
                    .modified = false,
                    .child_intent = std::nullopt
                }
            );
            relationships.edit().storage_loans.push_back(
                OwnershipStorageLoan {
                    .holder = {},
                    .backing = OwnershipPlace {.object = backing, .path = {}},
                    .origin = origin,
                    .protection = OwnershipLoanProtection::Contents,
                    .sequence_type = std::nullopt
                }
            );
        }
        if (const auto* structure = std::get_if<StructTypeValue>(&value)) {
            for (const auto& [index, field] : std::views::enumerate(
                     program.declarations().structure(structure->structure).fields
                 )) {
                merge_relationships(
                    relationships,
                    nest_relationships(element_value(field.type, index), {index})
                );
            }
        } else if (const auto* enumeration = std::get_if<EnumTypeValue>(&value)) {
            for (const auto id :
                 program.declarations().enumeration(enumeration->enumeration).cases) {
                for (const auto& [index, element] :
                     std::views::enumerate(program.declarations().enum_case(id).payload_types)) {
                    merge_relationships(
                        relationships,
                        nest_relationships(element_value(element, index), {index})
                    );
                }
            }
        }
        return relationships;
    };
    const auto inputs = [&](std::span<const LocalBindingID> ids,
                            std::vector<OwnershipCallArgument>& destination) noexcept {
        for (const auto id : ids) {
            const auto& binding = source.binding(id);
            input_role = destination.size();
            auto relationships = abstract_value(binding.type, binding.origin);
            const auto borrowed = binding.storage.visit(
                Overloaded {
                    [&](const ParameterBindingStorage& value) noexcept {
                        return value.access == AccessMode::Write
                            || (value.access == AccessMode::Read
                                && !contents(binding.type).read_is_value_snapshot());
                    },
                    [](const CaptureBindingStorage&) static noexcept {
                        // A closure invocation borrows its stored captures; it
                        // does not create new owners for value-captured fields.
                        return true;
                    },
                    [](const OwnerBindingStorage&) static noexcept { return false; },
                    [](const AsyncChildBindingStorage&) static noexcept { return false; },
                    [](const AccessBindingStorage&) static noexcept { return false; },
                }
            );
            auto alias = std::optional<OwnershipPlace>();
            if (borrowed) {
                alias = OwnershipPlace {.object = result.objects.size(), .path = {}};
                result.objects.push_back(
                    OwnershipStorageObject {
                        .type = binding.type,
                        .origin = binding.origin,
                        .site =
                            OwnershipStorageSite {
                                .body = source.id(),
                                .slot = result.objects.size(),
                                .kind = OwnershipStorageSiteKind::Input,
                                .element_selection = std::nullopt
                            },
                        .many = false,
                        .feedback = false
                    }
                );
                result.state.objects.push_back(
                    OwnershipObjectState {
                        .available = true,
                        .taken = std::nullopt,
                        .relationships = relationships,
                        .modified = false,
                        .child_intent = std::nullopt
                    }
                );
            }
            destination.push_back(
                OwnershipCallArgument {
                    .alias = std::move(alias),
                    .value = std::move(relationships),
                    .storage = {},
                    .capture_holder = std::nullopt
                }
            );
        }
    };
    inputs(source.inputs().parameters, result.parameters);
    inputs(source.inputs().captures, result.captures);
    result.outlives.assign(result.objects.size(), std::vector<bool>(result.objects.size(), true));
    return result;
}

auto OwnershipBatchAnalyzer::join_summary(
    OwnershipCallSummary& destination,
    const OwnershipCallSummary& source,
    std::size_t input_objects
) noexcept -> void {
    // A query's domain keeps node identities and only gains graph facts. The
    // current topology snapshot lifts retained completions into that same domain.
    if (source.referents.size() < destination.referents.size()
        || !std::ranges::includes(source.owns, destination.owns)) {
        invariant_violation("ownership call domain withdrew retained storage");
    }
    for (const auto [previous, current] :
         std::views::zip(destination.referents, source.referents)) {
        if (previous.type != current.type
            || previous.site != current.site
            || (previous.many && !current.many)
            || (previous.feedback && !current.feedback)) {
            invariant_violation("ownership call domain changed a retained storage identity");
        }
    }
    for (const auto& completion : source.completions) {
        const auto found =
            std::ranges::find_if(destination.completions, [&](const auto& previous) noexcept {
                return previous.stopped == completion.stopped
                    && previous.failure == completion.failure;
            });
        if (found == destination.completions.end()) {
            destination.completions.push_back(completion);
        } else {
            join_ownership_state(found->state, completion.state);
            merge_relationships(found->value, completion.value);
        }
    }
    destination.referents = source.referents;
    destination.owns = source.owns;
    for (const auto& effect : source.effects) {
        if (!std::ranges::contains(destination.effects, effect)) {
            destination.effects.push_back(effect);
        }
    }
    std::ranges::sort(destination.effects, {}, [](const auto& effect) static noexcept {
        return std::tuple(
            effect.place,
            effect.invalidates,
            effect.storage,
            effect.take,
            effect.preserves_backing
        );
    });
    std::ranges::sort(destination.completions, {}, [](const auto& completion) static noexcept {
        return std::pair(completion.stopped, completion.failure);
    });
    const auto domain = input_objects + destination.referents.size();
    for (auto& completion : destination.completions) {
        completion.state.objects.resize(
            domain,
            {.available = true,
             .taken = std::nullopt,
             .relationships = {},
             .modified = false,
             .child_intent = std::nullopt}
        );
    }
}

auto OwnershipBatchAnalyzer::run() noexcept -> AnalysisResult<OwnershipAnalysisSummary> {
    auto evaluation_count = 0uz;
    auto definition_queries = std::map<AnalysisBodyID, const OwnershipCallQuery*>();
    // Source bodies are checked; an instance repeats one with fewer paths.
    for (const auto& [id, source] : model.bodies()) {
        if (source.specialized()) {
            continue;
        }
        auto input = symbolic_input(source);
        if (auto error = check_ownership_contracts(model, source)) {
            diagnose(
                error->code,
                std::move(error->message),
                error->origin,
                error->related,
                std::move(error->related_label),
                std::move(error->help)
            );
        }
        const auto& definition = query(std::move(input), false);
        definition_queries.emplace(id, std::addressof(definition));
    }
    if (failure.has_value()) {
        return std::unexpected(*failure);
    }
    // Components are numbered dependency-first. Finish pending callees before
    // their readers; equivalent component keys retain FIFO order for recursion.
    // Dependencies discovered during evaluation remain conservative when a call
    // changes its input context.
    while (!pending_queries.empty()) {
        const auto index = pending_queries.begin()->second;
        pending_queries.erase(pending_queries.begin());
        auto& query = *queries[index];
        query.queued = false;
        if (query.pending_input) {
            query.input = std::move(*query.pending_input);
            query.pending_input.reset();
        }
        active_query = index;
        ++evaluation_count;
        const auto input_revision = query.input_revision;
        auto analyzer = OwnershipBodyAnalyzer(*this, query.input, query.topology);
        const auto revision = query.topology.revision;
        auto result = analyzer.run();
        active_query.reset();
        if (!result.answer.has_value()) {
            // An invalid completion keeps priority over every deferred query.
            commit_diagnosis(std::move(result.diagnosis), std::addressof(result.answer.error()));
            return std::unexpected(*failure);
        }
        query.diagnosis = std::move(result.diagnosis);
        auto joined = query.answer;
        join_summary(joined, *result.answer, query.input.objects.size());
        const auto changed = joined != query.answer;
        // The joined candidate covers this transfer. Its certificate holds
        // while the input, topology, and answers read by the transfer stay fixed.
        // Publishing a changed answer invalidates its readers, including this
        // query when it read its own answer.
        query.evaluation_is_closed = !query.queued
            && revision == query.topology.revision
            && input_revision == query.input_revision;
        if (changed) {
            query.answer = std::move(joined);
            for (const auto consumer : query.consumers) {
                enqueue(consumer);
            }
        }
        if (!query.evaluation_is_closed) {
            // Input, topology, or a consumed answer changed during evaluation.
            enqueue(index);
        }
    }
    for (const auto& query : queries) {
        if (!query->evaluation_is_closed) {
            invariant_violation("final ownership evaluation did not validate a closed call answer");
        }
    }
    // Every changed answer schedules its readers. With no pending query,
    // each retained diagnosis observes the final answers it consumed.
    for (const auto& query : queries) {
        commit_diagnosis(std::move(query->diagnosis));
        if (failure.has_value()) {
            return std::unexpected(*failure);
        }
    }
    for (const auto [origin, transferable] : returned_copies) {
        if (transferable) {
            diagnostics.warning(
                DiagnosticBuilder(
                    DiagnosticCode::LintReturnCopy,
                    "returned owner is copied; transfer it with '&&' to avoid the copy"
                )
                    .primary(program.provenance().source_span(origin))
                    .build()
            );
        }
    }
    auto nodes = 0uz;
    auto edges = 0uz;
    for (const auto& query : queries) {
        nodes += query->topology.objects.size();
        edges += query->topology.owns.size();
    }
    {
        auto derived = ReadPassingFacts {};
        for (const auto [callable, declaration] : program.declarations().callables()) {
            const auto& signature = program.callable_signatures().signature(declaration.signature);
            auto stable = std::vector<bool>(signature.parameters.size(), false);
            for (const auto [index, parameter] : std::views::enumerate(signature.parameters)) {
                stable[index] = parameter.access == AccessMode::Read
                    && signature.execution == CallableExecutionKind::Async
                    && contents(parameter.type).contains_native_value;
            }
            if (const auto body_id = program.declarations().body_for_callable(callable)) {
                const auto source_id =
                    AnalysisBodyID {body(*body_id).specialized().value_or(*body_id)};
                if (const auto found = definition_queries.find(source_id);
                    found != definition_queries.end()) {
                    const auto& query = *found->second;
                    const auto source_parameters = body(source_id).inputs().parameters;
                    const auto instance_parameters = body(*body_id).inputs().parameters;
                    const auto retain = [&](const OwnershipRelationships& relationships) noexcept {
                        const auto backing = [&](const OwnershipPlace& place) noexcept {
                            for (auto index = 0uz; index < instance_parameters.size(); ++index) {
                                const auto binding = instance_parameters[index];
                                const auto source_parameter =
                                    std::ranges::find(source_parameters, binding);
                                if (source_parameter == source_parameters.end()) {
                                    invariant_violation(
                                        "specialized parameter lost its source binding"
                                    );
                                }
                                const auto& argument =
                                    query.input.parameters[static_cast<std::size_t>(
                                        source_parameter - source_parameters.begin()
                                    )];
                                if (index >= stable.size()
                                    || signature.parameters[index].access != AccessMode::Read) {
                                    continue;
                                }
                                const auto retained = [&](const OwnershipPlace& formal) noexcept {
                                    return (place.object == formal.object
                                            && overlaps(place.path, formal.path))
                                        || (place.object < query.topology.roots.size()
                                            && place.object != formal.object
                                            && std::ranges::contains(
                                                query.topology.roots[place.object],
                                                formal.object
                                            ));
                                };
                                stable[index] = stable[index]
                                    || (argument.alias && retained(*argument.alias))
                                    || std::ranges::any_of(argument.storage, retained);
                            }
                        };
                        for (const auto& row : relationships.view().storage_loans) {
                            backing(row.backing);
                        }
                        for (const auto& row : relationships.view().captures) {
                            backing(row.target);
                        }
                        for (const auto& row : relationships.view().callable_loans) {
                            if (row.backing) {
                                backing(*row.backing);
                            }
                        }
                    };
                    for (const auto& completion : query.answer.completions) {
                        retain(completion.value);
                        for (auto index = 0uz; index < query.input.objects.size(); ++index) {
                            if (completion.state.objects[index].modified) {
                                retain(completion.state.objects[index].relationships);
                            }
                        }
                    }
                }
            }
            derived.stable_parameters.emplace(callable, std::move(stable));
        }
        for (const auto& [body_id, source] : model.bodies()) {
            static_cast<void>(body_id);
            if (!source.concrete()) {
                continue;
            }
            visit_semantic_nodes(source.region(), [&](const auto& node) noexcept {
                if constexpr (std::same_as<
                                  std::remove_cvref_t<decltype(node)>,
                                  SemanticExpression>) {
                    const auto* adoption = std::get_if<SemBorrowCallable>(&node.value);
                    const auto* array = std::get_if<SemArrayAdopt>(&node.value);
                    if (adoption == nullptr && array == nullptr) {
                        return;
                    }
                    const auto from = adoption ? adoption->source->type.construction()
                                               : array->source->type.construction();
                    auto to = node.type.construction();
                    while (const auto* element =
                               std::get_if<ConstructionArrayTypeValue>(&model.shape(to))) {
                        to = element->element;
                    }
                    if (!std::holds_alternative<ConstructionCallableViewTypeValue>(
                            model.shape(to)
                        )) {
                        return;
                    }
                    const auto target =
                        callable_adaptation(program, std::get<TypeID>(from), node.type.resolved())
                            .callable;
                    if (!target) {
                        return;
                    }
                    const auto callable = std::get<CallableID>(*target);
                    const auto& required = derived.stable_parameters.at(callable);
                    const auto& signature = program.callable_signatures().signature(
                        program.declarations().callable(callable).signature
                    );
                    for (const auto [index, parameter] :
                         std::views::enumerate(signature.parameters)) {
                        const auto facts = contents(parameter.type);
                        if (required[index]
                            && facts.contains_native_value
                            && !facts.contains_storage_owner
                            && !facts.contains_closure_owner
                            && !facts.contains_operation_owner) {
                            diagnose(
                                DiagnosticCode::TypeCallableViewEscape,
                                "callable view cannot discard a retained Read storage requirement",
                                node.origin,
                                std::nullopt,
                                {},
                                "call the function directly to preserve its borrowed input storage"
                            );
                        }
                    }
                }
            });
        }
        if (failure) {
            return std::unexpected(*failure);
        }
        if (passing != nullptr) {
            *passing = std::move(derived);
        }
    }
    return OwnershipAnalysisSummary {
        .query_count = queries.size(),
        .evaluation_count = evaluation_count,
        .storage_node_count = nodes,
        .storage_edge_count = edges
    };
}

auto analyze_body_batch(
    const AnalysisModelView& model,
    AnalysisDiagnostics diagnostics,
    ReadPassingFacts* passing
) noexcept -> AnalysisResult<void> {
    return OwnershipBatchAnalyzer(model, diagnostics, passing)
        .run()
        .transform([](const OwnershipAnalysisSummary&) static noexcept {});
}
