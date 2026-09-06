module carven:semantic.analysis.ownership.state.impl;

import :semantic.analysis.ownership.context;
import std;

namespace ownership {

BodyAnalyzer::BodyAnalyzer(
    BatchAnalyzer& analysis,
    const CallInput& input,
    bool diagnosing
) noexcept
    : analysis(analysis),
      input(input),
      body(analysis.body(input.body_id)),
      draft(analysis.draft),
      facts(analysis.facts_for_body(input.body_id)),
      diagnosing(diagnosing),
      accesses(input.accesses) {
    const auto bind = [&](std::span<const LocalBindingID> bindings,
                          std::span<const CallArgument> values) noexcept {
        for (const auto& [id, value] : std::views::zip(bindings, values)) {
            if (value.alias.has_value()) {
                aliases.emplace(id, *value.alias);
            }
        }
    };
    bind(body.inputs().parameters, input.parameters);
    bind(body.inputs().captures, input.captures);
}
auto BodyAnalyzer::object_type(std::size_t object) const noexcept -> TypeID {
    return object < input.objects.size() ? input.objects[object].type
                                         : facts.locals[object - input.objects.size()].type;
}
auto BodyAnalyzer::object_origin(std::size_t object) const noexcept -> ProgramOriginID {
    return object < input.objects.size() ? input.objects[object].origin
                                         : facts.locals[object - input.objects.size()].origin;
}
auto BodyAnalyzer::temporary(const SemanticExpression& expression) const noexcept -> std::size_t {
    return input.objects.size() + facts.temporaries.at(std::addressof(expression));
}
auto BodyAnalyzer::diagnose(
    DiagnosticCode code,
    std::string message,
    ProgramOriginID origin,
    std::optional<ProgramOriginID> related
) noexcept -> void {
    if (diagnosing) {
        analysis.diagnose(code, std::move(message), origin, related);
    }
}
auto BodyAnalyzer::outlives(std::size_t source, std::size_t destination) const noexcept -> bool {
    if (source < input.objects.size()) {
        return destination >= input.objects.size() || input.outlives[source][destination];
    }
    return destination >= input.objects.size()
        && body.lifetime_regions().outlives(
            facts.locals[source - input.objects.size()].lifetime,
            facts.locals[destination - input.objects.size()].lifetime
        );
}
auto BodyAnalyzer::leave(Flow& flow, LifetimeRegionID lifetime) const noexcept -> void {
    const auto found = facts.lifetime_objects.find(lifetime);
    if (found == facts.lifetime_objects.end()) {
        return;
    }
    const auto release = [&](State& state) noexcept {
        for (const auto object : found->second) {
            state.objects[input.objects.size() + object] = {};
        }
    };
    if (flow.normal.has_value()) {
        release(*flow.normal);
    }
    for (auto& exit : flow.exits) {
        release(exit.state);
    }
}
auto BodyAnalyzer::retain(
    State& state,
    const Relationships& relationships,
    const SemanticExpression& source
) const noexcept -> void {
    const auto found = facts.temporaries.find(std::addressof(source));
    if (found != facts.temporaries.end()) {
        state.objects[input.objects.size() + found->second] =
            {.available = true, .taken = std::nullopt, .relationships = relationships};
    }
}
auto BodyAnalyzer::references(const Relationships& relationships, const State& state) const noexcept
    -> std::vector<Capture> {
    auto result = std::vector<Capture>();
    auto visited = std::flat_set<Place>();
    const auto inspect = [&](this const auto& self, const Relationships& value) noexcept -> void {
        const auto follow = [&](const Place& target) noexcept {
            if (visited.insert(target).second) {
                self(project(state.objects[target.object].relationships, target.path));
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
auto BodyAnalyzer::use(
    const Relationships& relationships,
    const State& state,
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
auto BodyAnalyzer::store(
    State& state,
    const Place& target,
    const Relationships& relationships,
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
    merge_relationships(destination.relationships, nested(relationships, target.path));
}
auto BodyAnalyzer::binding_place(LocalBindingID binding) const noexcept -> Place {
    const auto found = aliases.find(binding);
    return found == aliases.end() ? Place {input.objects.size() + binding.index(), {}}
                                  : found->second;
}
auto BodyAnalyzer::location(const SemanticExpression& source) const noexcept
    -> std::optional<Place> {
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
auto BodyAnalyzer::is_writable(LocalBindingID id) const noexcept -> bool {
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
auto BodyAnalyzer::write_access(const Place& target, ProgramOriginID origin) noexcept -> void {
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
auto BodyAnalyzer::require_available(
    const State& state,
    const Place& target,
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
auto BodyAnalyzer::constant_truth(const SemanticExpression& source) const noexcept
    -> std::optional<bool> {
    if (source.constant.has_value()) {
        const auto constant = draft.constants().constant(*source.constant);
        if (const auto* value = std::get_if<BooleanConstant>(&constant.value)) {
            return value->value;
        }
    }
    return std::nullopt;
}
auto BodyAnalyzer::constant_index(const SemanticExpression& source) const noexcept
    -> std::optional<std::uint64_t> {
    if (source.constant.has_value()) {
        const auto constant = draft.constants().constant(*source.constant);
        if (const auto* value = std::get_if<IntegerConstant>(&constant.value)) {
            return value->as_unsigned();
        }
    }
    return std::nullopt;
}

} // namespace ownership
