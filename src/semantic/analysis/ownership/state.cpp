module carven:semantic.analysis.ownership.state.impl;

import :semantic.analysis.ownership.context;
import :semantic.semir.evaluation;
import std;

OwnershipBodyAnalyzer::OwnershipBodyAnalyzer(
    OwnershipBatchAnalyzer& analysis,
    const OwnershipCallInput& input,
    bool diagnosing
) noexcept
    : analysis(analysis),
      input(input),
      body(analysis.body(input.body_id)),
      program(analysis.program),
      facts(analysis.facts_for_body(input.body_id)),
      diagnosing(diagnosing),
      accesses(input.accesses),
      storage_readers(input.storage_readers) {
    const auto bind = [&](std::span<const LocalBindingID> bindings,
                          std::span<const OwnershipCallArgument> values) noexcept {
        for (const auto& [id, value] : std::views::zip(bindings, values)) {
            const auto* parameter = std::get_if<ParameterBindingStorage>(&body.binding(id).storage);
            if (value.capture_holder) {
                capture_holders.emplace(id, *value.capture_holder);
            } else if (parameter != nullptr
                       && parameter->access == AccessMode::Read
                       && analysis.contents(body.binding(id).type).read_borrows_storage()) {
                auto storage = value.storage;
                if (storage.empty() && value.alias) {
                    storage.push_back(*value.alias);
                }
                selected_storage.emplace(id, std::move(storage));
            } else if (!value.storage.empty()) {
                selected_storage.emplace(id, value.storage);
            } else if (value.alias.has_value()) {
                aliases.emplace(id, *value.alias);
            }
        }
    };
    bind(body.inputs().parameters, input.parameters);
    bind(body.inputs().captures, input.captures);
}

auto OwnershipBodyAnalyzer::object_type(std::size_t object) const noexcept -> TypeID {
    return object < input.objects.size() ? input.objects[object].type
                                         : facts.locals[object - input.objects.size()].type;
}

auto OwnershipBodyAnalyzer::object_origin(std::size_t object) const noexcept -> ProgramOriginID {
    return object < input.objects.size() ? input.objects[object].origin
                                         : facts.locals[object - input.objects.size()].origin;
}

auto OwnershipBodyAnalyzer::diagnose(
    DiagnosticCode code,
    std::string message,
    ProgramOriginID origin,
    std::optional<ProgramOriginID> related
) noexcept -> void {
    if (diagnosing) {
        analysis.diagnose(code, std::move(message), origin, related);
    }
}

auto OwnershipBodyAnalyzer::outlives(std::size_t source, std::size_t destination) const noexcept
    -> bool {
    if (source < input.objects.size()) {
        return destination >= input.objects.size() || input.outlives[source][destination];
    }
    return destination >= input.objects.size()
        && body.lifetime_regions().outlives(
            facts.locals[source - input.objects.size()].lifetime,
            facts.locals[destination - input.objects.size()].lifetime
        );
}

auto OwnershipBodyAnalyzer::full_expression_storage(std::size_t object) const noexcept -> bool {
    return object >= input.objects.size()
        && body.lifetime_regions().region(facts.locals[object - input.objects.size()].lifetime).kind
        == LifetimeRegionKind::FullExpression;
}

auto OwnershipBodyAnalyzer::leave(OwnershipFlow& flow, LifetimeRegionID lifetime) noexcept -> void {
    const auto found = facts.lifetime_objects.find(lifetime);
    if (found == facts.lifetime_objects.end()) {
        return;
    }
    const auto release = [&](OwnershipState& state) noexcept {
        for (const auto object : found->second) {
            state.objects[input.objects.size() + object] = {};
        }
    };
    const auto check = [&](const OwnershipRelationships& value,
                           const OwnershipState& state) noexcept {
        for (const auto& loan : value.storage_loans) {
            if (!state.objects[loan.backing.object].available) {
                diagnose(
                    DiagnosticCode::AccessBorrowConflict,
                    "borrowed view escapes the lifetime of its backing",
                    loan.origin,
                    object_origin(loan.backing.object)
                );
            }
        }
    };
    if (flow.normal.has_value()) {
        release(flow.normal->state);
        check(flow.normal->value, flow.normal->state);
    }
    for (auto& exit : flow.exits) {
        release(exit.state);
        exit.payload.visit([&](const auto& payload) noexcept {
            if constexpr (requires { payload.value; }) {
                check(payload.value, exit.state);
            }
        });
    }
}

auto OwnershipBodyAnalyzer::retain(
    OwnershipState& state,
    const OwnershipRelationships& relationships,
    const SemanticExpression& source
) const noexcept -> void {
    const auto found = facts.temporaries.find(std::addressof(source));
    if (found != facts.temporaries.end()) {
        state.objects[input.objects.size() + found->second] = {
            .available = true,
            .taken = std::nullopt,
            .relationships = relationships,
            .modified = false
        };
    }
}

auto OwnershipBodyAnalyzer::references(
    const OwnershipRelationships& relationships,
    const OwnershipState& state
) const noexcept -> std::vector<OwnershipCapture> {
    auto result = std::vector<OwnershipCapture>();
    auto visited = std::flat_set<OwnershipPlace>();

    struct PendingRelationships final {
        OwnershipRelationships value;
        std::size_t capture;
        std::size_t loan;
        std::size_t storage;
    };

    auto pending = std::vector<PendingRelationships> {{relationships, 0uz, 0uz, 0uz}};
    while (!pending.empty()) {
        auto& current = pending.back();
        auto target = std::optional<OwnershipPlace>();
        if (current.capture < current.value.captures.size()) {
            const auto& capture = current.value.captures[current.capture++];
            if (!std::ranges::contains(result, capture)) {
                result.push_back(capture);
            }
            target = capture.target;
        } else if (current.loan < current.value.callable_loans.size()) {
            target = current.value.callable_loans[current.loan++].backing;
        } else if (current.storage < current.value.storage_loans.size()) {
            target = current.value.storage_loans[current.storage++].backing;
        } else {
            pending.pop_back();
            continue;
        }
        if (target && visited.insert(*target).second) {
            pending.push_back(
                {project_relationships(state.objects[target->object].relationships, target->path),
                 0uz,
                 0uz,
                 0uz}
            );
        }
    }
    return result;
}

auto OwnershipBodyAnalyzer::tracked_borrows(
    const OwnershipRelationships& value,
    const OwnershipState& state
) const noexcept -> bool {
    auto pending = std::vector<OwnershipRelationships> {value};
    auto visited = std::flat_set<OwnershipPlace>();
    while (!pending.empty()) {
        const auto current = std::move(pending.back());
        pending.pop_back();
        if (!current.callable_loans.empty() || !current.captures.empty()) {
            return true;
        }
        for (const auto& loan : current.storage_loans) {
            if (visited.insert(loan.backing).second) {
                pending.push_back(project_relationships(
                    state.objects[loan.backing.object].relationships,
                    loan.backing.path
                ));
            }
        }
    }
    return false;
}

auto OwnershipBodyAnalyzer::use(
    const OwnershipRelationships& relationships,
    const OwnershipState& state,
    ProgramOriginID origin,
    bool direct
) noexcept -> void {
    for (const auto& loan : relationships.storage_loans) {
        if (!state.objects[loan.backing.object].available) {
            diagnose(
                DiagnosticCode::AccessBorrowConflict,
                "borrowed view has unavailable or expired backing",
                origin,
                loan.origin
            );
        }
    }
    for (const auto& loan : relationships.callable_loans) {
        if (loan.backing.has_value() && !state.objects[loan.backing->object].available) {
            diagnose(
                DiagnosticCode::AccessBorrowConflict,
                "callable view has unavailable or expired backing",
                origin,
                loan.origin
            );
        }
        if (loan.direct_only && !direct) {
            diagnose(
                DiagnosticCode::TypeCallableViewEscape,
                "capturing temporary view is only valid as a direct call argument",
                origin
            );
        }
    }
    for (const auto& capture : references(relationships, state)) {
        if (!state.objects[capture.target.object].available) {
            diagnose(
                DiagnosticCode::AccessBorrowConflict,
                "closure has unavailable or expired Write capture",
                origin,
                capture.origin
            );
        }
    }
}

auto OwnershipBodyAnalyzer::store(
    OwnershipState& state,
    const OwnershipPlace& target,
    const OwnershipRelationships& relationships,
    ProgramOriginID origin,
    bool definite
) noexcept -> void {
    use(relationships, state, origin);
    check_storage_write(state, target, origin);
    for (const auto& loan : relationships.storage_loans) {
        if (loan.backing.object == target.object || !outlives(loan.backing.object, target.object)) {
            diagnose(
                DiagnosticCode::AccessBorrowConflict,
                "view holder outlives its backing or creates a self reference",
                origin,
                loan.origin
            );
        }
    }
    for (const auto& loan : relationships.callable_loans) {
        if (loan.backing.has_value() && !outlives(loan.backing->object, target.object)) {
            diagnose(
                DiagnosticCode::TypeCallableViewEscape,
                "callable storage outlives its backing",
                origin,
                loan.origin
            );
        }
    }
    for (const auto& capture : references(relationships, state)) {
        if (!outlives(capture.target.object, target.object)) {
            diagnose(
                DiagnosticCode::AccessBorrowConflict,
                "closure storage outlives its Write capture",
                origin,
                capture.origin
            );
        }
    }
    auto& destination = state.objects[target.object];
    destination.modified = true;
    const auto singleton =
        definite && (target.object >= input.objects.size() || !input.objects[target.object].many);
    if (singleton && target.path.empty()) {
        destination = {
            .available = true,
            .taken = std::nullopt,
            .relationships = relationships,
            .modified = true
        };
        return;
    }
    if (singleton && std::ranges::all_of(target.path, [](const auto& part) static noexcept {
            return part.has_value();
        })) {
        const auto replaced = [&](const auto& row) noexcept {
            return row.holder.size() >= target.path.size()
                && std::equal(target.path.begin(), target.path.end(), row.holder.begin());
        };
        std::erase_if(destination.relationships.callable_loans, replaced);
        std::erase_if(destination.relationships.captures, replaced);
        std::erase_if(destination.relationships.storage_loans, replaced);
    }
    merge_relationships(destination.relationships, nest_relationships(relationships, target.path));
}

auto OwnershipBodyAnalyzer::binding_place(LocalBindingID binding) const noexcept -> OwnershipPlace {
    const auto found = aliases.find(binding);
    return found == aliases.end() ? OwnershipPlace {input.objects.size() + binding.index(), {}}
                                  : found->second;
}

auto OwnershipBodyAnalyzer::binding_places(
    LocalBindingID binding,
    const OwnershipState& state
) const noexcept -> std::vector<OwnershipPlace> {
    if (const auto found = capture_holders.find(binding); found != capture_holders.end()) {
        const auto& holder = found->second;
        const auto value =
            project_relationships(state.objects[holder.object].relationships, holder.path);
        auto result = std::vector<OwnershipPlace>();
        for (const auto& capture : value.captures) {
            if (capture.holder.empty()) {
                result.push_back(capture.target);
            }
        }
        std::ranges::sort(result);
        result.erase(std::ranges::unique(result).begin(), result.end());
        return result;
    }
    if (const auto found = selected_storage.find(binding); found != selected_storage.end()) {
        return found->second;
    }
    return {binding_place(binding)};
}

auto OwnershipBodyAnalyzer::is_writable(LocalBindingID id) const noexcept -> bool {
    return body.binding(id).storage.visit(
        Overloaded {
            [](const OwnerBindingStorage& value) static noexcept { return value.writable; },
            [](const ParameterBindingStorage& value) static noexcept {
                return value.access == AccessMode::Write;
            },
            [](const CaptureBindingStorage& value) static noexcept {
                return value.mode == CaptureMode::Write;
            },
        }
    );
}

auto OwnershipBodyAnalyzer::write_access(
    const OwnershipPlace& target,
    ProgramOriginID origin
) noexcept -> void {
    for (const auto& access : accesses) {
        if (access.stable && overlaps(access.place, target)) {
            diagnose(
                DiagnosticCode::AccessOperationConflict,
                "Write conflicts with stable match selection",
                origin,
                object_origin(access.place.object)
            );
        }
    }
}

auto OwnershipBodyAnalyzer::require_available(
    const OwnershipState& state,
    const OwnershipPlace& target,
    ProgramOriginID origin
) noexcept -> void {
    if (!state.objects[target.object].available) {
        diagnose(
            DiagnosticCode::AccessUnavailable,
            "binding is unavailable before initialization or after Take",
            origin,
            state.objects[target.object].taken
        );
    }
}

auto OwnershipBodyAnalyzer::constant_truth(const SemanticExpression& source) const noexcept
    -> std::optional<bool> {
    return known_boolean(program, source);
}

auto OwnershipBodyAnalyzer::constant_index(const SemanticExpression& source) const noexcept
    -> std::optional<std::uint64_t> {
    if (source.constant.has_value()) {
        const auto& constant = program.constants().constant(*source.constant);
        if (const auto* value = std::get_if<IntegerConstant>(&constant.value)) {
            return value->as_unsigned();
        }
    }
    return std::nullopt;
}

auto OwnershipBodyAnalyzer::protect_storage(const OwnershipRelationships& value) noexcept -> void {
    storage_readers
        .insert(storage_readers.end(), value.storage_loans.begin(), value.storage_loans.end());
}

auto OwnershipBodyAnalyzer::restore_storage_readers(std::size_t count) noexcept -> void {
    storage_readers.erase(
        storage_readers.begin() + static_cast<std::ptrdiff_t>(count),
        storage_readers.end()
    );
}

auto OwnershipBodyAnalyzer::check_storage_write(
    const OwnershipState& state,
    const OwnershipPlace& target,
    ProgramOriginID origin
) noexcept -> void {
    const auto check = [&](std::span<const OwnershipStorageLoan> loans) noexcept {
        for (const auto& loan : loans) {
            if (overlaps(loan.backing, target)) {
                diagnose(
                    DiagnosticCode::AccessBorrowConflict,
                    "operation conflicts with a live borrowed view",
                    origin,
                    loan.origin
                );
            }
        }
    };
    check(storage_readers);
    for (const auto& object : state.objects) {
        check(object.relationships.storage_loans);
    }
}
