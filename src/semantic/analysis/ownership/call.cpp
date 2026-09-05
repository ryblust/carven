module carven:semantic.analysis.ownership.call.impl;

import :semantic.analysis.ownership.context;
import :semantic.analysis.validation;
import std;

namespace ownership {

BatchAnalyzer::BatchAnalyzer(std::span<const SemIRBody> bodies, ProgramDraft& draft) noexcept
    : draft(draft),
      bodies(bodies) {}
auto BatchAnalyzer::body(BodyID id) const noexcept -> const SemIRBody& {
    const auto found = std::ranges::find(bodies, id, &SemIRBody::id);
    if (found == bodies.end()) {
        invariant_violation("ownership call refers to an absent body");
    }
    return *found;
}
auto BatchAnalyzer::diagnose(
    DiagnosticCode code,
    std::string message,
    ProgramOriginID origin,
    std::optional<ProgramOriginID> related
) noexcept -> void {
    if (failure.has_value()) {
        return;
    }
    auto diagnostic = DiagnosticBuilder(code, std::move(message));
    diagnostic.primary(draft.source_span(origin));
    if (related.has_value()) {
        diagnostic.related(draft.source_span(*related), "related storage or access");
    }
    failure = draft.diagnostics().error(diagnostic.build());
}
auto BatchAnalyzer::query(CallInput input) noexcept -> std::vector<CallCompletion> {
    for (const auto& query : queries) {
        if (query->input == input) {
            return query->answer;
        }
    }
    queries.push_back(std::make_unique<CallQuery>(CallQuery {std::move(input), {}}));
    return {};
}
auto BatchAnalyzer::root_input(const SemIRBody& source) const noexcept -> CallInput {
    auto result = CallInput {source.id(), {}, {}, {}, {}, {}};
    auto contents = TypeContentsQuery(draft);
    const auto abstract_value =
        [&](this const auto& self, TypeID type, ProgramOriginID origin) noexcept -> Relationships {
        auto relationships = Relationships {};
        const auto value = draft.type_copy(type).value;
        if (std::holds_alternative<CallableViewTypeValue>(value)) {
            relationships.loans.push_back({{}, std::nullopt, std::nullopt, origin, false});
        } else if (const auto* closure = std::get_if<ClosureTypeValue>(&value)) {
            const auto& target = body(*draft.body_for_callable(closure->callable));
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
                          .relationships = std::move(captured)}}
                    );
                    relationships.captures.push_back(
                        {ProjectionPath {index}, Place {object, {}}, capture.origin}
                    );
                } else {
                    merge_relationships(
                        relationships,
                        nested(std::move(captured), ProjectionPath {index})
                    );
                }
            }
        } else if (const auto* array = std::get_if<ArrayTypeValue>(&value); array != nullptr
                   && (contents.contains_view(type) || contents.contents(type).closure_owner)) {
            relationships = nested(self(array->element, origin), ProjectionPath {std::nullopt});
        }
        return relationships;
    };
    const auto inputs = [&](std::span<const LocalBindingID> ids,
                            std::vector<CallArgument>& destination) noexcept {
        for (const auto id : ids) {
            const auto& binding = source.binding(id);
            auto relationships = abstract_value(binding.type, binding.origin);
            const auto borrowed = std::visit(
                Overloaded {
                    [](const ParameterBindingStorage& value) static noexcept {
                        return value.access != AccessMode::Take;
                    },
                    [](const CaptureBindingStorage& value) static noexcept {
                        return value.mode == CaptureMode::Write;
                    },
                    [](const OwnerBindingStorage&) static noexcept { return false; },
                },
                binding.storage
            );
            auto alias = std::optional<Place>();
            if (borrowed) {
                alias = Place {result.objects.size(), {}};
                result.objects.push_back(
                    {binding.type,
                     binding.origin,
                     {.available = true, .taken = std::nullopt, .relationships = relationships}}
                );
            }
            destination.push_back({std::move(alias), std::move(relationships)});
        }
    };
    inputs(source.inputs().parameters, result.parameters);
    inputs(source.inputs().captures, result.captures);
    result.outlives.assign(result.objects.size(), std::vector<bool>(result.objects.size(), true));
    return result;
}
auto BatchAnalyzer::run() noexcept -> AnalysisResult<void> {
    for (const auto& source : bodies) {
        verify_semantic_body(source, draft, bodies);
        auto input = root_input(source);
        BodyAnalyzer(*this, input, true).check_contracts();
        static_cast<void>(query(std::move(input)));
    }
    if (failure.has_value()) {
        return std::unexpected(*failure);
    }
    // The equations are over finite input relationships and exit relationships.
    // A recursive call reads the current answer; no execution graph is built.
    for (;;) {
        auto changed = false;
        const auto count = queries.size();
        for (auto index = 0uz; index < queries.size(); ++index) {
            auto& query = *queries[index];
            auto answer = BodyAnalyzer(*this, query.input, false).run();
            if (answer != query.answer) {
                query.answer = std::move(answer);
                changed = true;
            }
        }
        if (!changed && count == queries.size()) {
            break;
        }
    }
    for (const auto& query : queries) {
        static_cast<void>(BodyAnalyzer(*this, query->input, true).run());
        if (failure.has_value()) {
            return std::unexpected(*failure);
        }
    }
    return {};
}
auto BodyAnalyzer::run() noexcept -> std::vector<CallCompletion> {
    auto state = State {.objects = std::vector<ObjectState>(objects.size())};
    for (auto index = 0uz; index < input.objects.size(); ++index) {
        state.objects[index] = input.objects[index].state;
    }
    const auto initialize = [&](std::span<const LocalBindingID> bindings,
                                std::span<const CallArgument> values) noexcept {
        for (const auto& [id, value] : std::views::zip(bindings, values)) {
            const auto* parameter = std::get_if<ParameterBindingStorage>(&body.binding(id).storage);
            // A Read may be a value copy. Its snapshot remains a possible holder
            // independently of whether target traits choose a reference.
            const auto copy = !value.alias.has_value()
                || (parameter != nullptr && parameter->access == AccessMode::Read);
            state.objects[input.objects.size() + id.index()] = {
                .available = true,
                .taken = std::nullopt,
                .relationships = copy ? value.value : Relationships {}
            };
        }
    };
    initialize(body.inputs().parameters, input.parameters);
    initialize(body.inputs().captures, input.captures);
    auto flow = region(body.region(), std::move(state));
    auto result = std::vector<CallCompletion>();
    const auto complete =
        [&](std::optional<TypeID> failure, State state, Relationships value) noexcept {
            auto valid = true;
            const auto check = [&](const Relationships& relationships) noexcept {
                for (const auto& capture : relationships.captures) {
                    if (capture.target.object >= input.objects.size()) {
                        diagnose(
                            DiagnosticCode::AccessBorrowConflict,
                            "escaping closure outlives its captured owner",
                            body.region().origin,
                            capture.origin
                        );
                        valid = false;
                    }
                }
                for (const auto& loan : relationships.loans) {
                    if (loan.backing.has_value() && loan.backing->object >= input.objects.size()) {
                        diagnose(
                            DiagnosticCode::AccessBorrowConflict,
                            "escaping callable storage outlives its backing",
                            body.region().origin,
                            loan.origin
                        );
                        valid = false;
                    }
                }
            };
            check(value);
            state.objects.resize(input.objects.size());
            for (const auto& object : state.objects) {
                check(object.relationships);
            }
            if (!valid) {
                return;
            }
            const auto found = std::ranges::find(result, failure, &CallCompletion::failure);
            if (found == result.end()) {
                result.push_back({failure, std::move(state), std::move(value)});
            } else {
                join(found->state, state);
                merge_relationships(found->value, value);
            }
        };
    if (flow.normal.has_value()) {
        const auto callable = draft.callable_for_body(body.id());
        if (callable.has_value() && !body.region().result.has_value()) {
            const auto result_type =
                draft.concrete_type(draft.construction_callable_contract_copy(*callable).result);
            if (draft.type_copy(result_type).value
                != CanonicalTypeValue {BuiltinTypeValue {.kind = BuiltinType::Void}}) {
                invariant_violation("normal callable exit did not deliver its result");
            }
        }
        complete(std::nullopt, std::move(*flow.normal), std::move(flow.value));
    }
    for (auto& exit : flow.exits) {
        if (exit.kind == ExitKind::Return || exit.kind == ExitKind::Failure) {
            complete(exit.failure, std::move(exit.state), std::move(exit.value));
        } else {
            invariant_violation("loop transfer escaped its callable");
        }
    }
    std::ranges::sort(result, {}, &CallCompletion::failure);
    return result;
}
auto BodyAnalyzer::call(
    CallableID callable,
    const Relationships& captures,
    std::span<const CallArgument> parameters,
    State state,
    ProgramOriginID origin
) noexcept -> Flow {
    const auto target_id = draft.body_for_callable(callable);
    if (!target_id.has_value()) {
        auto result = Flow {.normal = std::move(state), .value = {}, .exits = {}};
        const auto contract = draft.construction_callable_contract_copy(callable);
        for (const auto type :
             draft.failure_set_copy(draft.concrete_failure_set(contract.failures)).members) {
            result.exits.push_back({ExitKind::Failure, type, *result.normal, {}});
        }
        return result;
    }
    const auto& target = analysis.body(*target_id);
    auto call_input = CallInput {target.id(), {}, {}, {}, {}, {}};
    auto sources = std::vector<std::size_t>();
    auto normalized = std::flat_map<std::size_t, std::size_t>();
    const auto map_place = [&](Place place) noexcept {
        const auto [found, inserted] = normalized.emplace(place.object, sources.size());
        if (inserted) {
            sources.push_back(place.object);
        }
        place.object = found->second;
        return place;
    };
    const auto map_facts = [&](Relationships value) noexcept {
        for (auto& capture : value.captures) {
            capture.target = map_place(std::move(capture.target));
        }
        for (auto& loan : value.loans) {
            loan.direct_only = false;
            if (loan.backing.has_value()) {
                loan.backing = map_place(std::move(*loan.backing));
            }
        }
        return value;
    };
    const auto map_input = [&](CallArgument value) noexcept {
        if (value.alias.has_value()) {
            value.alias = map_place(std::move(*value.alias));
        }
        value.value = map_facts(std::move(value.value));
        return value;
    };
    for (const auto& parameter : parameters) {
        call_input.parameters.push_back(map_input(parameter));
    }
    for (const auto [index, id] : std::views::enumerate(target.inputs().captures)) {
        auto value = project(captures, ProjectionPath {index});
        auto alias = std::optional<Place>();
        if (std::get<CaptureBindingStorage>(target.binding(id).storage).mode
            == CaptureMode::Write) {
            const auto found =
                std::ranges::find_if(value.captures, [](const Capture& capture) static noexcept {
                    return capture.holder.empty();
                });
            if (found == value.captures.end()) {
                invariant_violation("closure call lost a Write capture target");
            }
            alias = found->target;
            write_access(*alias, origin);
            value = project(state.objects[alias->object].relationships, alias->path);
        }
        call_input.captures.push_back(map_input({std::move(alias), std::move(value)}));
    }
    for (auto index = 0uz; index < sources.size(); ++index) {
        const auto source = sources[index];
        auto object_state = state.objects[source];
        object_state.relationships = map_facts(std::move(object_state.relationships));
        call_input.objects.push_back(
            {objects[source].type, objects[source].origin, std::move(object_state)}
        );
    }
    for (const auto source : sources) {
        auto row = std::vector<bool>();
        for (const auto destination : sources) {
            row.push_back(outlives(source, destination));
        }
        call_input.outlives.push_back(std::move(row));
    }
    for (const auto& access : accesses) {
        if (normalized.contains(access.place.object)) {
            call_input.accesses.push_back({map_place(access.place), access.stable});
        }
    }
    std::ranges::sort(call_input.accesses);
    const auto duplicates = std::ranges::unique(call_input.accesses);
    call_input.accesses.erase(duplicates.begin(), duplicates.end());
    const auto restore_facts = [&](Relationships value) noexcept {
        for (auto& capture : value.captures) {
            capture.target.object = sources[capture.target.object];
        }
        for (auto& loan : value.loans) {
            if (loan.backing.has_value()) {
                loan.backing->object = sources[loan.backing->object];
            }
        }
        return value;
    };
    auto result = Flow {};
    for (auto answer : analysis.query(std::move(call_input))) {
        auto returned = state;
        for (auto index = 0uz; index < sources.size(); ++index) {
            auto object = std::move(answer.state.objects[index]);
            object.relationships = restore_facts(std::move(object.relationships));
            returned.objects[sources[index]] = std::move(object);
        }
        const auto value = restore_facts(std::move(answer.value));
        if (answer.failure.has_value()) {
            result.exits.push_back({ExitKind::Failure, answer.failure, std::move(returned), {}});
        } else {
            join_normal(result.normal, std::optional(std::move(returned)));
            merge_relationships(result.value, value);
        }
    }
    return result;
}

} // namespace ownership

auto analyze_body_batch(std::span<const SemIRBody> bodies, ProgramDraft& draft) noexcept
    -> AnalysisResult<void> {
    return ownership::BatchAnalyzer(bodies, draft).run();
}
