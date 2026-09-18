module carven:semantic.analysis.ownership.solver.impl;

import :semantic.analysis.ownership.context;
import :semantic.semir.program;
import std;

OwnershipBatchAnalyzer::OwnershipBatchAnalyzer(
    const SemIRProgram& program,
    AnalysisDiagnostics diagnostics
) noexcept
    : program(program),
      diagnostics(diagnostics),
      bodies(program.bodies()) {
    for (const auto [id, body] : bodies.entries()) {
        body_facts.emplace(id, prepare_ownership_body_facts(body, program));
    }
}

auto OwnershipBatchAnalyzer::facts_for_body(BodyID id) const noexcept -> const OwnershipBodyFacts& {
    return body_facts.at(id);
}

auto OwnershipBatchAnalyzer::contents(TypeID type) const noexcept -> TypeContents {
    return program.type_contents(type);
}

auto OwnershipBatchAnalyzer::body(BodyID id) const noexcept -> const SemIRBody& {
    return bodies.body(id);
}

auto OwnershipBatchAnalyzer::diagnose(
    DiagnosticCode code,
    std::string message,
    ProgramOriginID origin,
    std::optional<ProgramOriginID> related
) noexcept -> void {
    if (failure.has_value()) {
        return;
    }
    auto diagnostic = DiagnosticBuilder(code, std::move(message));
    diagnostic.primary(program.provenance().source_span(origin));
    if (related.has_value()) {
        diagnostic.related(program.provenance().source_span(*related), "related storage or access");
    }
    failure = diagnostics.error(diagnostic.build());
}

auto OwnershipBatchAnalyzer::enqueue(std::size_t index) noexcept -> void {
    auto& query = *queries[index];
    if (!query.queued) {
        query.queued = true;
        pending_queries.push_back(index);
    }
}

auto OwnershipBatchAnalyzer::query(OwnershipCallInput input) noexcept
    -> std::span<const OwnershipCallCompletion> {
    normalize_storage_loans(input.storage_readers);
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
    for (auto& object : input.objects) {
        normalize_relationships(object.state.relationships);
    }
    auto& candidates = body_queries[input.body_id];
    const auto found = std::ranges::find_if(candidates, [&](std::size_t index) noexcept {
        return queries[index]->input == input;
    });
    auto index = queries.size();
    if (found != candidates.end()) {
        index = *found;
    } else {
        if (queries_sealed) {
            invariant_violation("ownership diagnosis discovered an unsolved call input");
        }
        candidates.push_back(index);
        queries.push_back(
            std::make_unique<OwnershipCallQuery>(
                OwnershipCallQuery {std::move(input), {}, {}, false}
            )
        );
        enqueue(index);
    }
    auto& query = *queries[index];
    if (active_query.has_value()) {
        query.consumers.insert(*active_query);
    }
    return query.answer;
}

auto OwnershipBatchAnalyzer::root_input(const SemIRBody& source) const noexcept
    -> OwnershipCallInput {
    auto result = OwnershipCallInput {source.id(), {}, {}, {}, {}, {}, {}};
    const auto abstract_value = [&](this const auto& self,
                                    TypeID type,
                                    ProgramOriginID origin) noexcept -> OwnershipRelationships {
        auto relationships = OwnershipRelationships {};
        const auto facts = contents(type);
        if (!facts.closure_owner && !facts.callable_view) {
            return relationships;
        }
        const auto value = program.types().type(type).value;
        if (std::holds_alternative<CallableViewTypeValue>(value)) {
            relationships.callable_loans.push_back({{}, std::nullopt, std::nullopt, origin, false});
        } else if (const auto* closure = std::get_if<ClosureTypeValue>(&value)) {
            const auto& target = body(*program.declarations().body_for_callable(closure->callable));
            for (const auto [index, id] : std::views::enumerate(target.inputs().captures)) {
                const auto& capture = target.binding(id);
                auto captured = self(capture.type, capture.origin);
                if (std::get<CaptureBindingStorage>(capture.storage).mode == CaptureMode::Write) {
                    const auto object = result.objects.size();
                    result.objects.push_back(
                        {capture.type,
                         capture.origin,
                         {.available = true,
                          .taken = std::nullopt,
                          .relationships = std::move(captured),
                          .modified = false},
                         {.body = source.id(), .slot = object, .input = true},
                         false}
                    );
                    relationships.captures.push_back(
                        {OwnershipProjectionPath {index},
                         OwnershipPlace {object, {}},
                         capture.origin}
                    );
                } else {
                    merge_relationships(
                        relationships,
                        nest_relationships(std::move(captured), OwnershipProjectionPath {index})
                    );
                }
            }
        } else if (const auto* array = std::get_if<ArrayTypeValue>(&value); array != nullptr
                   && (contents(type).callable_view || contents(type).closure_owner)) {
            relationships = nest_relationships(
                self(array->element, origin),
                OwnershipProjectionPath {std::nullopt}
            );
        }

        if (const auto* slice = std::get_if<SliceTypeValue>(&value);
            slice != nullptr && contents(type).callable_view) {
            auto elements = nest_relationships(self(slice->element, origin), {std::nullopt});
            const auto backing = result.objects.size();
            result.objects.push_back(
                {type,
                 origin,
                 {.available = true,
                  .taken = std::nullopt,
                  .relationships = std::move(elements),
                  .modified = false},
                 {.body = source.id(), .slot = backing, .input = true},
                 false}
            );
            relationships.storage_loans.push_back({{}, {backing, {}}, origin});
        }
        if (const auto* structure = std::get_if<StructTypeValue>(&value)) {
            for (const auto& [index, field] : std::views::enumerate(
                     program.declarations().structure(structure->structure).fields
                 )) {
                merge_relationships(
                    relationships,
                    nest_relationships(self(field.type, origin), {index})
                );
            }
        } else if (const auto* enumeration = std::get_if<EnumTypeValue>(&value)) {
            for (const auto id :
                 program.declarations().enumeration(enumeration->enumeration).cases) {
                for (const auto& [index, element] :
                     std::views::enumerate(program.declarations().enum_case(id).payload_types)) {
                    merge_relationships(
                        relationships,
                        nest_relationships(self(element, origin), {index})
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
            auto relationships = abstract_value(binding.type, binding.origin);
            const auto borrowed = binding.storage.visit(
                Overloaded {
                    [](const ParameterBindingStorage& value) static noexcept {
                        return value.access != AccessMode::Take;
                    },
                    [](const CaptureBindingStorage&) static noexcept {
                        // A closure invocation borrows its stored captures; it
                        // does not create new owners for value-captured fields.
                        return true;
                    },
                    [](const OwnerBindingStorage&) static noexcept { return false; },
                }
            );
            auto alias = std::optional<OwnershipPlace>();
            if (borrowed) {
                alias = OwnershipPlace {result.objects.size(), {}};
                result.objects.push_back(
                    {binding.type,
                     binding.origin,
                     {.available = true,
                      .taken = std::nullopt,
                      .relationships = relationships,
                      .modified = false},
                     {.body = source.id(), .slot = result.objects.size(), .input = true},
                     false}
                );
            }
            destination.push_back({std::move(alias), std::move(relationships), {}, std::nullopt});
        }
    };
    inputs(source.inputs().parameters, result.parameters);
    inputs(source.inputs().captures, result.captures);
    result.outlives.assign(result.objects.size(), std::vector<bool>(result.objects.size(), true));
    return result;
}

auto OwnershipBatchAnalyzer::run() noexcept -> AnalysisResult<void> {
    for (const auto [id, source] : bodies.entries()) {
        auto input = root_input(source);
        OwnershipBodyAnalyzer(*this, input, true).check_contracts();
        static_cast<void>(query(std::move(input)));
    }
    if (failure.has_value()) {
        return std::unexpected(*failure);
    }
    // Dependencies are discovered while evaluating structured bodies. Retaining
    // earlier dependencies is conservative when a call changes its input context.
    while (!pending_queries.empty()) {
        const auto index = pending_queries.front();
        pending_queries.pop_front();
        auto& query = *queries[index];
        query.queued = false;
        active_query = index;
        auto answer = OwnershipBodyAnalyzer(*this, query.input, false).run();
        active_query.reset();
        if (!answer.has_value()) {
            // Replay this invalid completion with diagnostics enabled so the
            // transfer that first violated a lifetime keeps its precise witness.
            static_cast<void>(OwnershipBodyAnalyzer(*this, query.input, true).run());
            if (!failure.has_value()) {
                const auto& escape = answer.error();
                diagnose(
                    DiagnosticCode::AccessBorrowConflict,
                    escape.message,
                    escape.origin,
                    escape.related
                );
            }
            return std::unexpected(*failure);
        }
        auto joined = query.answer;
        for (const auto& completion : *answer) {
            const auto found = std::ranges::find_if(joined, [&](const auto& previous) noexcept {
                return previous.test_stopped == completion.test_stopped
                    && previous.failure == completion.failure;
            });
            if (found == joined.end()) {
                joined.push_back(completion);
            } else {
                join_ownership_state(found->state, completion.state);
                merge_relationships(found->value, completion.value);
            }
        }
        std::ranges::sort(joined, {}, [](const auto& completion) static noexcept {
            return std::pair(completion.test_stopped, completion.failure);
        });
        if (joined != query.answer) {
            query.answer = std::move(joined);
            for (const auto consumer : query.consumers) {
                enqueue(consumer);
            }
        }
    }
    queries_sealed = true;
    for (const auto& query : queries) {
        const auto answer = OwnershipBodyAnalyzer(*this, query->input, true).run();
        if (!answer.has_value()) {
            const auto& escape = answer.error();
            diagnose(
                DiagnosticCode::AccessBorrowConflict,
                escape.message,
                escape.origin,
                escape.related
            );
        }
        if (failure.has_value()) {
            return std::unexpected(*failure);
        }
        if (*answer != query->answer) {
            invariant_violation("ownership diagnosis changed a solved call answer");
        }
    }
    return {};
}

auto analyze_body_batch(const SemIRProgram& program, AnalysisDiagnostics diagnostics) noexcept
    -> AnalysisResult<void> {
    return OwnershipBatchAnalyzer(program, diagnostics).run();
}
