module carven:semantic.analysis.ownership.call.impl;

import :semantic.analysis.ownership.context;
import :semantic.analysis.program;
import std;

auto OwnershipBodyAnalyzer::run() noexcept -> std::vector<OwnershipCallCompletion> {
    auto state = OwnershipState {
        .objects = std::vector<OwnershipObjectState>(input.objects.size() + facts.locals.size())
    };
    for (auto index = 0uz; index < input.objects.size(); ++index) {
        state.objects[index] = input.objects[index].state;
    }
    const auto initialize = [&](std::span<const LocalBindingID> bindings,
                                std::span<const OwnershipCallArgument> values) noexcept {
        for (const auto& [id, value] : std::views::zip(bindings, values)) {
            const auto* parameter = std::get_if<ParameterBindingStorage>(&body.binding(id).storage);
            // A Read may be a value copy. Its snapshot remains a possible holder
            // independently of whether target traits choose a reference.
            const auto copy = !value.alias.has_value()
                || (parameter != nullptr && parameter->access == AccessMode::Read);
            state.objects[input.objects.size() + id.index()] = {
                .available = true,
                .taken = std::nullopt,
                .relationships = copy ? value.value : OwnershipRelationships {}
            };
        }
    };
    initialize(body.inputs().parameters, input.parameters);
    initialize(body.inputs().captures, input.captures);
    auto flow = region(body.region(), std::move(state));
    auto result = std::vector<OwnershipCallCompletion>();
    const auto complete = [&](std::optional<TypeID> failure,
                              OwnershipState state,
                              OwnershipRelationships value) noexcept {
        auto valid = true;
        const auto check = [&](const OwnershipRelationships& relationships) noexcept {
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
        const auto found = std::ranges::find(result, failure, &OwnershipCallCompletion::failure);
        if (found == result.end()) {
            result.push_back({failure, std::move(state), std::move(value)});
        } else {
            join_ownership_state(found->state, state);
            merge_relationships(found->value, value);
        }
    };
    if (flow.normal.has_value()) {
        const auto callable = draft.callable_for_body(body.id());
        if (callable.has_value() && !body.region().result.has_value()) {
            const auto result_type =
                draft.callable_signatures().signature(draft.callable_signature(*callable)).result;
            if (draft.types().type(result_type).value
                != CanonicalTypeValue {BuiltinTypeValue {.kind = BuiltinType::Void}}) {
                invariant_violation("normal callable exit did not deliver its result");
            }
        }
        complete(std::nullopt, std::move(*flow.normal), std::move(flow.value));
    }
    for (auto& exit : flow.exits) {
        if (exit.kind == OwnershipExitKind::Return || exit.kind == OwnershipExitKind::Failure) {
            complete(exit.failure, std::move(exit.state), std::move(exit.value));
        } else {
            invariant_violation("loop transfer escaped its callable");
        }
    }
    for (auto& completion : result) {
        normalize_relationships(completion.value);
        for (auto& object : completion.state.objects) {
            normalize_relationships(object.relationships);
        }
    }
    std::ranges::sort(result, {}, &OwnershipCallCompletion::failure);
    return result;
}

auto OwnershipBodyAnalyzer::call(
    CallableID callable,
    const OwnershipRelationships& captures,
    std::span<const OwnershipCallArgument> parameters,
    OwnershipState state,
    ProgramOriginID origin
) noexcept -> OwnershipFlow {
    const auto target_id = draft.body_for_callable(callable);
    if (!target_id.has_value()) {
        auto result = OwnershipFlow {.normal = std::move(state), .value = {}, .exits = {}};
        const auto contract =
            draft.callable_signatures().signature(draft.callable_signature(callable));
        for (const auto type : draft.failure_sets().failure_set(contract.failures).members) {
            result.exits.push_back({OwnershipExitKind::Failure, type, *result.normal, {}});
        }
        return result;
    }
    const auto& target = analysis.body(*target_id);
    auto call_input = OwnershipCallInput {target.id(), {}, {}, {}, {}, {}};
    auto sources = std::vector<std::size_t>();
    auto normalized = std::flat_map<std::size_t, std::size_t>();
    const auto map_place = [&](OwnershipPlace place) noexcept {
        const auto [found, inserted] = normalized.emplace(place.object, sources.size());
        if (inserted) {
            sources.push_back(place.object);
        }
        place.object = found->second;
        return place;
    };
    const auto map_facts = [&](OwnershipRelationships value) noexcept {
        for (auto& capture : value.captures) {
            capture.target = map_place(std::move(capture.target));
        }
        for (auto& loan : value.loans) {
            loan.direct_only = false;
            if (loan.backing.has_value()) {
                loan.backing = map_place(std::move(*loan.backing));
            }
        }
        normalize_relationships(value);
        return value;
    };
    const auto map_input = [&](OwnershipCallArgument value) noexcept {
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
        auto value = project_relationships(captures, OwnershipProjectionPath {index});
        auto alias = std::optional<OwnershipPlace>();
        if (std::get<CaptureBindingStorage>(target.binding(id).storage).mode
            == CaptureMode::Write) {
            const auto found = std::ranges::find_if(
                value.captures,
                [](const OwnershipCapture& capture) static noexcept {
                    return capture.holder.empty();
                }
            );
            if (found == value.captures.end()) {
                invariant_violation("closure call lost a Write capture target");
            }
            alias = found->target;
            write_access(*alias, origin);
            value = project_relationships(state.objects[alias->object].relationships, alias->path);
        }
        call_input.captures.push_back(map_input({std::move(alias), std::move(value)}));
    }
    for (auto index = 0uz; index < sources.size(); ++index) {
        const auto source = sources[index];
        auto object_state = state.objects[source];
        object_state.relationships = map_facts(std::move(object_state.relationships));
        call_input.objects.push_back(
            {object_type(source), object_origin(source), std::move(object_state)}
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
    const auto restore_facts = [&](OwnershipRelationships value) noexcept {
        for (auto& capture : value.captures) {
            capture.target.object = sources[capture.target.object];
        }
        for (auto& loan : value.loans) {
            if (loan.backing.has_value()) {
                loan.backing->object = sources[loan.backing->object];
            }
        }
        normalize_relationships(value);
        return value;
    };
    auto result = OwnershipFlow {};
    for (auto answer : analysis.query(std::move(call_input))) {
        auto returned = state;
        for (auto index = 0uz; index < sources.size(); ++index) {
            auto object = std::move(answer.state.objects[index]);
            object.relationships = restore_facts(std::move(object.relationships));
            returned.objects[sources[index]] = std::move(object);
        }
        const auto value = restore_facts(std::move(answer.value));
        if (answer.failure.has_value()) {
            result.exits.push_back(
                {OwnershipExitKind::Failure, answer.failure, std::move(returned), {}}
            );
        } else {
            join_normal_ownership_state(result.normal, std::optional(std::move(returned)));
            merge_relationships(result.value, value);
        }
    }
    return result;
}
