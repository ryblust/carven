module carven:semantic.analysis.ownership.topology.impl;

import :semantic.analysis.ownership.context;
import std;

auto OwnershipBodyAnalyzer::storage_regions(const OwnershipPlace& place) const noexcept
    -> const std::vector<OwnershipRegionProjection>& {
    if (cached_region_revision != topology.revision) {
        region_cache.clear();
        cached_region_revision = topology.revision;
    }
    const auto [found, inserted] = region_cache.try_emplace(place);
    if (inserted) {
        found->second = project_storage_region(topology.owns, place);
    }
    return found->second;
}

auto OwnershipBodyAnalyzer::propagate_storage_facts() noexcept -> void {
    if (topology.propagated_revision == topology.revision) {
        return;
    }
    auto changed = true;
    while (changed) {
        changed = false;
        for (const auto& edge : topology.owns) {
            auto& roots = topology.roots[edge.element];
            const auto previous = roots.size();
            if (edge.element != edge.carrier.object) {
                roots.append_range(topology.roots[edge.carrier.object]);
            }
            std::ranges::sort(roots);
            roots.erase(std::ranges::unique(roots).begin(), roots.end());
            if (roots.size() != previous) {
                ++topology.revision;
                changed = true;
            }
            if (topology.objects[edge.carrier.object].many
                && !topology.objects[edge.element].many) {
                topology.objects[edge.element].many = true;
                ++topology.revision;
                changed = true;
            }
        }
    }
    topology.propagated_revision = topology.revision;
}

auto OwnershipBodyAnalyzer::storage_available(
    const OwnershipState& state,
    std::size_t object
) const noexcept -> bool {
    return std::ranges::all_of(topology.roots[object], [&](const auto root) noexcept {
        return state.objects[root].available;
    });
}

auto OwnershipBodyAnalyzer::synchronize_storage(OwnershipState& state) const noexcept -> void {
    while (state.objects.size() < topology.objects.size()) {
        const auto available = storage_available(state, state.objects.size());
        state.objects.push_back({available, std::nullopt, {}, false});
    }
    // A retained query domain can contain descendants discovered by an earlier
    // evaluation before this evaluation initializes their local carrier. Checked
    // elements cannot be taken; their availability follows that current carrier.
    for (auto object = input.objects.size() + facts.locals.size(); object < state.objects.size();
         ++object) {
        state.objects[object].available = storage_available(state, object);
    }
}

auto OwnershipBodyAnalyzer::map_relationships(
    const OwnershipRelationships& value,
    FunctionRef<OwnershipPlace(OwnershipPlace) noexcept> map
) const noexcept -> OwnershipRelationships {
    auto result = value;
    if (auto* rows = result.edit_existing()) {
        for (auto& row : rows->captures) {
            row.target = map(std::move(row.target));
        }
        for (auto& row : rows->storage_loans) {
            row.backing = map(std::move(row.backing));
        }
        for (auto& row : rows->callable_loans) {
            if (row.backing) {
                *row.backing = map(std::move(*row.backing));
            }
        }
    }
    normalize_relationships(result);
    return result;
}

auto OwnershipBodyAnalyzer::select_owned_storage(
    const OwnershipPlace& carrier,
    TypeID element,
    std::optional<std::uint64_t> index,
    ProgramOriginID selection,
    OwnershipState& state,
    bool summarized
) noexcept -> std::vector<OwnershipPlace> {
    synchronize_storage(state);
    auto known = std::vector<OwnershipPlace>();
    for (const auto& edge : topology.owns) {
        if (edge.carrier != carrier
            || edge.index != index
            || topology.objects[edge.element].type != element) {
            continue;
        }
        const auto object = edge.element;
        if ((summarized || topology.objects[carrier.object].many)
            && !topology.objects[object].many) {
            topology.objects[object].many = true;
            ++topology.revision;
        }
        known.push_back({object, {}});
    }
    auto anchor = topology.anchors[carrier.object];
    if (carrier.object < input.objects.size() + facts.locals.size()) {
        anchor = carrier;
        anchor.path.push_back(index);
    }
    const auto key = OwnershipStorageFeedbackKey {
        anchor,
        feedback_origin.value_or(selection),
        selection,
        element,
        carrier.path,
        index
    };
    const auto feedback = feedback_origin.has_value() || summarized;
    if (!known.empty()) {
        if (feedback) {
            const auto [found, inserted] = topology.feedback.emplace(key, known.front().object);
            if (!inserted && !topology.objects[found->second].many) {
                topology.objects[found->second].many = true;
                ++topology.revision;
            }
            if (!inserted && !std::ranges::contains(known, OwnershipPlace {found->second, {}})) {
                known.push_back({found->second, {}});
                topology.owns.push_back({carrier, found->second, index});
                ++topology.revision;
            }
        }
        propagate_storage_facts();
        synchronize_storage(state);
        std::ranges::sort(known);
        known.erase(std::ranges::unique(known).begin(), known.end());
        return known;
    }
    auto object = topology.objects.size();
    if (feedback) {
        const auto [found, inserted] = topology.feedback.emplace(key, object);
        object = found->second;
        if (!inserted && !topology.objects[object].many) {
            topology.objects[object].many = true;
            ++topology.revision;
        }
    }
    if (object == topology.objects.size()) {
        const auto uncertain =
            !index || std::ranges::any_of(carrier.path, [](const auto& part) static noexcept {
                return !part;
            });
        topology.objects.push_back(
            {element,
             selection,
             {},
             {body.id(), selection.index(), false, selection},
             summarized || uncertain || topology.objects[carrier.object].many}
        );
        topology.roots.push_back(topology.roots[carrier.object]);
        topology.anchors.push_back(std::move(anchor));
    }
    topology.owns.push_back({carrier, object, index});
    ++topology.revision;
    std::ranges::sort(topology.owns);
    topology.owns.erase(std::ranges::unique(topology.owns).begin(), topology.owns.end());
    propagate_storage_facts();
    synchronize_storage(state);
    return {{object, {}}};
}

auto OwnershipBodyAnalyzer::select_element_storage(
    TypeID sequence,
    std::span<const OwnershipPlace> storage,
    const OwnershipRelationships& relationships,
    std::optional<std::uint64_t> index,
    ProgramOriginID selection,
    OwnershipState& state
) noexcept -> std::vector<OwnershipPlace> {
    auto result = std::vector<OwnershipPlace>();
    const auto& shape = program.types().type(sequence).value;
    if (std::holds_alternative<SliceTypeValue>(shape)) {
        for (const auto& loan : relationships.view().storage_loans) {
            if (loan.holder.empty()) {
                result.push_back(loan.backing);
            }
        }
        index = std::nullopt;
    } else {
        result.assign(storage.begin(), storage.end());
    }
    auto selected_storage = std::vector<OwnershipPlace>();
    for (auto selected : result) {
        if (const auto* owner = std::get_if<OwnedSequenceTypeValue>(&shape)) {
            selected_storage.append_range(
                select_owned_storage(selected, owner->element, index, selection, state)
            );
        } else {
            selected.path.push_back(index);
            selected_storage.push_back(std::move(selected));
        }
    }
    result = std::move(selected_storage);
    std::ranges::sort(result);
    result.erase(std::ranges::unique(result).begin(), result.end());
    return result;
}
