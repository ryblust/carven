module carven:semantic.analysis.failure.instantiate.impl;

import :semantic.analysis.failure;
import :support.invariant;
import std;

FailureInstantiation::FailureInstantiation(
    FailureConstraintStore& store,
    FailureSetStoreBuilder& sets,
    FailureInstantiationEnvironment substitutions,
    std::optional<FailureParameterEnvironment> deferred
) noexcept
    : destination(store),
      failure_sets(sets),
      environment(std::move(substitutions)),
      deferred_parameters(std::move(deferred)) {
    if (destination.program_identity != failure_sets.owner()) {
        invariant_violation("failure instantiation inputs belong to different semantic programs");
    }
}

auto FailureInstantiation::independent(ConstructionFailureRef source) noexcept -> bool {
    if (const auto found = independent_sources.find(source); found != independent_sources.end()) {
        return found->second;
    }
    auto pending = std::vector<ConstructionFailureRef> {source};
    auto visited = std::set<ConstructionFailureRef>();
    auto unchanged = true;
    const auto same_members = [&](std::span<const TypeID> members) noexcept {
        return !environment.member || std::ranges::all_of(members, [&](TypeID member) noexcept {
            return environment.member(member) == member;
        });
    };
    while (unchanged && !pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        if (!visited.insert(current).second) {
            continue;
        }
        if (const auto* known = std::get_if<FailureSetID>(&current)) {
            unchanged = same_members(failure_sets.copy(*known).members);
        } else if (const auto* symbolic = std::get_if<GenericFailureID>(&current)) {
            const auto expression = destination.generic_failure_copy(*symbolic);
            unchanged = expression.parameters.empty() && same_members(expression.concrete_members);
            for (const auto& contribution : expression.guards) {
                pending.push_back(contribution.gate);
                pending.push_back(contribution.source);
            }
        } else {
            const auto term = destination.copy(std::get<FailureTermID>(current));
            unchanged = same_members(term.direct_members)
                && same_members(term.excluded_members)
                && (!term.retained_members || same_members(*term.retained_members));
            if (environment.origin) {
                unchanged = unchanged
                    && std::ranges::all_of(term.throw_sites, [&](const auto& site) noexcept {
                                return environment.origin(site.origin) == site.origin;
                            });
            }
            pending.insert(pending.end(), term.inputs.begin(), term.inputs.end());
            for (const auto& contribution : term.guarded_inputs) {
                pending.push_back(contribution.gate);
                pending.push_back(contribution.source);
            }
        }
    }
    independent_sources.emplace(source, unchanged);
    return unchanged;
}

auto FailureInstantiation::substitute(ConstructionFailureRef source) noexcept
    -> ConstructionFailureRef {
    if (const auto found = substituted.find(source); found != substituted.end()) {
        return found->second;
    }
    if (const auto* symbolic = std::get_if<GenericFailureID>(&source)) {
        const auto expression = destination.generic_failure_copy(*symbolic);
        if (expression.parameters.empty() && expression.guards.empty()) {
            auto members = expression.concrete_members;
            if (environment.member) {
                for (auto& member : members) {
                    member = environment.member(member);
                }
            }
            const auto result = failure_sets.intern(std::move(members));
            substituted.emplace(source, result);
            return result;
        }
    }
    if (const auto* term = std::get_if<FailureTermID>(&source); term && deferred_parameters) {
        const auto result = destination.application(*term, *deferred_parameters, failure_sets);
        substituted.emplace(source, result);
        return result;
    }
    if (independent(source)) {
        substituted.emplace(source, source);
        return source;
    }
    if (const auto* known = std::get_if<FailureSetID>(&source)) {
        auto members = failure_sets.copy(*known).members;
        for (auto& member : members) {
            member = environment.member(member);
        }
        const auto result = failure_sets.intern(std::move(members));
        substituted.emplace(source, result);
        return result;
    }
    if (const auto* symbolic = std::get_if<GenericFailureID>(&source)) {
        return substitute_generic(*symbolic);
    }
    return substitute_term(std::get<FailureTermID>(source));
}

auto FailureInstantiation::substitute_term(FailureTermID source) noexcept
    -> ConstructionFailureRef {
    // Reserve before following edges, including guards. Recursive equations then
    // reconnect to this instance's row rather than to the definition's equation.
    const auto result = destination.add_empty_term();
    substituted.emplace(source, result);
    populate_term(source, result);
    return result;
}

auto FailureInstantiation::populate_term(FailureTermID source, FailureTermID result) noexcept
    -> void {
    auto term = destination.copy(source);
    const auto map_members = [&](std::vector<TypeID>& members) noexcept {
        if (environment.member) {
            for (auto& member : members) {
                member = environment.member(member);
            }
        }
        members = destination.normalize_members(std::move(members));
    };
    map_members(term.direct_members);
    map_members(term.excluded_members);
    if (term.retained_members) {
        map_members(*term.retained_members);
    }
    for (auto& input : term.inputs) {
        input = substitute(input);
    }
    for (auto& contribution : term.guarded_inputs) {
        contribution.gate = substitute(contribution.gate);
        contribution.source = substitute(contribution.source);
    }
    for (auto& site : term.throw_sites) {
        if (environment.member) {
            site.member = environment.member(site.member);
        }
        if (environment.origin) {
            site.origin = environment.origin(site.origin);
        }
        destination.require_origin(site.origin);
    }
    for (const auto input : term.inputs) {
        destination.require_source(input);
    }
    for (const auto& contribution : term.guarded_inputs) {
        destination.require_source(contribution.gate);
        destination.require_source(contribution.source);
    }
    destination.term_table.mutate(result) = std::move(term);
}

auto FailureInstantiation::substitute_generic(GenericFailureID source) noexcept
    -> ConstructionFailureRef {
    const auto expression = destination.generic_failure_copy(source);
    auto constants = expression.concrete_members;
    if (environment.member) {
        for (auto& member : constants) {
            member = environment.member(member);
        }
    }
    auto inputs = std::vector<ConstructionFailureRef> {failure_sets.intern(std::move(constants))};
    const auto map_mask = [&](std::vector<TypeID> members) noexcept {
        if (environment.member) {
            for (auto& member : members) {
                member = environment.member(member);
            }
        }
        return destination.normalize_members(std::move(members));
    };
    const auto filter = [&](ConstructionFailureRef input,
                            const GenericFailureMask& mask) noexcept -> ConstructionFailureRef {
        auto members = map_mask(mask.members);
        if (const auto* known = std::get_if<FailureSetID>(&input)) {
            auto selected = std::vector<TypeID>();
            const auto value = failure_sets.copy(*known);
            if (mask.complemented) {
                std::ranges::set_difference(value.members, members, std::back_inserter(selected));
            } else {
                std::ranges::set_intersection(value.members, members, std::back_inserter(selected));
            }
            return failure_sets.intern(std::move(selected));
        }
        if (const auto* symbolic = std::get_if<GenericFailureID>(&input)) {
            return mask.complemented
                ? destination.add_generic_residual(*symbolic, std::move(members))
                : destination.add_generic_intersection(*symbolic, std::move(members));
        }
        return mask.complemented ? destination.add_residual_term(input, std::move(members))
                                 : destination.add_intersection_term(input, std::move(members));
    };
    for (const auto& component : expression.parameters) {
        if (!environment.parameter) {
            invariant_violation(
                "symbolic failure instantiation is missing its parameter environment"
            );
        }
        const auto input = environment.parameter(component.parameter);
        destination.require_source(input);
        inputs.push_back(filter(input, component.mask));
    }
    const auto as_generic = [&](ConstructionFailureRef input) noexcept -> GenericFailureID {
        if (const auto* known = std::get_if<FailureSetID>(&input)) {
            return destination.add_generic_concrete(failure_sets.copy(*known).members);
        }
        return std::get<GenericFailureID>(input);
    };
    for (const auto& contribution : expression.guards) {
        const auto gate = substitute(contribution.gate);
        const auto value = substitute(contribution.source);
        if (const auto* known = std::get_if<FailureSetID>(&gate)) {
            if (!failure_sets.copy(*known).members.empty()) {
                inputs.push_back(value);
            }
        } else if (!std::holds_alternative<FailureTermID>(gate)
                   && !std::holds_alternative<FailureTermID>(value)) {
            inputs.push_back(destination.add_generic_guard(as_generic(gate), as_generic(value)));
        } else {
            const auto guarded = destination.add_empty_term();
            destination.add_guarded_contribution(guarded, gate, value);
            inputs.push_back(guarded);
        }
    }
    auto result = ConstructionFailureRef(inputs.front());
    if (std::ranges::any_of(inputs, [](const auto& input) static noexcept {
            return std::holds_alternative<FailureTermID>(input);
        })) {
        result = destination.add_union_term(std::move(inputs));
    } else if (std::ranges::any_of(inputs, [](const auto& input) static noexcept {
                   return std::holds_alternative<GenericFailureID>(input);
               })) {
        auto symbolic_inputs = std::vector<GenericFailureID>();
        for (const auto input : inputs) {
            symbolic_inputs.push_back(as_generic(input));
        }
        result = destination.add_generic_union(std::move(symbolic_inputs));
    } else {
        auto members = std::vector<TypeID>();
        for (const auto input : inputs) {
            const auto value = failure_sets.copy(std::get<FailureSetID>(input));
            members.insert(members.end(), value.members.begin(), value.members.end());
        }
        result = failure_sets.intern(std::move(members));
    }
    substituted.emplace(source, result);
    return result;
}

auto FailureConstraintStore::parameter(GenericFailureParameter source) noexcept
    -> GenericFailureID {
    return add_generic_parameter(source);
}

auto FailureConstraintStore::reduce_application_argument(
    ConstructionFailureRef source,
    FailureSetStoreBuilder& sets
) noexcept -> ConstructionFailureRef {
    // Declaration dependencies are complete at this boundary. Reduce exact
    // acyclic equations and ordinary union cycles through the shared algebra;
    // other recursive equations retain their original term identity.
    auto active = std::set<ConstructionFailureRef>();
    auto memo = std::map<ConstructionFailureRef, std::optional<GenericFailureID>>();
    const auto visit =
        [&](auto&& self,
            ConstructionFailureRef reference) noexcept -> std::optional<GenericFailureID> {
        if (const auto* generic = std::get_if<GenericFailureID>(&reference)) {
            return *generic;
        }
        if (const auto* concrete = std::get_if<FailureSetID>(&reference)) {
            return add_generic_concrete(sets.copy(*concrete).members);
        }
        if (const auto found = memo.find(reference); found != memo.end()) {
            return found->second;
        }
        if (!active.insert(reference).second) {
            return std::nullopt;
        }
        const auto term = std::get<FailureTermID>(reference);
        auto result = std::optional<GenericFailureID>();
        if (const auto found = applications_by_result.find(term);
            found != applications_by_result.end()) {
            const auto application = applications[found->second];
            if (const auto body = self(self, application.source)) {
                auto parameters = std::set<GenericFailureParameter>();
                auto pending = std::vector<GenericFailureID> {*body};
                auto seen = std::set<GenericFailureID>();
                while (!pending.empty()) {
                    const auto current = pending.back();
                    pending.pop_back();
                    if (!seen.insert(current).second) {
                        continue;
                    }
                    const auto expression = generic_failure_copy(current);
                    for (const auto& component : expression.parameters) {
                        parameters.insert(component.parameter);
                    }
                    for (const auto& guard : expression.guards) {
                        pending.push_back(guard.gate);
                        pending.push_back(guard.source);
                    }
                }
                auto environment = FailureParameterEnvironment();
                auto closed = true;
                for (const auto parameter : parameters) {
                    const auto actual = application.environment.find(parameter);
                    if (actual == application.environment.end()) {
                        continue;
                    }
                    const auto reduced = self(self, actual->second);
                    if (!reduced) {
                        closed = false;
                        break;
                    }
                    environment.emplace(parameter, *reduced);
                }
                if (closed) {
                    auto instance = FailureInstantiation(
                        *this,
                        sets,
                        {
                            .parameter = [this, environment](
                                             GenericFailureParameter parameter
                                         ) noexcept -> ConstructionFailureRef {
                                const auto found = environment.find(parameter);
                                return found == environment.end()
                                    ? ConstructionFailureRef(add_generic_parameter(parameter))
                                    : found->second;
                            },
                            .member = {},
                            .origin = {},
                        }
                    );
                    const auto substituted = instance.substitute(*body);
                    if (const auto* concrete = std::get_if<FailureSetID>(&substituted)) {
                        result = add_generic_concrete(sets.copy(*concrete).members);
                    } else {
                        result = std::get<GenericFailureID>(substituted);
                    }
                }
            }
        } else {
            const auto equation = copy(term);
            auto inputs =
                std::vector<GenericFailureID> {add_generic_concrete(equation.direct_members)};
            auto sources = equation.inputs;
            if (equation.excluded_members.empty()
                && !equation.retained_members
                && equation.guarded_inputs.empty()) {
                // Signature/body equality introduces harmless union cycles.
                // Their least solution is the union of all reachable ordinary
                // rows. Applications, masks and guards remain exact boundaries.
                auto pending = std::vector<FailureTermID> {term};
                auto seen = std::set<FailureTermID>();
                sources.clear();
                while (!pending.empty()) {
                    const auto current = pending.back();
                    pending.pop_back();
                    if (!seen.insert(current).second) {
                        continue;
                    }
                    const auto row = copy(current);
                    inputs.push_back(add_generic_concrete(row.direct_members));
                    for (const auto input : row.inputs) {
                        const auto* child = std::get_if<FailureTermID>(&input);
                        if (child && !applications_by_result.contains(*child)) {
                            const auto nested = copy(*child);
                            if (nested.excluded_members.empty()
                                && !nested.retained_members
                                && nested.guarded_inputs.empty()) {
                                pending.push_back(*child);
                                continue;
                            }
                        }
                        sources.push_back(input);
                    }
                }
            }
            auto closed = true;
            for (const auto input : sources) {
                const auto value = self(self, input);
                if (!value) {
                    closed = false;
                    break;
                }
                inputs.push_back(*value);
            }
            if (closed) {
                for (const auto& guard : equation.guarded_inputs) {
                    const auto gate = self(self, guard.gate);
                    const auto value = self(self, guard.source);
                    if (!gate || !value) {
                        closed = false;
                        break;
                    }
                    inputs.push_back(add_generic_guard(*gate, *value));
                }
            }
            if (closed) {
                auto value = add_generic_union(std::move(inputs));
                if (equation.retained_members) {
                    value = add_generic_intersection(value, *equation.retained_members);
                }
                value = add_generic_residual(value, equation.excluded_members);
                result = value;
            }
        }
        active.erase(reference);
        memo.emplace(reference, result);
        return result;
    };
    const auto reduced = visit(visit, source);
    if (!reduced) {
        return source;
    }
    const auto expression = generic_failure_copy(*reduced);
    if (expression.parameters.empty() && expression.guards.empty()) {
        return sets.intern(expression.concrete_members);
    }
    return *reduced;
}

auto FailureConstraintStore::normalize_application_environment(
    FailureParameterEnvironment environment,
    FailureSetStoreBuilder& sets
) noexcept -> FailureParameterEnvironment {
    for (auto iterator = environment.begin(); iterator != environment.end();) {
        if (closing_applications) {
            iterator->second = reduce_application_argument(iterator->second, sets);
        }
        auto identity = false;
        if (const auto* symbolic = std::get_if<GenericFailureID>(&iterator->second)) {
            const auto expression = generic_failure_copy(*symbolic);
            identity = expression.concrete_members.empty()
                && expression.guards.empty()
                && expression.parameters.size() == 1uz
                && expression.parameters[0].parameter == iterator->first
                && expression.parameters[0].mask.complemented
                && expression.parameters[0].mask.members.empty();
        }
        if (identity) {
            iterator = environment.erase(iterator);
        } else {
            ++iterator;
        }
    }
    return environment;
}

auto FailureConstraintStore::application(
    FailureTermID source,
    FailureParameterEnvironment environment,
    FailureSetStoreBuilder& sets
) noexcept -> FailureTermID {
    if (const auto found = applications_by_result.find(source);
        found != applications_by_result.end()) {
        const auto existing = applications[found->second];
        auto composition = FailureInstantiation(
            *this,
            sets,
            {
                .parameter = [this, environment](GenericFailureParameter parameter) noexcept
                    -> ConstructionFailureRef {
                    const auto bound = environment.find(parameter);
                    return bound == environment.end()
                        ? ConstructionFailureRef(add_generic_parameter(parameter))
                        : bound->second;
                },
                .member = [](TypeID type) static noexcept { return type; },
                .origin = [](ProgramOriginID origin) static noexcept { return origin; },
            },
            environment
        );
        for (const auto& [parameter, value] : existing.environment) {
            environment.insert_or_assign(parameter, composition.substitute(value));
        }
        source = existing.source;
    }
    environment = normalize_application_environment(std::move(environment), sets);
    if (environment.empty()) {
        return source;
    }
    const auto key = std::pair(source, environment);
    if (const auto found = application_cache.find(key); found != application_cache.end()) {
        return found->second;
    }
    const auto height = [&](const FailureParameterEnvironment& arguments) noexcept {
        auto memo = std::map<GenericFailureID, std::size_t>();
        const auto visit = [&](auto&& self, GenericFailureID id) noexcept -> std::size_t {
            if (const auto found = memo.find(id); found != memo.end()) {
                return found->second;
            }
            auto depth = 0uz;
            for (const auto& guard : generic_failure_copy(id).guards) {
                depth = std::max(
                    depth,
                    1uz + std::max(self(self, guard.gate), self(self, guard.source))
                );
            }
            memo.emplace(id, depth);
            return depth;
        };
        auto result = 0uz;
        for (const auto& [parameter, value] : arguments) {
            static_cast<void>(parameter);
            if (const auto* generic = std::get_if<GenericFailureID>(&value)) {
                result = std::max(result, visit(visit, *generic));
            }
        }
        return result;
    };
    constexpr auto term_atoms = [](const FailureParameterEnvironment& arguments) static noexcept {
        auto terms = std::set<FailureTermID>();
        for (const auto& [parameter, value] : arguments) {
            static_cast<void>(parameter);
            if (const auto* term = std::get_if<FailureTermID>(&value)) {
                terms.insert(*term);
            }
        }
        return terms;
    };
    for (auto ancestor = active_application; ancestor; ancestor = applications[*ancestor].parent) {
        const auto& previous = applications[*ancestor];
        if (previous.source != source) {
            continue;
        }
        const auto previous_terms = term_atoms(previous.environment);
        const auto candidate_terms = term_atoms(environment);
        // Union, masks and permutation have finite normalized domains. A guard
        // constructor on a recursive flow must stabilize before making a new
        // key. Opaque terms can circulate, but cannot become fresh recursive
        // atoms whose finite substitution has not been established.
        if (height(environment) > height(previous.environment)
            || !std::ranges::includes(previous_terms, candidate_terms)) {
            growth = environment.begin()->first;
            return previous.result;
        }
    }
    const auto result = add_empty_term();
    application_cache.emplace(key, result);
    applications_by_result.emplace(result, applications.size());
    applications.push_back(
        {.source = source,
         .environment = std::move(environment),
         .result = result,
         .complete = false,
         .parent = active_application}
    );
    return result;
}

auto FailureConstraintStore::complete_applications(
    FailureSetStoreBuilder& sets,
    std::span<const ConstructionFailureRef> roots
) noexcept -> bool {
    if (growth) {
        return false;
    }
    closing_applications = true;
    auto selected = std::set<ConstructionFailureRef>();
    auto queued = std::set<std::size_t>();
    auto work = std::deque<std::size_t>();
    const auto enqueue = [&](std::size_t index) noexcept {
        if (!applications[index].complete && queued.insert(index).second) {
            work.push_back(index);
        }
    };
    const auto select = [&](std::span<const ConstructionFailureRef> entries) noexcept {
        auto pending = std::vector<ConstructionFailureRef>(entries.begin(), entries.end());
        auto visited = std::set<ConstructionFailureRef>();
        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            if (!visited.insert(current).second) {
                continue;
            }
            selected.insert(current);
            if (const auto* term = std::get_if<FailureTermID>(&current)) {
                if (const auto found = applications_by_result.find(*term);
                    found != applications_by_result.end()) {
                    enqueue(found->second);
                }
            }
            pending.append_range(dependencies(current));
        }
    };
    if (roots.empty()) {
        for (auto index = 0uz; index < applications.size(); ++index) {
            enqueue(index);
        }
    } else {
        select(roots);
    }
    while (!work.empty()) {
        const auto index = work.front();
        work.pop_front();
        auto application = applications[index];
        application.environment =
            normalize_application_environment(std::move(application.environment), sets);
        applications[index].environment = application.environment;
        if (!application.environment.empty()) {
            application_cache.try_emplace(
                std::pair(application.source, application.environment),
                application.result
            );
        }
        const auto previous_count = applications.size();
        // Persist only semantic IDs and a value environment. The temporary
        // session is rebuilt against the current owner after draft moves.
        auto instance = FailureInstantiation(
            *this,
            sets,
            {
                .parameter = [this, environment = application.environment](
                                 GenericFailureParameter parameter
                             ) noexcept -> ConstructionFailureRef {
                    const auto bound = environment.find(parameter);
                    return bound == environment.end()
                        ? ConstructionFailureRef(add_generic_parameter(parameter))
                        : bound->second;
                },
                .member = [](TypeID type) static noexcept { return type; },
                .origin = [](ProgramOriginID origin) static noexcept { return origin; },
            },
            application.environment
        );
        instance.substituted.emplace(application.source, application.result);
        active_application = index;
        instance.populate_term(application.source, application.result);
        active_application.reset();
        if (growth) {
            closing_applications = false;
            return false;
        }
        applications[index].complete = true;
        if (roots.empty()) {
            for (auto next = previous_count; next < applications.size(); ++next) {
                enqueue(next);
            }
        } else {
            // Composition may reuse an earlier cached application. Reachability,
            // rather than allocation order, determines the selected worklist.
            const auto entries = std::array<ConstructionFailureRef, 1> {application.result};
            select(entries);
        }
    }
    for (const auto& application : applications) {
        if (!application.complete && (roots.empty() || selected.contains(application.result))) {
            invariant_violation(
                "failure dependency closure retained a pending selected application"
            );
        }
    }
    closing_applications = false;
    return true;
}

auto FailureConstraintStore::dependencies(ConstructionFailureRef source) const noexcept
    -> std::vector<ConstructionFailureRef> {
    auto result = std::vector<ConstructionFailureRef>();
    if (const auto* term = std::get_if<FailureTermID>(&source)) {
        if (const auto application = applications_by_result.find(*term);
            application != applications_by_result.end()) {
            const auto& value = applications[application->second];
            result.push_back(value.source);
            for (const auto& [parameter, actual] : value.environment) {
                static_cast<void>(parameter);
                result.push_back(actual);
            }
        }
        const auto value = copy(*term);
        result.append_range(value.inputs);
        for (const auto& guard : value.guarded_inputs) {
            result.push_back(guard.gate);
            result.push_back(guard.source);
        }
    } else if (const auto* generic = std::get_if<GenericFailureID>(&source)) {
        const auto value = generic_failure_copy(*generic);
        for (const auto& guard : value.guards) {
            result.push_back(guard.gate);
            result.push_back(guard.source);
        }
    }
    return result;
}

auto FailureConstraintStore::growing_parameter() const noexcept
    -> std::optional<GenericFailureParameter> {
    return growth;
}
