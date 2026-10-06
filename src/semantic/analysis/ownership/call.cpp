module carven:semantic.analysis.ownership.call.impl;

import :semantic.analysis.ownership.context;
import :semantic.semir.program;
import std;

auto OwnershipBodyAnalyzer::run() noexcept -> OwnershipBodyResult {
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
            for (const auto& capture : relationships.view().captures) {
                if (capture.target.object >= input.objects.size()) {
                    escape = OwnershipEscape {
                        "escaping closure outlives its captured owner",
                        body.region().origin,
                        capture.origin
                    };
                    return;
                }
            }
            for (const auto& loan : relationships.view().storage_loans) {
                if (loan.backing.object >= input.objects.size()) {
                    escape = OwnershipEscape {
                        "escaping view outlives its backing",
                        body.region().origin,
                        loan.origin
                    };
                    return;
                }
            }
            for (const auto& loan : relationships.view().callable_loans) {
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
        return OwnershipBodyResult {
            .answer = std::unexpected(std::move(*escape)),
            .diagnosis = std::move(diagnosis)
        };
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
    return OwnershipBodyResult {.answer = std::move(result), .diagnosis = std::move(diagnosis)};
}

auto OwnershipBodyAnalyzer::call(
    CallableID callable,
    const OwnershipRelationships& captures,
    std::optional<OwnershipPlace> capture_owner,
    std::span<const OwnershipCallArgument> arguments,
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
    // Argument evaluation has already checked accesses. Value Read parameters
    // copy contained relationships, not the source holder's storage identity.
    auto parameters = std::vector<OwnershipCallArgument>(arguments.begin(), arguments.end());
    for (auto&& [id, argument] : std::views::zip(target.inputs().parameters, parameters)) {
        const auto& binding = target.binding(id);
        const auto* parameter = std::get_if<ParameterBindingStorage>(&binding.storage);
        if (parameter != nullptr
            && parameter->access == AccessMode::Read
            && analysis.contents(binding.type).read_is_value_snapshot()) {
            argument.alias.reset();
            argument.storage.clear();
        }
    }
    auto raw_captures = std::vector<OwnershipCallArgument>();
    for (const auto [index, id] : std::views::enumerate(target.inputs().captures)) {
        auto value = project_relationships(captures, OwnershipProjectionPath {index});
        auto alias = std::optional<OwnershipPlace>();
        auto holder = std::optional<OwnershipPlace>();
        auto storage = std::vector<OwnershipPlace>();
        if (std::get<CaptureBindingStorage>(target.binding(id).storage).mode
            == CaptureMode::Write) {
            for (const auto& capture : value.view().captures) {
                if (capture.holder.empty()) {
                    storage.push_back(capture.target);
                    write_access(capture.target, origin, false);
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

    auto graph_objects = std::vector<OwnershipExternalObject>();
    auto actual_sources = std::vector<OwnershipPlace>();
    auto edges = input.owns;
    for (auto object = 0uz; object < state.objects.size(); ++object) {
        graph_objects.push_back(
            {object_type(object),
             object_origin(object),
             state.objects[object],
             object < input.objects.size()
                 ? input.objects[object].site
                 : OwnershipStorageSite {body.id(), object - input.objects.size(), false},
             object < input.objects.size() && input.objects[object].many}
        );
        actual_sources.push_back({object, {}});
    }
    // Split at checked storage edges, never by guessing an enum payload type.
    // The caller coordinates live only in this frame's restoration map.
    const auto project_place = [&](OwnershipPlace place) noexcept {
        auto offset = 0uz;
        auto object = place.object;
        for (const auto& boundary : place.indirections) {
            auto carrier = OwnershipPlace {
                object,
                OwnershipProjectionPath(
                    place.path.begin() + static_cast<std::ptrdiff_t>(offset),
                    place.path.begin() + static_cast<std::ptrdiff_t>(boundary.offset)
                )
            };
            const auto index = place.path[boundary.offset];
            const auto uncertain =
                !index || std::ranges::any_of(carrier.path, [](const auto& part) noexcept {
                    return !part;
                });
            const auto found = std::ranges::find_if(edges, [&](const auto& edge) noexcept {
                return edge.direct
                    && edge.carrier == carrier
                    && edge.index == index
                    && graph_objects[edge.element].type == boundary.element
                    && (!uncertain
                        || graph_objects[edge.element].site.element_selection == boundary.site);
            });
            auto element = graph_objects.size();
            if (found != edges.end()) {
                element = found->element;
            } else {
                auto selected = carrier.path;
                selected.push_back(index);
                auto actual = actual_sources[object];
                const auto base = actual.path.size();
                actual.path.append_range(selected);
                actual.indirections.push_back(
                    {base + carrier.path.size(), boundary.element, boundary.site}
                );
                auto projected = graph_objects[object].state;
                projected.relationships = project_relationships(projected.relationships, selected);
                projected.modified = false;
                graph_objects.push_back(
                    {boundary.element,
                     boundary.site,
                     std::move(projected),
                     {body.id(), boundary.site.index(), false, boundary.site},
                     graph_objects[object].many || uncertain}
                );
                actual_sources.push_back(std::move(actual));
                edges.push_back({std::move(carrier), element, index, true});
            }
            object = element;
            offset = boundary.offset + 1uz;
        }
        place.object = object;
        place.path.erase(
            place.path.begin(),
            place.path.begin() + static_cast<std::ptrdiff_t>(offset)
        );
        place.indirections.clear();
        return place;
    };
    const auto project_facts = [&](OwnershipRelationships value) noexcept {
        if (auto* rows = value.edit_existing()) {
            for (auto& row : rows->captures) {
                row.target = project_place(std::move(row.target));
            }
            for (auto& row : rows->storage_loans) {
                row.backing = project_place(std::move(row.backing));
            }
            for (auto& row : rows->callable_loans) {
                if (row.backing) {
                    row.backing = project_place(std::move(*row.backing));
                }
            }
        }
        return value;
    };
    const auto project_argument = [&](OwnershipCallArgument& argument) noexcept {
        if (argument.alias) {
            argument.alias = project_place(std::move(*argument.alias));
        }
        if (argument.capture_holder) {
            argument.capture_holder = project_place(std::move(*argument.capture_holder));
        }
        for (auto& place : argument.storage) {
            place = project_place(std::move(place));
        }
        argument.value = project_facts(std::move(argument.value));
    };
    for (auto& argument : parameters) {
        project_argument(argument);
    }
    for (auto& argument : raw_captures) {
        project_argument(argument);
    }
    auto projected_accesses = accesses;
    for (auto& access : projected_accesses) {
        access.place = project_place(std::move(access.place));
    }
    auto projected_readers = storage_readers;
    for (auto& loan : projected_readers) {
        loan.backing = project_place(std::move(loan.backing));
    }
    for (auto object = 0uz; object < graph_objects.size(); ++object) {
        auto relationships = project_facts(graph_objects[object].state.relationships);
        graph_objects[object].state.relationships = std::move(relationships);
    }
    const auto site_for = [&](std::size_t object) noexcept {
        return graph_objects[object].site;
    };
    const auto many_for = [&](std::size_t object) noexcept {
        return graph_objects[object].many;
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
        // Interface roles remain distinct even when their referents are many.
        // The many bit still forbids strong updates.
        distinguished.insert(object);
    };
    const auto visit_facts = [&](const OwnershipRelationships& value) noexcept {
        for (const auto& row : value.view().captures) {
            discover(row.target.object);
        }
        for (const auto& row : value.view().storage_loans) {
            discover(row.backing.object);
        }
        for (const auto& row : value.view().callable_loans) {
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
        // A direct interface referent earns a finite root role. This does not
        // turn a many referent into a singleton.
        if (argument.value.view().storage_loans.size() == 1uz
            && argument.value.view().storage_loans.front().holder.empty()) {
            distinguish(argument.value.view().storage_loans.front().backing.object);
        }
        if (argument.value.view().callable_loans.size() == 1uz
            && argument.value.view().callable_loans.front().backing) {
            distinguish(argument.value.view().callable_loans.front().backing->object);
        }
    };
    for (const auto& argument : parameters) {
        visit_input(argument);
    }
    for (const auto& argument : raw_captures) {
        visit_input(argument);
    }
    // Inline fields and callable capture storage have a finite structural shape.
    // Preserve their exact roots. Sequence selections were split above;
    // indirect slice backing is deliberately not followed here.
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
        for (const auto& row : value.view().captures) {
            add(row.holder, row.target.object);
        }
        for (const auto& row : value.view().callable_loans) {
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
        inline_facts(graph_objects[inline_roots[cursor]].state.relationships);
    }
    for (auto cursor = 0uz; cursor < reachable.size(); ++cursor) {
        visit_facts(graph_objects[reachable[cursor]].state.relationships);
    }
    auto sources = std::vector<std::vector<std::size_t>>();
    auto normalized = std::flat_map<std::size_t, std::size_t>();
    auto summaries = std::flat_map<OwnershipStorageSite, std::size_t>();
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
        if (auto* rows = value.edit_existing()) {
            for (auto& row : rows->captures) {
                row.target = map_place(std::move(row.target));
            }
            for (auto& row : rows->storage_loans) {
                row.backing = map_place(std::move(row.backing));
            }
            for (auto& row : rows->callable_loans) {
                row.direct_only = false;
                if (row.backing) {
                    row.backing = map_place(std::move(*row.backing));
                }
            }
        }
        return value;
    };
    const auto map_argument = [&](OwnershipCallArgument& argument) noexcept -> void {
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
    };
    auto call_input = OwnershipCallInput {
        .body_id = target.id(),
        .parameters = std::move(parameters),
        .captures = std::move(raw_captures),
        .objects = {},
        .outlives = {},
        .accesses = {},
        .storage_readers = {}
    };
    for (auto& argument : call_input.parameters) {
        map_argument(argument);
    }
    for (auto& argument : call_input.captures) {
        map_argument(argument);
    }
    for (const auto& group : sources) {
        const auto first = group.front();
        auto object = OwnershipExternalObject {
            graph_objects[first].type,
            graph_objects[first].origin,
            {.available = true, .taken = std::nullopt, .relationships = {}, .modified = false},
            site_for(first),
            group.size() > 1uz
        };
        for (const auto source : group) {
            object.many |= many_for(source);
            object.state.available &= graph_objects[source].state.available;
            if (graph_objects[source].state.taken) {
                object.state.taken = graph_objects[source].state.taken;
            }
            merge_relationships(
                object.state.relationships,
                map_facts(graph_objects[source].state.relationships)
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
                continue;
            }
            for (const auto element : reachable) {
                if (storage_regions_overlap(edges, loan.backing, {element, {}})) {
                    auto copy = loan;
                    copy.backing = {normalized.at(element), {}};
                    copy.holder.clear();
                    // Preserve a known outer index when the retained root is
                    // an ancestor carrier, so other elements remain writable.
                    auto regions = std::vector<OwnershipPlace>();
                    auto pending = std::vector<std::size_t> {loan.backing.object};
                    auto seen = std::flat_set<std::size_t>();
                    while (!pending.empty()) {
                        const auto next = pending.back();
                        pending.pop_back();
                        if (!seen.insert(next).second) {
                            continue;
                        }
                        for (const auto& edge : edges) {
                            if (edge.element != next) {
                                continue;
                            }
                            if (edge.carrier.object == element) {
                                auto region = edge.carrier;
                                region.path.push_back(edge.index);
                                regions.push_back(map_place(std::move(region)));
                            }
                            pending.push_back(edge.carrier.object);
                        }
                    }
                    if (regions.empty()) {
                        call_input.storage_readers.push_back(std::move(copy));
                    } else {
                        for (const auto& region : regions) {
                            auto projected = copy;
                            projected.backing = region;
                            call_input.storage_readers.push_back(std::move(projected));
                        }
                    }
                }
            }
        }
    };
    protect_external(projected_readers);
    for (const auto& [holder, object] : std::views::enumerate(graph_objects)) {
        if (!normalized.contains(static_cast<std::size_t>(holder))) {
            protect_external(object.state.relationships.view().storage_loans);
        }
    }
    for (const auto& from : sources) {
        auto row = std::vector<bool>();
        for (const auto& to : sources) {
            auto valid = true;
            for (const auto source : from) {
                for (const auto destination : to) {
                    valid &=
                        outlives(actual_sources[source].object, actual_sources[destination].object);
                }
            }
            row.push_back(valid);
        }
        call_input.outlives.push_back(std::move(row));
    }
    // Keep only observable roots. Intermediate recursive ancestors become
    // transitive carrier relations, rather than ever longer object chains.
    const auto ancestors = [&](std::size_t element, auto&& visit) noexcept {
        auto pending = std::vector<std::pair<std::size_t, bool>> {{element, true}};
        auto seen = std::flat_set<std::size_t>();
        while (!pending.empty()) {
            const auto [next, direct] = pending.back();
            pending.pop_back();
            if (!seen.insert(next).second) {
                continue;
            }
            for (const auto& edge : edges) {
                if (edge.element != next) {
                    continue;
                }
                visit(edge, direct && edge.direct);
                pending.push_back({edge.carrier.object, false});
            }
        }
    };
    for (const auto element : reachable) {
        ancestors(element, [&](const OwnershipStorageEdge& edge, bool direct) noexcept {
            if (normalized.contains(edge.carrier.object)) {
                call_input.owns.push_back(
                    {map_place(edge.carrier), normalized.at(element), edge.index, direct}
                );
            }
        });
    }
    std::ranges::sort(call_input.owns);
    call_input.owns.erase(std::ranges::unique(call_input.owns).begin(), call_input.owns.end());
    const auto must_ancestor = [&](const OwnershipPlace& owner,
                                   const OwnershipPlace& referent) noexcept {
        const auto exact_prefix = [&](const OwnershipPlace& selected) noexcept {
            return owner.object == selected.object
                && owner.path.size() <= selected.path.size()
                && std::ranges::all_of(
                       selected.path,
                       [](const auto& part) noexcept { return part.has_value(); }
                )
                && std::ranges::equal(
                       owner.path,
                       std::span(selected.path).first(owner.path.size())
                );
        };
        if (exact_prefix(referent)) {
            return true;
        }
        auto pending = std::vector<std::size_t> {referent.object};
        auto seen = std::flat_set<std::size_t>();
        while (!pending.empty()) {
            const auto next = pending.back();
            pending.pop_back();
            if (!seen.insert(next).second) {
                continue;
            }
            const auto carriers = std::ranges::count_if(edges, [&](const auto& edge) noexcept {
                return edge.element == next;
            });
            if (carriers != 1) {
                continue;
            }
            for (const auto& edge : edges) {
                if (edge.element != next || !edge.direct) {
                    continue;
                }
                if (exact_prefix(edge.carrier)) {
                    return true;
                }
                // A concrete root role preserves this direct selection's
                // relative owner identity, even if the selected index is unknown.
                pending.push_back(edge.carrier.object);
            }
        }
        return false;
    };
    for (const auto& access : projected_accesses) {
        if (normalized.contains(access.place.object)) {
            auto mapped = access;
            mapped.place = map_place(std::move(mapped.place));
            call_input.accesses.push_back(std::move(mapped));
        } else {
            const auto covered = access.kind == OwnershipAccessKind::Structural
                && std::ranges::any_of(
                                     projected_accesses,
                                     [&](const OwnershipAccess& child) noexcept {
                                         return child.kind == OwnershipAccessKind::Structural
                                             && normalized.contains(child.place.object)
                                             && must_ancestor(access.place, child.place);
                                     }
                );
            if (covered) {
                continue;
            }
            // A direct selection still has its exact finite inline suffix. Only
            // a summarized descendant needs protection over the whole region.
            ancestors(
                access.place.object,
                [&](const OwnershipStorageEdge& edge, bool direct) noexcept {
                    if (normalized.contains(edge.carrier.object)) {
                        auto region = edge.carrier;
                        region.path.push_back(edge.index);
                        if (direct) {
                            region.path.append_range(access.place.path);
                        }
                        call_input.accesses.push_back(
                            {map_place(std::move(region)),
                             access.kind,
                             access.descendants || !direct}
                        );
                    }
                }
            );
            if (access.kind == OwnershipAccessKind::Stable) {
                for (const auto element : reachable) {
                    ancestors(element, [&](const OwnershipStorageEdge& edge, bool) noexcept {
                        if (overlaps(access.place, edge.carrier)) {
                            call_input.accesses.push_back(
                                {{normalized.at(element), {}}, access.kind, true}
                            );
                        }
                    });
                }
            }
        }
    }
    std::ranges::sort(call_input.accesses);
    call_input.accesses.erase(
        std::ranges::unique(call_input.accesses).begin(),
        call_input.accesses.end()
    );
    const auto restore_place = [&](OwnershipPlace place, std::size_t source, bool loan) noexcept {
        auto actual = actual_sources[source];
        const auto offset = actual.path.size();
        actual.path.append_range(place.path);
        for (auto boundary : place.indirections) {
            boundary.offset += offset;
            actual.indirections.push_back(boundary);
        }
        if (loan && !actual.indirections.empty()) {
            // A borrowed descendant protects its first selected element. This deliberately
            // broadens the region, and makes recursive returned loans finite.
            actual.path.resize(actual.indirections.front().offset + 1uz);
            actual.indirections.clear();
        }
        return actual;
    };
    const auto restore_facts = [&](const OwnershipRelationships& value) noexcept {
        auto restored = OwnershipRelationships {};
        for (const auto& row : value.view().captures) {
            for (const auto source : sources[row.target.object]) {
                auto copy = row;
                copy.target = restore_place(copy.target, source, false);
                restored.edit().captures.push_back(std::move(copy));
            }
        }
        for (const auto& row : value.view().storage_loans) {
            for (const auto source : sources[row.backing.object]) {
                auto copy = row;
                copy.backing = restore_place(copy.backing, source, true);
                restored.edit().storage_loans.push_back(std::move(copy));
            }
        }
        for (const auto& row : value.view().callable_loans) {
            if (!row.backing) {
                restored.edit().callable_loans.push_back(row);
            } else {
                for (const auto source : sources[row.backing->object]) {
                    auto copy = row;
                    *copy.backing = restore_place(*copy.backing, source, false);
                    restored.edit().callable_loans.push_back(std::move(copy));
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
    const auto answers = analysis.query(std::move(call_input));
    for (const auto [answer_index, answer] : std::views::enumerate(answers)) {
        // Move only on the final answer; no later iteration can use state.
        // NOLINTNEXTLINE(bugprone-use-after-move)
        auto returned = answer_index + 1uz == answers.size() ? std::move(state) : state;
        for (auto index = 0uz; index < sources.size(); ++index) {
            const auto& object = answer.state.objects[index];
            if (!object.modified) {
                continue;
            }
            auto restored = object;
            restored.relationships = restore_facts(object.relationships);
            for (const auto source : sources[index]) {
                const auto& actual = actual_sources[source];
                auto& destination = returned.objects[actual.object];
                if (!actual.path.empty()) {
                    // A projected write updates only that source subregion.
                    store(returned, actual, restored.relationships, origin, !many[index]);
                    continue;
                }
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
