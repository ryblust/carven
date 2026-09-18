module carven:semantic.analysis.ownership.call.impl;

import :semantic.analysis.ownership.context;
import :semantic.semir.program;
import std;

auto OwnershipBodyAnalyzer::run() noexcept
    -> std::expected<std::vector<OwnershipCallCompletion>, OwnershipEscape> {
    auto state = OwnershipState {
        .objects = std::vector<OwnershipObjectState>(
            input.objects.size() + facts.locals.size(),
            OwnershipObjectState {
                .available = false,
                .taken = std::nullopt,
                .relationships = {},
                .modified = false
            }
        )
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
            const auto borrowed = parameter != nullptr
                && parameter->access == AccessMode::Read
                && analysis.contents(body.binding(id).type).read_borrows_storage();
            const auto copy = !borrowed
                && !value.capture_holder
                && ((!value.alias && value.storage.empty())
                    || (parameter && parameter->access == AccessMode::Read));
            state.objects[input.objects.size() + id.index()] = {
                .available = true,
                .taken = std::nullopt,
                .relationships = copy ? value.value : OwnershipRelationships {},
                .modified = false
            };
        }
    };
    initialize(body.inputs().parameters, input.parameters);
    initialize(body.inputs().captures, input.captures);
    auto flow = region(body.region(), std::move(state)).run();
    auto result = std::vector<OwnershipCallCompletion>();
    auto escape = std::optional<OwnershipEscape>();
    const auto complete = [&](bool test_stopped,
                              std::optional<TypeID> failure,
                              OwnershipState state,
                              OwnershipRelationships value) noexcept {
        const auto check = [&](const OwnershipRelationships& relationships) noexcept {
            if (escape.has_value()) {
                return;
            }
            for (const auto& capture : relationships.captures) {
                if (capture.target.object >= input.objects.size()) {
                    escape = OwnershipEscape {
                        "escaping closure outlives its captured owner",
                        body.region().origin,
                        capture.origin
                    };
                    return;
                }
            }
            for (const auto& loan : relationships.storage_loans) {
                if (loan.backing.object >= input.objects.size()) {
                    escape = OwnershipEscape {
                        "escaping view outlives its backing",
                        body.region().origin,
                        loan.origin
                    };
                    return;
                }
            }
            for (const auto& loan : relationships.callable_loans) {
                if (loan.backing.has_value() && loan.backing->object >= input.objects.size()) {
                    escape = OwnershipEscape {
                        "escaping callable storage outlives its backing",
                        body.region().origin,
                        loan.origin
                    };
                    return;
                }
            }
        };
        check(value);
        state.objects.resize(input.objects.size());
        for (const auto& object : state.objects) {
            check(object.relationships);
        }
        if (escape.has_value()) {
            return;
        }
        const auto found = std::ranges::find_if(result, [&](const auto& answer) noexcept {
            return answer.test_stopped == test_stopped && answer.failure == failure;
        });
        if (found == result.end()) {
            result.push_back({test_stopped, failure, std::move(state), std::move(value)});
        } else {
            join_ownership_state(found->state, state);
            merge_relationships(found->value, value);
        }
    };
    if (flow.normal.has_value()) {
        const auto callable = program.declarations().callable_for_body(body.id());
        if (callable.has_value() && !body.region().result.has_value()) {
            const auto result_type =
                program.callable_signatures()
                    .signature(program.declarations().callable(*callable).signature)
                    .result;
            if (program.types().type(result_type).value
                != CanonicalTypeValue {BuiltinTypeValue {.kind = BuiltinType::Void}}) {
                invariant_violation("normal callable exit did not deliver its result");
            }
        }
        complete(false, std::nullopt, std::move(flow.normal->state), std::move(flow.normal->value));
    }
    for (auto& exit : flow.exits) {
        exit.payload.visit(
            Overloaded {
                [&](OwnershipReturn& value) noexcept {
                    complete(false, std::nullopt, std::move(exit.state), std::move(value.value));
                },
                [&](OwnershipFailure& value) noexcept {
                    complete(false, value.type, std::move(exit.state), std::move(value.value));
                },
                [&](OwnershipTestStopped&) noexcept {
                    complete(true, std::nullopt, std::move(exit.state), {});
                },
                [](const auto&) static noexcept {
                    invariant_violation("loop transfer escaped its callable");
                }
            }
        );
    }
    if (escape.has_value()) {
        return std::unexpected(std::move(*escape));
    }
    for (auto& completion : result) {
        normalize_relationships(completion.value);
        for (auto& object : completion.state.objects) {
            normalize_relationships(object.relationships);
        }
    }
    std::ranges::sort(result, {}, [](const auto& answer) static noexcept {
        return std::pair(answer.test_stopped, answer.failure);
    });
    return result;
}

auto OwnershipBodyAnalyzer::call(
    CallableID callable,
    const OwnershipRelationships& captures,
    std::optional<OwnershipPlace> capture_owner,
    std::span<const OwnershipCallArgument> parameters,
    OwnershipState state,
    ProgramOriginID origin
) noexcept -> OwnershipFlow {
    const auto target_id = program.declarations().body_for_callable(callable);
    if (!target_id.has_value()) {
        auto result =
            OwnershipFlow {.normal = OwnershipNormal {std::move(state), {}, {}}, .exits = {}};
        const auto contract = program.callable_signatures().signature(
            program.declarations().callable(callable).signature
        );
        for (const auto type : program.failure_sets().failure_set(contract.failures).members) {
            result.exits.push_back({OwnershipFailure {type, {}}, result.normal->state});
        }
        return result;
    }
    const auto& target = analysis.body(*target_id);
    auto raw_captures = std::vector<OwnershipCallArgument>();
    for (const auto [index, id] : std::views::enumerate(target.inputs().captures)) {
        auto value = project_relationships(captures, OwnershipProjectionPath {index});
        auto alias = std::optional<OwnershipPlace>();
        auto holder = std::optional<OwnershipPlace>();
        auto storage = std::vector<OwnershipPlace>();
        if (std::get<CaptureBindingStorage>(target.binding(id).storage).mode
            == CaptureMode::Write) {
            for (const auto& capture : value.captures) {
                if (capture.holder.empty()) {
                    storage.push_back(capture.target);
                    write_access(capture.target, origin);
                }
            }
            if (storage.empty()) {
                invariant_violation("closure call lost a Write capture target");
            }
            if (capture_owner) {
                holder = *capture_owner;
                holder->path.push_back(index);
            }
            value = {};
            for (const auto& place : storage) {
                merge_relationships(
                    value,
                    project_relationships(state.objects[place.object].relationships, place.path)
                );
            }
        } else if (capture_owner) {
            alias = *capture_owner;
            alias->path.push_back(index);
        }
        raw_captures.push_back({alias, std::move(value), std::move(storage), holder});
    }

    const auto site_for = [&](std::size_t object) noexcept {
        if (object < input.objects.size()) {
            return input.objects[object].site;
        }
        return OwnershipAllocationSite {body.id(), object - input.objects.size(), false};
    };
    const auto many_for = [&](std::size_t object) noexcept {
        return object < input.objects.size() && input.objects[object].many;
    };
    auto reachable = std::vector<std::size_t>();
    auto seen = std::flat_set<std::size_t>();
    auto distinguished = std::flat_set<std::size_t>();
    const auto discover = [&](std::size_t object) noexcept {
        if (seen.insert(object).second) {
            reachable.push_back(object);
        }
    };
    const auto distinguish = [&](std::size_t object) noexcept {
        discover(object);
        if (!many_for(object)) {
            distinguished.insert(object);
        }
    };
    const auto visit_facts = [&](const OwnershipRelationships& value) noexcept {
        for (const auto& row : value.captures) {
            discover(row.target.object);
        }
        for (const auto& row : value.storage_loans) {
            discover(row.backing.object);
        }
        for (const auto& row : value.callable_loans) {
            if (row.backing) {
                discover(row.backing->object);
            }
        }
    };
    const auto visit_input = [&](const OwnershipCallArgument& argument) noexcept {
        if (argument.alias) {
            distinguish(argument.alias->object);
        }
        if (argument.capture_holder) {
            distinguish(argument.capture_holder->object);
        }
        for (const auto& place : argument.storage) {
            discover(place.object);
        }
        if (argument.storage.size() == 1uz) {
            distinguish(argument.storage.front().object);
        }
        visit_facts(argument.value);
        // Only an unambiguous direct referent earns a root role. A many node
        // remains many even when a single edge names it.
        if (argument.value.storage_loans.size() == 1uz
            && argument.value.storage_loans.front().holder.empty()) {
            distinguish(argument.value.storage_loans.front().backing.object);
        }
        if (argument.value.callable_loans.size() == 1uz
            && argument.value.callable_loans.front().backing) {
            distinguish(argument.value.callable_loans.front().backing->object);
        }
    };
    for (const auto& argument : parameters) {
        visit_input(argument);
    }
    for (const auto& argument : raw_captures) {
        visit_input(argument);
    }
    // Inline fields and callable capture storage have a finite structural shape.
    // Preserve their exact roots; recursive storage can grow only across a
    // slice backing edge, which is deliberately not followed here.
    auto inline_roots = std::vector<std::size_t>(distinguished.begin(), distinguished.end());
    const auto inline_target = [&](std::size_t object) noexcept {
        discover(object);
        if (!many_for(object) && distinguished.insert(object).second) {
            inline_roots.push_back(object);
        }
    };
    const auto inline_facts = [&](const OwnershipRelationships& value) noexcept {
        auto slots = std::flat_map<OwnershipProjectionPath, std::optional<std::size_t>>();
        const auto add = [&](const OwnershipProjectionPath& holder,
                             std::optional<std::size_t> object) noexcept {
            if (!std::ranges::all_of(holder, [](const auto& part) static noexcept {
                    return part.has_value();
                })) {
                return;
            }
            const auto [found, inserted] = slots.emplace(holder, object);
            if (!inserted) {
                found->second.reset();
            }
        };
        for (const auto& row : value.captures) {
            add(row.holder, row.target.object);
        }
        for (const auto& row : value.callable_loans) {
            add(row.holder, row.backing ? std::optional(row.backing->object) : std::nullopt);
        }
        for (const auto& [holder, object] : slots) {
            if (object) {
                inline_target(*object);
            }
        }
    };
    for (const auto& argument : parameters) {
        inline_facts(argument.value);
    }
    for (const auto& argument : raw_captures) {
        inline_facts(argument.value);
    }
    for (auto cursor = 0uz; cursor < inline_roots.size(); ++cursor) {
        inline_facts(state.objects[inline_roots[cursor]].relationships);
    }
    for (auto cursor = 0uz; cursor < reachable.size(); ++cursor) {
        visit_facts(state.objects[reachable[cursor]].relationships);
    }
    auto sources = std::vector<std::vector<std::size_t>>();
    auto normalized = std::flat_map<std::size_t, std::size_t>();
    auto summaries = std::flat_map<OwnershipAllocationSite, std::size_t>();
    for (const auto source : reachable) {
        auto index = sources.size();
        if (!distinguished.contains(source)) {
            const auto [found, inserted] = summaries.emplace(site_for(source), index);
            index = found->second;
        }
        if (index == sources.size()) {
            sources.emplace_back();
        }
        sources[index].push_back(source);
        normalized.emplace(source, index);
    }
    const auto map_place = [&](OwnershipPlace place) noexcept {
        place.object = normalized.at(place.object);
        return place;
    };
    const auto map_facts = [&](OwnershipRelationships value) noexcept {
        for (auto& row : value.captures) {
            row.target = map_place(std::move(row.target));
        }
        for (auto& row : value.storage_loans) {
            row.backing = map_place(std::move(row.backing));
        }
        for (auto& row : value.callable_loans) {
            row.direct_only = false;
            if (row.backing) {
                row.backing = map_place(std::move(*row.backing));
            }
        }
        normalize_relationships(value);
        return value;
    };
    const auto map_argument = [&](OwnershipCallArgument argument) noexcept {
        if (argument.alias) {
            argument.alias = map_place(std::move(*argument.alias));
        }
        if (argument.capture_holder) {
            argument.capture_holder = map_place(std::move(*argument.capture_holder));
        }
        for (auto& place : argument.storage) {
            place = map_place(std::move(place));
        }
        argument.value = map_facts(std::move(argument.value));
        return argument;
    };
    auto call_input = OwnershipCallInput {target.id(), {}, {}, {}, {}, {}, {}};
    for (const auto& argument : parameters) {
        call_input.parameters.push_back(map_argument(argument));
    }
    for (const auto& argument : raw_captures) {
        call_input.captures.push_back(map_argument(argument));
    }
    for (const auto& group : sources) {
        const auto first = group.front();
        auto object = OwnershipExternalObject {
            object_type(first),
            object_origin(first),
            {.available = true, .taken = std::nullopt, .relationships = {}, .modified = false},
            site_for(first),
            group.size() > 1uz
        };
        for (const auto source : group) {
            object.many |= many_for(source);
            object.state.available &= state.objects[source].available;
            if (state.objects[source].taken) {
                object.state.taken = state.objects[source].taken;
            }
            merge_relationships(
                object.state.relationships,
                map_facts(state.objects[source].relationships)
            );
        }
        call_input.objects.push_back(std::move(object));
    }
    const auto protect_external = [&](std::span<const OwnershipStorageLoan> loans) noexcept {
        for (auto loan : loans) {
            if (normalized.contains(loan.backing.object)) {
                loan.backing = map_place(std::move(loan.backing));
                loan.holder.clear();
                call_input.storage_readers.push_back(std::move(loan));
            }
        }
    };
    protect_external(storage_readers);
    for (const auto [holder, object] : std::views::enumerate(state.objects)) {
        if (!normalized.contains(static_cast<std::size_t>(holder))) {
            protect_external(object.relationships.storage_loans);
        }
    }
    for (const auto& from : sources) {
        auto row = std::vector<bool>();
        for (const auto& to : sources) {
            auto valid = true;
            for (const auto source : from) {
                for (const auto destination : to) {
                    valid &= outlives(source, destination);
                }
            }
            row.push_back(valid);
        }
        call_input.outlives.push_back(std::move(row));
    }
    for (const auto& access : accesses) {
        if (normalized.contains(access.place.object)) {
            call_input.accesses.push_back({map_place(access.place), access.stable});
        }
    }
    std::ranges::sort(call_input.accesses);
    call_input.accesses.erase(
        std::ranges::unique(call_input.accesses).begin(),
        call_input.accesses.end()
    );
    const auto restore_facts = [&](const OwnershipRelationships& value) noexcept {
        auto restored = OwnershipRelationships {};
        for (const auto& row : value.captures) {
            for (const auto source : sources[row.target.object]) {
                auto copy = row;
                copy.target.object = source;
                restored.captures.push_back(std::move(copy));
            }
        }
        for (const auto& row : value.storage_loans) {
            for (const auto source : sources[row.backing.object]) {
                auto copy = row;
                copy.backing.object = source;
                restored.storage_loans.push_back(std::move(copy));
            }
        }
        for (const auto& row : value.callable_loans) {
            if (!row.backing) {
                restored.callable_loans.push_back(row);
            } else {
                for (const auto source : sources[row.backing->object]) {
                    auto copy = row;
                    copy.backing->object = source;
                    restored.callable_loans.push_back(std::move(copy));
                }
            }
        }
        normalize_relationships(restored);
        return restored;
    };
    const auto many = call_input.objects
        | std::views::transform([](const auto& object) static noexcept { return object.many; })
        | std::ranges::to<std::vector>();
    auto result = OwnershipFlow {};
    for (const auto& answer : analysis.query(std::move(call_input))) {
        auto returned = state;
        for (auto index = 0uz; index < sources.size(); ++index) {
            const auto& object = answer.state.objects[index];
            if (!object.modified) {
                continue;
            }
            auto restored = object;
            restored.relationships = restore_facts(object.relationships);
            for (const auto source : sources[index]) {
                auto& destination = returned.objects[source];
                if (many[index]) {
                    destination.available &= restored.available;
                    destination.modified = true;
                    if (restored.taken) {
                        destination.taken = restored.taken;
                    }
                    merge_relationships(destination.relationships, restored.relationships);
                } else {
                    destination = restored;
                }
            }
        }
        const auto value = restore_facts(answer.value);
        if (answer.test_stopped) {
            result.exits.push_back({OwnershipTestStopped {}, std::move(returned)});
        } else if (answer.failure) {
            result.exits.push_back(
                {OwnershipFailure {*answer.failure, value}, std::move(returned)}
            );
        } else {
            join_normal_ownership(
                result.normal,
                std::optional(OwnershipNormal {std::move(returned), value, {}})
            );
        }
    }
    return result;
}
