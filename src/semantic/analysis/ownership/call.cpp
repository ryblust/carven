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
        state.objects[index] = input.state.objects[index];
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
                if (std::ranges::any_of(
                        topology.roots[capture.target.object],
                        [&](const auto root) noexcept { return root >= input.objects.size(); }
                    )) {
                    escape = OwnershipEscape {
                        "escaping closure outlives its captured owner",
                        body.region().origin,
                        capture.origin
                    };
                    return;
                }
            }
            for (const auto& loan : relationships.view().storage_loans) {
                if (std::ranges::any_of(
                        topology.roots[loan.backing.object],
                        [&](const auto root) noexcept { return root >= input.objects.size(); }
                    )) {
                    escape = OwnershipEscape {
                        "escaping view outlives its backing",
                        body.region().origin,
                        loan.origin
                    };
                    return;
                }
            }
            for (const auto& loan : relationships.view().callable_loans) {
                if (loan.backing.has_value()
                    && std::ranges::any_of(
                        topology.roots[loan.backing->object],
                        [&](const auto root) noexcept { return root >= input.objects.size(); }
                    )) {
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
        for (auto index = 0uz; index < input.objects.size(); ++index) {
            check(state.objects[index].relationships);
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
    const auto base = input.objects.size() + facts.locals.size();
    const auto publish_place = [&](OwnershipPlace place) noexcept {
        if (place.object >= base) {
            place.object -= facts.locals.size();
        }
        return place;
    };
    auto published_edges = std::vector<OwnershipStorageEdge>();
    for (const auto& edge : topology.owns) {
        // Input topology is proof context, not a change to caller ownership.
        if (edge.carrier.object < input.objects.size() && edge.element < input.objects.size()) {
            continue;
        }
        if (std::ranges::all_of(
                topology.roots[edge.element],
                [&](const auto root) noexcept { return root >= input.objects.size(); }
            )
            || std::ranges::all_of(
                topology.roots[edge.carrier.object],
                [&](const auto root) noexcept { return root >= input.objects.size(); }
            )) {
            continue;
        }
        auto copy = edge;
        copy.carrier = publish_place(std::move(copy.carrier));
        copy.element = publish_place({copy.element, {}}).object;
        published_edges.push_back(std::move(copy));
    }
    std::ranges::sort(published_edges);
    published_edges.erase(std::ranges::unique(published_edges).begin(), published_edges.end());
    for (auto& completion : result) {
        completion.state.objects.erase(
            completion.state.objects.begin() + static_cast<std::ptrdiff_t>(input.objects.size()),
            completion.state.objects.begin() + static_cast<std::ptrdiff_t>(base)
        );
        completion.value = map_relationships(completion.value, publish_place);
        normalize_relationships(completion.value);
        for (auto& object : completion.state.objects) {
            object.relationships = map_relationships(object.relationships, publish_place);
        }
    }
    auto summary = OwnershipCallSummary {
        .completions = std::move(result),
        .referents = {},
        .owns = {},
        .effects = {}
    };
    // Keep domain slots stable across reevaluations, including local-only
    // nodes. Their owns edges are unpublished and restoration leaves their
    // source lists empty, so they cannot enter caller contexts.
    for (auto index = base; index < topology.objects.size(); ++index) {
        summary.referents.push_back(topology.objects[index]);
    }
    summary.owns = std::move(published_edges);
    for (auto effect : effects) {
        if (std::ranges::all_of(topology.roots[effect.place.object], [&](const auto root) noexcept {
                return root >= input.objects.size();
            })) {
            continue;
        }
        effect.place = publish_place(std::move(effect.place));
        summary.effects.push_back(std::move(effect));
    }
    std::ranges::sort(summary.effects, {}, [](const auto& effect) static noexcept {
        return std::tuple(effect.place, effect.invalidates, effect.storage, effect.take);
    });
    summary.effects.erase(std::ranges::unique(summary.effects).begin(), summary.effects.end());
    std::ranges::sort(summary.completions, {}, [](const auto& answer) static noexcept {
        return std::pair(answer.test_stopped, answer.failure);
    });
    return OwnershipBodyResult {.answer = std::move(summary), .diagnosis = std::move(diagnosis)};
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

    synchronize_storage(state);
    const auto recursive_call = analysis.same_recursion_component(body.id(), target.id());
    auto call_input = OwnershipCallInput {target.id(), {}, {}, {}, {}, {}, {}, {}, {}, {}};
    auto restored_sources = std::vector<std::vector<OwnershipPlace>>();
    auto normalized = std::flat_map<std::size_t, std::size_t>();
    auto external_readers = storage_readers;
    if (!analysis.facts_for_body(target.id()).relation_demand.requires_context()) {
        // This transfer cannot observe relations between formal regions. Its
        // symbolic input is fixed; actual places belong only to substitution.
        call_input = analysis.symbolic_input(target);
        restored_sources.resize(call_input.objects.size());
        const auto substitute = [&](const auto& actual, const auto& symbolic) noexcept {
            for (const auto& [argument, parameter] : std::views::zip(actual, symbolic)) {
                if (!parameter.alias) {
                    continue;
                }
                auto& sources = restored_sources[parameter.alias->object];
                sources = argument.storage;
                if (sources.empty() && argument.alias) {
                    sources.push_back(*argument.alias);
                }
            }
        };
        substitute(parameters, call_input.parameters);
        substitute(raw_captures, call_input.captures);
        for (const auto& object : state.objects) {
            external_readers.append_range(object.relationships.view().storage_loans);
        }
    } else {
        // Input projection is read-only until query returns. Keep shape and current
        // flow state separate instead of deep-copying the complete retained domain.
        const auto& graph_objects = topology.objects;
        const auto& edges = topology.owns;
        const auto site_for = [&](std::size_t object) noexcept {
            return graph_objects[object].site;
        };
        const auto many_for = [&](std::size_t object) noexcept {
            return graph_objects[object].many;
        };
        auto reachable = std::vector<std::size_t>();
        auto seen = std::flat_set<std::size_t>();
        auto distinguished = std::flat_set<std::size_t>();
        auto role_sources = std::map<OwnershipInterfaceRole, std::flat_set<std::size_t>>();
        auto memberships = std::flat_map<std::size_t, std::vector<OwnershipInterfaceRole>>();
        const auto add_role = [&](std::size_t binding,
                                  OwnershipInterfaceUse use,
                                  OwnershipProjectionPath holder,
                                  const OwnershipPlace& place) noexcept {
            const auto role = OwnershipInterfaceRole {binding, use, std::move(holder), place.path};
            role_sources[role].insert(place.object);
            if (state.objects[place.object].relationships.empty()) {
                memberships[place.object].push_back(role);
            }
        };
        const auto discover = [&](std::size_t object) noexcept {
            if (seen.insert(object).second) {
                reachable.push_back(object);
            }
        };
        const auto distinguish = [&](std::size_t object) noexcept {
            discover(object);
            // Current interface roles resist site-only merging. Possible aliases
            // remain direct region relations; the many bit forbids strong updates.
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
        const auto visit_input = [&](std::size_t binding,
                                     const OwnershipCallArgument& argument) noexcept {
            if (argument.alias) {
                distinguish(argument.alias->object);
                add_role(binding, OwnershipInterfaceUse::Storage, {}, *argument.alias);
            }
            if (argument.capture_holder) {
                distinguish(argument.capture_holder->object);
                add_role(
                    binding,
                    OwnershipInterfaceUse::CaptureHolder,
                    {},
                    *argument.capture_holder
                );
            }
            for (const auto& place : argument.storage) {
                discover(place.object);
                add_role(binding, OwnershipInterfaceUse::Storage, {}, place);
            }
            if (argument.storage.size() == 1uz) {
                distinguish(argument.storage.front().object);
            }
            visit_facts(argument.value);
            for (const auto& loan : argument.value.view().storage_loans) {
                add_role(binding, OwnershipInterfaceUse::StorageLoan, loan.holder, loan.backing);
            }
            for (const auto& loan : argument.value.view().callable_loans) {
                if (loan.backing) {
                    add_role(
                        binding,
                        OwnershipInterfaceUse::CallableLoan,
                        loan.holder,
                        *loan.backing
                    );
                }
            }
            for (const auto& capture : argument.value.view().captures) {
                add_role(binding, OwnershipInterfaceUse::Capture, capture.holder, capture.target);
            }
            // A single interface referent is a traversal boundary. This does not
            // turn a many referent into a singleton.
            if (argument.value.view().storage_loans.size() == 1uz
                && argument.value.view().storage_loans.front().holder.empty()) {
                const auto& loan = argument.value.view().storage_loans.front();
                distinguish(loan.backing.object);
            }
            if (argument.value.view().callable_loans.size() == 1uz
                && argument.value.view().callable_loans.front().backing) {
                const auto& loan = argument.value.view().callable_loans.front();
                distinguish(loan.backing->object);
            }
        };
        for (const auto [index, argument] : std::views::enumerate(parameters)) {
            visit_input(static_cast<std::size_t>(index), argument);
        }
        for (const auto [index, argument] : std::views::enumerate(raw_captures)) {
            visit_input(parameters.size() + static_cast<std::size_t>(index), argument);
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
            auto slots = std::flat_map<OwnershipProjectionPath, std::optional<OwnershipPlace>>();
            const auto add = [&](const OwnershipProjectionPath& holder,
                                 std::optional<OwnershipPlace> place) noexcept {
                if (!std::ranges::all_of(holder, [](const auto& part) static noexcept {
                        return part.has_value();
                    })) {
                    return;
                }
                const auto [found, inserted] = slots.emplace(holder, std::move(place));
                if (!inserted) {
                    found->second.reset();
                }
            };
            for (const auto& row : value.view().captures) {
                add(row.holder, row.target);
            }
            for (const auto& row : value.view().callable_loans) {
                add(row.holder, row.backing);
            }
            for (const auto& [holder, place] : slots) {
                if (place) {
                    inline_target(place->object);
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
        // Keep owns paths between interface roles, including one possible-alias
        // relay at either endpoint. Alias possibilities never become traversal edges.
        // Added connectors are selected owning values; Sequence element admission
        // excludes contained loan and callable relationships.
        // Other caller readers and selections use the instantiated effects below.
        const auto interface = reachable;
        auto alias_seeds = std::flat_map<std::size_t, std::vector<std::size_t>>();
        for (const auto role : interface) {
            auto roots = std::vector<std::size_t> {role};
            for (auto object = 0uz; object < graph_objects.size(); ++object) {
                if (object != role && storage_aliases({role, {}}, {object, {}})) {
                    roots.push_back(object);
                }
            }
            alias_seeds.emplace(role, std::move(roots));
        }
        // These indices borrow the caller's immutable owns graph for this projection.
        auto outgoing = std::vector<std::vector<const OwnershipStorageEdge*>>(graph_objects.size());
        auto incoming = std::vector<std::vector<const OwnershipStorageEdge*>>(graph_objects.size());
        for (const auto& edge : edges) {
            outgoing[edge.carrier.object].push_back(&edge);
            incoming[edge.element].push_back(&edge);
        }
        const auto graph_reachable = [&](const auto& roots, bool reverse) noexcept {
            const auto& adjacency = reverse ? incoming : outgoing;
            auto visited = std::flat_set<std::size_t>(roots.begin(), roots.end());
            auto pending = roots;
            for (auto cursor = 0uz; cursor < pending.size(); ++cursor) {
                for (const auto* edge : adjacency[pending[cursor]]) {
                    const auto to = reverse ? edge->carrier.object : edge->element;
                    if (visited.insert(to).second) {
                        pending.push_back(to);
                    }
                }
            }
            return visited;
        };
        auto reverse_reach = std::flat_map<std::size_t, std::flat_set<std::size_t>>();
        for (const auto role : interface) {
            reverse_reach.emplace(role, graph_reachable(alias_seeds.at(role), true));
        }
        for (const auto from : interface) {
            const auto forward = graph_reachable(alias_seeds.at(from), false);
            for (const auto to : interface) {
                if (from == to) {
                    continue;
                }
                const auto& reverse = reverse_reach.at(to);
                for (const auto object : forward) {
                    if (reverse.contains(object)) {
                        discover(object);
                    }
                }
            }
        }
        // Role source sets are fixed by the current formal/capture interface.
        // Candidate count and caller allocation histories do not name regions.
        using BoundarySelection = decltype(OwnershipBoundaryFamily::selection);
        auto descriptors = std::flat_map<std::size_t, OwnershipRegionDescriptor>();
        if (recursive_call) {
            for (auto&& [source, roles] : memberships) {
                std::ranges::sort(roles);
                roles.erase(std::ranges::unique(roles).begin(), roles.end());
                descriptors[source].memberships = std::move(roles);
            }
            const auto collect_families = [&](OwnershipBoundaryDirection direction,
                                              const OwnershipInterfaceRole& role,
                                              const auto& roots) noexcept {
                const auto forward = direction == OwnershipBoundaryDirection::Forward;
                const auto& adjacency = forward ? outgoing : incoming;
                const auto add_family = [&](std::size_t source,
                                            BoundarySelection selection) noexcept {
                    if (seen.contains(source) && state.objects[source].relationships.empty()) {
                        descriptors[source].boundaries.push_back(
                            {direction, role, std::move(selection)}
                        );
                    }
                };
                const auto next_object = [&](const OwnershipStorageEdge& edge) noexcept {
                    return forward ? edge.element : edge.carrier.object;
                };
                auto seeds = std::flat_map<BoundarySelection, std::flat_set<std::size_t>>();
                for (const auto root : roots) {
                    add_family(root, std::nullopt);
                    for (const auto* boundary : adjacency[root]) {
                        const auto selection =
                            BoundarySelection(std::pair(boundary->carrier.path, boundary->index));
                        seeds[selection].insert(next_object(*boundary));
                    }
                }
                // Each role/selector fact enters an object once, regardless of
                // how many source candidates supply that same boundary fact.
                for (const auto& [selection, origins] : seeds) {
                    auto visited = origins;
                    auto pending = std::vector<std::size_t>(origins.begin(), origins.end());
                    for (auto cursor = 0uz; cursor < pending.size(); ++cursor) {
                        const auto source = pending[cursor];
                        add_family(source, selection);
                        if (distinguished.contains(source)) {
                            continue;
                        }
                        for (const auto* edge : adjacency[source]) {
                            const auto next = next_object(*edge);
                            if (visited.insert(next).second) {
                                pending.push_back(next);
                            }
                        }
                    }
                }
            };
            for (const auto& [role, candidates] : role_sources) {
                auto roots = std::flat_set<std::size_t>();
                for (const auto source : candidates) {
                    roots.insert_range(alias_seeds.at(source));
                }
                collect_families(OwnershipBoundaryDirection::Forward, role, roots);
                collect_families(OwnershipBoundaryDirection::Reverse, role, roots);
            }
            for (auto&& [source, descriptor] : descriptors) {
                static_cast<void>(source);
                std::ranges::sort(descriptor.boundaries);
            }
        }
        // Normalize all labeled regions by their complete semantic descriptor.
        // Allocation histories remain a separate identity for unlabeled objects.
        std::ranges::stable_sort(reachable, [&](const auto left, const auto right) noexcept {
            const auto a = descriptors.find(left);
            const auto b = descriptors.find(right);
            if ((a == descriptors.end()) != (b == descriptors.end())) {
                return a != descriptors.end();
            }
            if (a == descriptors.end()) {
                return false;
            }
            return std::pair(a->second, graph_objects[left].type)
                < std::pair(b->second, graph_objects[right].type);
        });
        auto sources = std::vector<std::vector<std::size_t>>();
        auto source_sites = std::vector<OwnershipStorageSite>();
        auto summaries = std::flat_map<std::pair<OwnershipStorageSite, TypeID>, std::size_t>();
        auto regions = std::flat_map<std::pair<OwnershipRegionDescriptor, TypeID>, std::size_t>();
        for (const auto source : reachable) {
            auto index = sources.size();
            auto site = site_for(source);
            const auto descriptor = descriptors.find(source);
            if (descriptor != descriptors.end()) {
                const auto [found, inserted] = regions.emplace(
                    std::pair(descriptor->second, graph_objects[source].type),
                    index
                );
                index = found->second;
                site = analysis.region_site(
                    target.id(),
                    graph_objects[source].type,
                    descriptor->second
                );
            } else {
                if (!recursive_call && memberships.contains(source)) {
                    site = {target.id(), index, OwnershipStorageSiteKind::Input};
                }
                // Non-owning contained backing/capture histories still retain
                // allocation sites in the SCC instead of acquiring new formal roles.
                if (!distinguished.contains(source)
                    && recursive_call
                    && analysis.same_recursion_component(site.body, target.id())) {
                    const auto [found, inserted] =
                        summaries.emplace(std::pair(site, graph_objects[source].type), index);
                    index = found->second;
                }
            }
            if (index == sources.size()) {
                sources.emplace_back();
                source_sites.push_back(site);
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
        call_input = OwnershipCallInput {
            .body_id = target.id(),
            .parameters = std::move(parameters),
            .captures = std::move(raw_captures),
            .objects = {},
            .state = {},
            .outlives = {},
            .accesses = {},
            .storage_readers = {},
            .owns = {},
            .possible_aliases = {}
        };
        for (auto& argument : call_input.parameters) {
            map_argument(argument);
        }
        for (auto& argument : call_input.captures) {
            map_argument(argument);
        }
        // Region sites retain the complete interned interface descriptor. Caller
        // identity stays in restoration; contained histories keep their own sites.
        for (const auto [index, group] : std::views::enumerate(sources)) {
            const auto first = group.front();
            auto object = OwnershipStorageObject {
                graph_objects[first].type,
                graph_objects[first].origin,
                source_sites[static_cast<std::size_t>(index)],
                group.size() > 1uz,
                false
            };
            auto incoming_state = OwnershipObjectState {true, std::nullopt, {}, false};
            for (const auto source : group) {
                object.many |= many_for(source);
                incoming_state.available &= state.objects[source].available;
                if (state.objects[source].taken) {
                    incoming_state.taken = state.objects[source].taken;
                }
                merge_relationships(
                    incoming_state.relationships,
                    map_facts(state.objects[source].relationships)
                );
            }
            call_input.objects.push_back(std::move(object));
            call_input.state.objects.push_back(std::move(incoming_state));
        }
        for (auto left = 0uz; left < sources.size(); ++left) {
            for (auto right = left + 1uz; right < sources.size(); ++right) {
                if (std::ranges::any_of(sources[left], [&](const auto a) noexcept {
                        return std::ranges::any_of(sources[right], [&](const auto b) noexcept {
                            return storage_aliases({a, {}}, {b, {}});
                        });
                    })) {
                    call_input.possible_aliases.emplace_back(left, right);
                }
            }
        }
        // These loans belong to caller holders that the callee cannot overwrite.
        // Keep their concrete graph identities for effect checks; projecting them to
        // a widened ancestor would erase the terminal field distinction.
        for (const auto& [holder, object] : std::views::enumerate(state.objects)) {
            if (!normalized.contains(static_cast<std::size_t>(holder))) {
                external_readers.append_range(object.relationships.view().storage_loans);
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
        for (const auto& edge : edges) {
            if (normalized.contains(edge.element) && normalized.contains(edge.carrier.object)) {
                call_input.owns.push_back(
                    {map_place(edge.carrier), normalized.at(edge.element), edge.index}
                );
            }
        }
        std::ranges::sort(call_input.owns);
        call_input.owns.erase(std::ranges::unique(call_input.owns).begin(), call_input.owns.end());
        for (const auto& group : sources) {
            auto places = std::vector<OwnershipPlace>();
            for (const auto object : group) {
                places.push_back({object, {}});
            }
            restored_sources.push_back(std::move(places));
        }
    }
    normalize_storage_loans(external_readers);
    const auto input_objects = restored_sources.size();
    const auto restore_place = [](OwnershipPlace place, OwnershipPlace source) noexcept {
        source.path.append_range(place.path);
        return source;
    };
    const auto restore_facts = [&](const OwnershipRelationships& value) noexcept {
        auto restored = OwnershipRelationships {};
        for (const auto& row : value.view().captures) {
            for (const auto& source : restored_sources[row.target.object]) {
                auto copy = row;
                copy.target = restore_place(copy.target, source);
                restored.edit().captures.push_back(std::move(copy));
            }
        }
        for (const auto& row : value.view().storage_loans) {
            for (const auto& source : restored_sources[row.backing.object]) {
                auto copy = row;
                copy.backing = restore_place(copy.backing, source);
                restored.edit().storage_loans.push_back(std::move(copy));
            }
        }
        for (const auto& row : value.view().callable_loans) {
            if (!row.backing) {
                restored.edit().callable_loans.push_back(row);
            } else {
                for (const auto& source : restored_sources[row.backing->object]) {
                    auto copy = row;
                    *copy.backing = restore_place(*copy.backing, source);
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
    const auto& answers = analysis.query(std::move(call_input), recursive_call);
    restored_sources.resize(input_objects + answers.referents.size());
    // New callee descendants retain their referent identity. Restore the
    // reachable graph once; never flatten owns edges back into a long path.
    const auto previous_edges = topology.owns.size();
    auto remaining = answers.owns;
    for (;;) {
        auto progress = false;
        for (auto edge = remaining.begin(); edge != remaining.end();) {
            const auto parents = restored_sources[edge->carrier.object];
            if (parents.empty()) {
                ++edge;
                continue;
            }
            auto& destinations = restored_sources[edge->element];
            if (destinations.empty()) {
                const auto& node = answers.referents[edge->element - input_objects];
                for (const auto& parent : parents) {
                    const auto carrier = restore_place(edge->carrier, parent);
                    const auto selected = select_owned_storage(
                        carrier,
                        node.type,
                        edge->index,
                        node.origin,
                        state,
                        node.many,
                        node.feedback || analysis.same_recursion_component(body.id(), target.id())
                    );
                    for (const auto& place : selected) {
                        destinations.push_back(place);
                    }
                }
                // Restoration maps possible referents, not paths to them.
                // Shared feedback can select the same node from many parents.
                std::ranges::sort(destinations);
                destinations.erase(std::ranges::unique(destinations).begin(), destinations.end());
            } else {
                for (const auto& parent : parents) {
                    const auto carrier = restore_place(edge->carrier, parent);
                    for (const auto& destination : destinations) {
                        topology.owns.push_back({carrier, destination.object, edge->index});
                    }
                }
            }
            edge = remaining.erase(edge);
            progress = true;
        }
        if (!progress) {
            break;
        }
    }
    std::ranges::sort(topology.owns);
    topology.owns.erase(std::ranges::unique(topology.owns).begin(), topology.owns.end());
    if (topology.owns.size() != previous_edges) {
        ++topology.revision;
    }
    propagate_storage_facts();
    synchronize_storage(state);
    // Instantiate the same checked effects in the caller graph. Input
    // holders remain part of the callee's stateful proof; only independent
    // caller readers and active selections are checked here.
    for (const auto& effect : answers.effects) {
        for (const auto& source : restored_sources[effect.place.object]) {
            const auto place = restore_place(effect.place, source);
            if (effect.take) {
                const auto mapped = OwnershipWriteEffect {place, true, true, effect.origin, true};
                if (!std::ranges::contains(effects, mapped)) {
                    effects.push_back(mapped);
                }
                if (diagnosing) {
                    auto external_state = state;
                    for (const auto& [object, role] : normalized) {
                        static_cast<void>(role);
                        external_state.objects[object].relationships = {};
                    }
                    if (const auto conflict = take_conflict(external_state, place)) {
                        diagnose(
                            conflict->code,
                            std::string(conflict->message),
                            effect.origin,
                            conflict->related
                        );
                    }
                }
            } else if (!effect.storage) {
                write_access(place, effect.origin, effect.invalidates);
            } else {
                const auto mapped =
                    OwnershipWriteEffect {place, effect.invalidates, true, effect.origin, false};
                if (!std::ranges::contains(effects, mapped)) {
                    effects.push_back(mapped);
                }
                if (diagnosing) {
                    for (const auto& loan : external_readers) {
                        if (storage_overlaps(loan.backing, place)) {
                            diagnose(
                                DiagnosticCode::AccessBorrowConflict,
                                "operation conflicts with a live borrowed view",
                                effect.origin,
                                loan.origin
                            );
                        }
                    }
                }
            }
        }
    }
    for (const auto [answer_index, answer] : std::views::enumerate(answers.completions)) {
        // Move only on the final answer; no later iteration can use state.
        // NOLINTNEXTLINE(bugprone-use-after-move)
        auto returned = answer_index + 1uz == answers.completions.size() ? std::move(state) : state;
        for (auto index = 0uz; index < restored_sources.size(); ++index) {
            const auto& object = answer.state.objects[index];
            if (!object.modified) {
                continue;
            }
            auto restored = object;
            restored.relationships = restore_facts(object.relationships);
            const auto& destinations = restored_sources[index];
            for (const auto& source : destinations) {
                auto& destination = returned.objects[source.object];
                if (destinations.size() > 1uz
                    || topology.objects[source.object].many
                    || (index < many.size() && many[index])) {
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
