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
        merge_storage_aliases(state);
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
    // Summary nodes represent possible aliases, not exact identities. Keep
    // every effect alternative in that finite alias class. This only widens:
    // terminal suffix, checked object type, and access obligation stay intact.
    for (auto cursor = 0uz; cursor < effects.size(); ++cursor) {
        const auto effect = effects[cursor];
        if (std::ranges::all_of(topology.roots[effect.place.object], [&](const auto root) noexcept {
                return root >= input.objects.size();
            })) {
            continue;
        }
        for (const auto& place : storage_alias_alternatives(effect.place)) {
            if (std::ranges::all_of(topology.roots[place.object], [&](const auto root) noexcept {
                    return root >= input.objects.size();
                })) {
                continue;
            }
            auto alternative = effect;
            alternative.place = place;
            if (!std::ranges::contains(effects, alternative)) {
                effects.push_back(std::move(alternative));
            }
        }
    }

    for (auto& completion : result) {
        merge_storage_aliases(completion.state);
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
        auto node = topology.objects[index];
        node.state = {};
        summary.referents.push_back(std::move(node));
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
    auto direct_formal = std::flat_set<std::size_t>();
    const auto discover = [&](std::size_t object) noexcept {
        if (seen.insert(object).second) {
            reachable.push_back(object);
        }
    };
    const auto distinguish = [&](std::size_t object) noexcept {
        discover(object);
        // Current interface roles resist site-only merging. Actual possible
        // aliases still share the conservative state partition.
        // The many bit still forbids strong updates.
        distinguished.insert(object);
        direct_formal.insert(object);
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
            direct_formal.insert(place.object);
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
        inline_facts(state.objects[inline_roots[cursor]].relationships);
    }
    for (auto cursor = 0uz; cursor < reachable.size(); ++cursor) {
        visit_facts(state.objects[reachable[cursor]].relationships);
    }
    // The interface consists of referenced parameter/capture roles and value
    // relationships, plus paths connecting distinct roles. Caller ancestry
    // outside that interface cannot change the function's transfer. Its active
    // loans and selections are checked against instantiated write effects below.
    const auto interface = reachable;
    const auto graph_reachable = [&](std::size_t seed, bool reverse) noexcept {
        auto visited = std::flat_set<std::size_t> {seed};
        auto pending = std::vector<std::size_t> {seed};
        for (auto cursor = 0uz; cursor < pending.size(); ++cursor) {
            for (const auto& edge : edges) {
                const auto from = reverse ? edge.element : edge.carrier.object;
                const auto to = reverse ? edge.carrier.object : edge.element;
                if (from == pending[cursor] && visited.insert(to).second) {
                    pending.push_back(to);
                }
            }
        }
        return visited;
    };
    for (const auto from : interface) {
        const auto forward = graph_reachable(from, false);
        for (const auto to : interface) {
            if (from == to || !forward.contains(to)) {
                continue;
            }
            const auto reverse = graph_reachable(to, true);
            for (const auto object : forward) {
                if (reverse.contains(object)) {
                    discover(object);
                }
            }
        }
    }
    using ConnectorFamily =
        std::tuple<std::size_t, OwnershipProjectionPath, std::optional<std::uint64_t>>;
    using ConnectorFamilies = std::vector<ConnectorFamily>;
    auto connector_families = std::flat_map<std::size_t, ConnectorFamilies>();
    const auto recursive_call = analysis.recursive_storage_site({body.id(), 0, false}, target.id());
    // One shared alias partition governs anchor families and normalized state.
    // Definite formal/inline roles have a stable semantic discovery prefix;
    // transitive backing histories remain facts and traversal boundaries.
    auto alias_roles = std::flat_map<std::size_t, std::size_t>();
    auto alias_parent = std::vector<std::size_t>();
    for (const auto source : reachable) {
        alias_roles.emplace(source, alias_parent.size());
        alias_parent.push_back(alias_parent.size());
    }
    const auto representative = [&](std::size_t index) noexcept {
        while (alias_parent[index] != index) {
            index = alias_parent[index];
        }
        return index;
    };
    for (const auto [index, source] : std::views::enumerate(reachable)) {
        for (auto previous = 0uz; previous < static_cast<std::size_t>(index); ++previous) {
            if (graph_objects[reachable[previous]].type == graph_objects[source].type
                && storage_aliases({reachable[previous], {}}, {source, {}})) {
                auto a = representative(previous);
                auto b = representative(static_cast<std::size_t>(index));
                if (a > b) {
                    std::swap(a, b);
                }
                alias_parent[b] = a;
            }
        }
    }
    auto owning_roles = std::vector<std::size_t>();
    for (const auto source : interface) {
        if (distinguished.contains(source)
            && analysis.contents(graph_objects[source].type).contains_storage_owner) {
            owning_roles.push_back(source);
        }
    }
    if (recursive_call) {
        for (const auto [ordinal, anchor] : std::views::enumerate(owning_roles)) {
            auto role = static_cast<std::size_t>(ordinal);
            for (auto previous = 0uz; previous < role; ++previous) {
                if (representative(alias_roles.at(owning_roles[previous]))
                    == representative(alias_roles.at(anchor))) {
                    role = previous;
                    break;
                }
            }
            for (const auto& first : edges) {
                if (first.carrier.object != anchor) {
                    continue;
                }
                auto visited = std::flat_set<std::size_t> {first.element};
                auto pending = std::vector<std::size_t> {first.element};
                for (auto cursor = 0uz; cursor < pending.size(); ++cursor) {
                    const auto source = pending[cursor];
                    if (std::ranges::contains(interface, source)) {
                        continue;
                    }
                    if (seen.contains(source)) {
                        connector_families[source].push_back(
                            {role, first.carrier.path, first.index}
                        );
                    }
                    for (const auto& next : edges) {
                        if (next.carrier.object == source && visited.insert(next.element).second) {
                            pending.push_back(next.element);
                        }
                    }
                }
            }
        }
        for (auto&& [source, families] : connector_families) {
            static_cast<void>(source);
            std::ranges::sort(families);
            families.erase(std::ranges::unique(families).begin(), families.end());
        }
        // Canonical connector order is derived from semantic families and type;
        // transient caller object numbers cannot become summary identity.
        std::ranges::stable_sort(reachable, [&](const auto left, const auto right) noexcept {
            const auto a = connector_families.find(left);
            const auto b = connector_families.find(right);
            if ((a == connector_families.end()) != (b == connector_families.end())) {
                return a == connector_families.end();
            }
            if (a == connector_families.end()) {
                return false;
            }
            return std::pair(a->second, graph_objects[left].type)
                < std::pair(b->second, graph_objects[right].type);
        });
    }
    auto sources = std::vector<std::vector<std::size_t>>();
    auto source_sites = std::vector<OwnershipStorageSite>();
    auto normalized = std::flat_map<std::size_t, std::size_t>();
    auto summaries = std::flat_map<std::pair<OwnershipStorageSite, TypeID>, std::size_t>();
    auto owned_summaries = std::flat_map<std::pair<ConnectorFamilies, TypeID>, std::size_t>();
    auto alias_groups = std::flat_map<std::size_t, std::size_t>();
    for (const auto source : reachable) {
        auto index = sources.size();
        const auto alias = representative(alias_roles.at(source));
        if (const auto found = alias_groups.find(alias); found != alias_groups.end()) {
            index = found->second;
        }
        auto site = site_for(source);
        const auto families = connector_families.find(source);
        if (index == sources.size()
            && families != connector_families.end()
            && !distinguished.contains(source)) {
            const auto [found, inserted] = owned_summaries.emplace(
                std::pair(families->second, graph_objects[source].type),
                index
            );
            index = found->second;
            site = {target.id(), index, true};
        } else {
            // Non-owning contained backing/capture histories still retain
            // allocation sites in the SCC instead of acquiring new formal roles.
            auto connector_site = site;
            connector_site.input = false;
            if (index == sources.size()
                && !distinguished.contains(source)
                && recursive_call
                && analysis.recursive_storage_site(connector_site, target.id())) {
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
        alias_groups.emplace(alias, index);
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
    // A direct owned selection with no contained relationships needs only its
    // formal role. Local allocation families remain observable through retained
    // slice/closure histories, even when a captured scalar itself has no rows.
    for (const auto [index, group] : std::views::enumerate(sources)) {
        const auto first = group.front();
        auto object = OwnershipExternalObject {
            graph_objects[first].type,
            graph_objects[first].origin,
            {.available = true, .taken = std::nullopt, .relationships = {}, .modified = false},
            std::ranges::any_of(
                group,
                [&](const auto source) noexcept { return direct_formal.contains(source); }
            )
                    && std::ranges::all_of(
                        group,
                        [&](const auto source) noexcept {
                            return state.objects[source].relationships.empty()
                                && (!analysis.recursive_storage_site(site_for(source), target.id())
                                    || site_for(source).element_selection.has_value());
                        }
                    )
                ? OwnershipStorageSite {target.id(), static_cast<std::size_t>(index), true}
                : source_sites[static_cast<std::size_t>(index)],
            group.size() > 1uz,
            false
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
    // These loans belong to caller holders that the callee cannot overwrite.
    // Keep their concrete graph identities for effect checks; projecting them to
    // a widened ancestor would erase the terminal field distinction.
    auto external_readers = storage_readers;
    for (const auto& [holder, object] : std::views::enumerate(state.objects)) {
        if (!normalized.contains(static_cast<std::size_t>(holder))) {
            external_readers.append_range(object.relationships.view().storage_loans);
        }
    }
    normalize_storage_loans(external_readers);
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
    auto restored_sources = sources;
    const auto restore_place = [&](OwnershipPlace place, std::size_t source) noexcept {
        place.object = source;
        return place;
    };
    const auto restore_facts = [&](const OwnershipRelationships& value) noexcept {
        auto restored = OwnershipRelationships {};
        for (const auto& row : value.view().captures) {
            for (const auto source : restored_sources[row.target.object]) {
                auto copy = row;
                copy.target = restore_place(copy.target, source);
                restored.edit().captures.push_back(std::move(copy));
            }
        }
        for (const auto& row : value.view().storage_loans) {
            for (const auto source : restored_sources[row.backing.object]) {
                auto copy = row;
                copy.backing = restore_place(copy.backing, source);
                restored.edit().storage_loans.push_back(std::move(copy));
            }
        }
        for (const auto& row : value.view().callable_loans) {
            if (!row.backing) {
                restored.edit().callable_loans.push_back(row);
            } else {
                for (const auto source : restored_sources[row.backing->object]) {
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
    const auto input_edges = call_input.owns;
    const auto& answers = analysis.query(std::move(call_input));
    restored_sources = sources;
    restored_sources.resize(sources.size() + answers.referents.size());
    // New callee descendants retain their referent identity. Restore the
    // reachable graph once; never flatten owns edges back into a long path.
    const auto previous_edges = topology.owns.size();
    auto remaining = answers.owns;
    // Existing input edges already have concrete caller associations. A
    // grouped input describes alternatives; rebuilding its edges as a
    // cross product would invent aliases between independent owners.
    std::erase_if(remaining, [&](const auto& edge) noexcept {
        return std::ranges::binary_search(input_edges, edge);
    });
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
                const auto& node = answers.referents[edge->element - sources.size()];
                for (const auto parent : parents) {
                    auto carrier = edge->carrier;
                    carrier.object = parent;
                    const auto selected = select_owned_storage(
                        carrier,
                        node.type,
                        edge->index,
                        node.origin,
                        state,
                        node.many,
                        node.feedback
                            || analysis.recursive_storage_site({body.id(), 0uz, false}, target.id())
                    );
                    for (const auto& place : selected) {
                        destinations.push_back(place.object);
                    }
                }
            } else {
                for (const auto parent : parents) {
                    auto carrier = edge->carrier;
                    carrier.object = parent;
                    for (const auto destination : destinations) {
                        topology.owns.push_back({carrier, destination, edge->index});
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
        for (const auto source : restored_sources[effect.place.object]) {
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
            auto destinations = std::vector<OwnershipPlace>();
            for (const auto source : restored_sources[index]) {
                destinations.append_range(storage_alias_alternatives({source, {}}));
            }
            std::ranges::sort(destinations);
            destinations.erase(std::ranges::unique(destinations).begin(), destinations.end());
            for (const auto& place : destinations) {
                const auto source = place.object;
                auto& destination = returned.objects[source];
                if (destinations.size() > 1uz
                    || topology.objects[source].many
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
        merge_storage_aliases(returned);
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
