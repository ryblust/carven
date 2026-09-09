module carven:semantic.analysis.ownership.state.impl;

import :semantic.analysis.ownership.context;
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
      accesses(input.accesses) {
    const auto bind = [&](std::span<const LocalBindingID> bindings,
                          std::span<const OwnershipCallArgument> values) noexcept {
        for (const auto& [id, value] : std::views::zip(bindings, values)) {
            if (value.alias.has_value()) {
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

auto OwnershipBodyAnalyzer::temporary(const SemanticExpression& expression) const noexcept
    -> std::size_t {
    return input.objects.size() + facts.temporaries.at(std::addressof(expression));
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

auto OwnershipBodyAnalyzer::leave(OwnershipFlow& flow, LifetimeRegionID lifetime) const noexcept
    -> void {
    const auto found = facts.lifetime_objects.find(lifetime);
    if (found == facts.lifetime_objects.end()) {
        return;
    }
    const auto release = [&](OwnershipState& state) noexcept {
        for (const auto object : found->second) {
            state.objects[input.objects.size() + object] = {};
        }
    };
    if (flow.normal.has_value()) {
        release(flow.normal->state);
    }
    for (auto& exit : flow.exits) {
        release(exit.state);
    }
}

auto OwnershipBodyAnalyzer::retain(
    OwnershipState& state,
    const OwnershipRelationships& relationships,
    const SemanticExpression& source
) const noexcept -> void {
    const auto found = facts.temporaries.find(std::addressof(source));
    if (found != facts.temporaries.end()) {
        state.objects[input.objects.size() + found->second] =
            {.available = true, .taken = std::nullopt, .relationships = relationships};
    }
}

auto OwnershipBodyAnalyzer::references(
    const OwnershipRelationships& relationships,
    const OwnershipState& state
) const noexcept -> std::vector<OwnershipCapture> {
    auto result = std::vector<OwnershipCapture>();
    auto visited = std::flat_set<OwnershipPlace>();
    const auto inspect = [&](this const auto& self,
                             const OwnershipRelationships& value) noexcept -> void {
        const auto follow = [&](const OwnershipPlace& target) noexcept {
            if (visited.insert(target).second) {
                self(
                    project_relationships(state.objects[target.object].relationships, target.path)
                );
            }
        };
        for (const auto& capture : value.captures) {
            if (!std::ranges::contains(result, capture)) {
                result.push_back(capture);
            }
            follow(capture.target);
        }
        for (const auto& loan : value.loans) {
            if (loan.backing.has_value()) {
                follow(*loan.backing);
            }
        }
    };
    inspect(relationships);
    return result;
}

auto OwnershipBodyAnalyzer::use(
    const OwnershipRelationships& relationships,
    const OwnershipState& state,
    ProgramOriginID origin,
    bool direct
) noexcept -> void {
    for (const auto& loan : relationships.loans) {
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
    ProgramOriginID origin
) noexcept -> void {
    use(relationships, state, origin);
    for (const auto& loan : relationships.loans) {
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
    if (target.path.empty()) {
        destination = {.available = true, .taken = std::nullopt, .relationships = relationships};
        return;
    }
    if (std::ranges::all_of(target.path, [](const auto& part) static noexcept {
            return part.has_value();
        })) {
        const auto replaced = [&](const auto& row) noexcept {
            return row.holder.size() >= target.path.size()
                && std::equal(target.path.begin(), target.path.end(), row.holder.begin());
        };
        std::erase_if(destination.relationships.loans, replaced);
        std::erase_if(destination.relationships.captures, replaced);
    }
    merge_relationships(destination.relationships, nest_relationships(relationships, target.path));
}

auto OwnershipBodyAnalyzer::binding_place(LocalBindingID binding) const noexcept -> OwnershipPlace {
    const auto found = aliases.find(binding);
    return found == aliases.end() ? OwnershipPlace {input.objects.size() + binding.index(), {}}
                                  : found->second;
}

auto OwnershipBodyAnalyzer::location(const SemanticExpression& source) const noexcept
    -> std::optional<OwnershipPlace> {
    if (const auto* foreign = std::get_if<SemCpp>(&source.value);
        foreign != nullptr && source.category == SemanticValueCategory::Place) {
        return location(foreign->operands.front().expression);
    }
    if (const auto* binding = std::get_if<SemBinding>(&source.value)) {
        return binding_place(binding->binding);
    }
    if (const auto* field = std::get_if<SemField>(&source.value)) {
        auto result = location(*field->source);
        if (result.has_value()) {
            result->path.push_back(field->field.field_index);
        }
        return result;
    }
    if (const auto* index = std::get_if<SemIndex>(&source.value)) {
        auto result = location(*index->source);
        if (result.has_value()) {
            result->path.push_back(constant_index(*index->index));
        }
        return result;
    }
    return std::nullopt;
}

auto OwnershipBodyAnalyzer::is_writable(LocalBindingID id) const noexcept -> bool {
    return std::visit(
        Overloaded {
            [](const OwnerBindingStorage& value) static noexcept { return value.writable; },
            [](const ParameterBindingStorage& value) static noexcept {
                return value.access == AccessMode::Write;
            },
            [](const CaptureBindingStorage& value) static noexcept {
                return value.mode == CaptureMode::Write;
            },
        },
        body.binding(id).storage
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
    if (source.constant.has_value()) {
        const auto constant = program.constants().constant(*source.constant);
        if (const auto* value = std::get_if<BooleanConstant>(&constant.value)) {
            return value->value;
        }
    }
    return std::nullopt;
}

auto OwnershipBodyAnalyzer::constant_index(const SemanticExpression& source) const noexcept
    -> std::optional<std::uint64_t> {
    if (source.constant.has_value()) {
        const auto constant = program.constants().constant(*source.constant);
        if (const auto* value = std::get_if<IntegerConstant>(&constant.value)) {
            return value->as_unsigned();
        }
    }
    return std::nullopt;
}
