module carven:semantic.analysis.nullability.state.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.nullability.context;
import :semantic.semir.traversal;
import :support.visit;
import std;

auto null_path_contains(
    std::span<const std::uint64_t> outer,
    std::span<const std::uint64_t> inner
) noexcept -> bool {
    return outer.size() <= inner.size() && std::ranges::equal(outer, inner.first(outer.size()));
}

auto join_null_normal(
    std::optional<NullNormal>& target,
    const std::optional<NullNormal>& source
) noexcept -> void {
    if (!source) {
        return;
    }
    if (!target) {
        target = source;
        return;
    }
    std::erase_if(target->state.facts, [&](const auto& entry) noexcept {
        const auto found = source->state.facts.find(entry.first);
        return found == source->state.facts.end() || found->second != entry.second;
    });
    std::erase_if(target->value, [&](const auto& entry) noexcept {
        const auto found = source->value.find(entry.first);
        return found == source->value.end() || found->second != entry.second;
    });
    target->state.exposed.insert(source->state.exposed.begin(), source->state.exposed.end());
}

auto append_null_exits(std::vector<NullExit>& target, std::vector<NullExit> source) noexcept
    -> void {
    target.append_range(std::views::as_rvalue(source));
}

auto project_null_value(const NullValue& value, std::uint64_t index) noexcept -> NullValue {
    auto result = NullValue();
    for (const auto& [path, fact] : value) {
        if (!path.empty() && path.front() == index) {
            result.emplace(NullPath(path.begin() + 1, path.end()), fact);
        }
    }
    return result;
}

NullabilityBodyAnalyzer::NullabilityBodyAnalyzer(
    const SemIRProgram& program,
    const SemIRBody& body,
    AnalysisDiagnostics diagnostics
) noexcept
    : program(program),
      body(body),
      diagnostics(diagnostics) {
    for (const auto entry : body.bindings()) {
        const auto aliases = std::visit(
            Overloaded {
                [&](const ParameterBindingStorage& storage) noexcept {
                    return storage.access == AccessMode::Write
                        || (storage.access == AccessMode::Read
                            && !std::holds_alternative<PointerTypeValue>(
                                program.types().type(entry.value.type).value
                            ));
                },
                [](const CaptureBindingStorage& storage) static noexcept {
                    return storage.mode == CaptureMode::Write;
                },
                [](const OwnerBindingStorage&) static noexcept { return false; },
            },
            entry.value.storage
        );
        if (aliases) {
            input_aliases.insert(entry.id);
        }
    }
}

auto NullabilityBodyAnalyzer::run() noexcept -> void {
    static_cast<void>(region(body.region(), {.facts = {}, .exposed = input_aliases}));
}

auto NullabilityBodyAnalyzer::location(
    const SemanticExpression& source,
    bool enclosing
) const noexcept -> std::optional<NullPlace> {
    if (const auto* binding = std::get_if<SemBinding>(&source.value)) {
        return NullPlace {.root = binding->binding, .path = {}};
    }
    if (const auto* field = std::get_if<SemField>(&source.value)) {
        auto result = location(*field->source, enclosing);
        if (result) {
            result->path.push_back(field->field.field_index);
        }
        return result;
    }
    if (const auto* index = std::get_if<SemIndex>(&source.value)) {
        auto result = location(*index->source, enclosing);
        if (!result) {
            return std::nullopt;
        }
        if (index->index->constant) {
            const auto& constant = program.constants().constant(*index->index->constant);
            if (const auto* integer = std::get_if<IntegerConstant>(&constant.value)) {
                if (const auto value = integer->as_unsigned()) {
                    result->path.push_back(*value);
                    return result;
                }
            }
        }
        return enclosing ? result : std::nullopt;
    }
    // Native projections may write their containing Carven storage, but never
    // supply a stable path on which a non-null proof can be retained.
    if (enclosing) {
        if (const auto* cpp = std::get_if<SemCpp>(&source.value); cpp != nullptr
            && source.category == SemanticValueCategory::Place
            && !cpp->operands.empty()) {
            return location(cpp->operands.front().expression, true);
        }
    }
    return std::nullopt;
}

auto NullabilityBodyAnalyzer::value_at(
    const NullState& state,
    const NullPlace& place
) const noexcept -> NullValue {
    auto result = NullValue();
    for (const auto& [slot, fact] : state.facts) {
        if (slot.root == place.root && null_path_contains(place.path, slot.path)) {
            result.emplace(
                NullPath(
                    slot.path.begin() + static_cast<std::ptrdiff_t>(place.path.size()),
                    slot.path.end()
                ),
                fact
            );
        }
    }
    return result;
}

auto NullabilityBodyAnalyzer::constant_value(const SemanticExpression& source) const noexcept
    -> NullValue {
    if (source.constant
        && std::holds_alternative<NullPointerConstant>(
            program.constants().constant(*source.constant).value
        )) {
        return {{{}, NullFact::Null}};
    }
    return {};
}

auto NullabilityBodyAnalyzer::truth(const SemanticExpression& source) const noexcept
    -> std::optional<bool> {
    if (source.constant) {
        if (const auto* boolean = std::get_if<BooleanConstant>(
                &program.constants().constant(*source.constant).value
            )) {
            return boolean->value;
        }
    }
    return std::nullopt;
}

auto NullabilityBodyAnalyzer::invalidate_exposed(NullState& state) const noexcept -> void {
    std::erase_if(state.facts, [&](const auto& entry) noexcept {
        return state.exposed.contains(entry.first.root) || range_aliases.contains(entry.first.root);
    });
}

auto NullabilityBodyAnalyzer::invalidate(
    NullState& state,
    const std::optional<NullPlace>& place
) const noexcept -> void {
    if (!place || state.exposed.contains(place->root) || range_aliases.contains(place->root)) {
        invalidate_exposed(state);
    }
    if (!place) {
        return;
    }
    std::erase_if(state.facts, [&](const auto& entry) noexcept {
        return entry.first.root == place->root
            && (null_path_contains(place->path, entry.first.path)
                || null_path_contains(entry.first.path, place->path));
    });
}

auto NullabilityBodyAnalyzer::store(
    NullState& state,
    const NullPlace& place,
    const NullValue& value
) const noexcept -> void {
    invalidate(state, place);
    for (const auto& [path, fact] : value) {
        auto target = place;
        target.path.append_range(path);
        state.facts.insert_or_assign(std::move(target), fact);
    }
}

auto NullabilityBodyAnalyzer::expose(
    NullState& state,
    const SemanticExpression& source
) const noexcept -> void {
    if (const auto place = location(source, true)) {
        state.exposed.insert(place->root);
        if (range_aliases.contains(place->root)) {
            state.exposed.insert(range_aliases.begin(), range_aliases.end());
        }
    }
}

auto NullabilityBodyAnalyzer::refine(
    std::optional<NullNormal>& branch,
    const NullPlace& place,
    NullFact fact
) const noexcept -> void {
    if (!branch) {
        return;
    }
    const auto known = branch->state.facts.find(place);
    if (known != branch->state.facts.end() && known->second != fact) {
        branch.reset();
        return;
    }
    branch->state.facts.insert_or_assign(place, fact);
}

auto NullabilityBodyAnalyzer::require_nonnull(
    ProgramOriginID origin,
    const NullValue& value
) noexcept -> void {
    const auto found = value.find({});
    if (found != value.end() && found->second == NullFact::NonNull) {
        return;
    }
    diagnostics.error(
        DiagnosticBuilder(
            DiagnosticCode::PointerNonNull,
            found != value.end() ? "cannot dereference a null address"
                                 : "dereference requires a locally proven non-null address"
        )
            .primary(program.provenance().source_span(origin))
            .note(
                "compare a stable ptr slot with nullptr before dereferencing; snapshot indirect or dynamic pointer values into a local binding"
            )
            .build()
    );
}

auto NullabilityBodyAnalyzer::failures(NullFlow& flow, FailureSetID failures_id) const noexcept
    -> void {
    if (!flow.normal) {
        return;
    }
    for (const auto type : program.failure_sets().failure_set(failures_id).members) {
        flow.exits.push_back({.payload = type, .state = flow.normal->state});
    }
}

auto NullabilityBodyAnalyzer::scan_write(
    NullState& state,
    const SemanticExpression& source
) const noexcept -> void {
    const auto argument = [&](AccessMode access, const SemanticExpression& value) noexcept {
        if (access == AccessMode::Write) {
            expose(state, value);
            invalidate(state, location(value, true));
        }
    };
    std::visit(
        Overloaded {
            [&](const SemTake& value) noexcept { invalidate(state, location(*value.place, true)); },
            [&](const SemCall& value) noexcept {
                for (const auto& item : value.arguments) {
                    argument(item.access, item.expression);
                }
                invalidate_exposed(state);
            },
            [&](const SemCppCall& value) noexcept {
                visit_cpp_operands(value, argument);
                invalidate_exposed(state);
            },
            [&](const SemCpp& value) noexcept {
                visit_cpp_operands(value, argument);
                invalidate_exposed(state);
            },
            [&](const SemClosure& value) noexcept {
                for (const auto& capture : value.captures) {
                    if (capture.mode == CaptureMode::Write) {
                        expose(state, capture.expression);
                    }
                }
            },
            [](const auto&) static noexcept {},
        },
        source.value
    );
}

auto NullabilityBodyAnalyzer::add_range_aliases(const SemRangeLoop& source) noexcept -> void {
    if (source.access != AccessMode::Write) {
        return;
    }
    if (const auto* sequence = std::get_if<SemSequenceRange>(&source.source)) {
        if (const auto place = location(sequence->value, true)) {
            range_aliases.insert(place->root);
        }
    }
    if (source.binding) {
        range_aliases.insert(*source.binding);
    }
}

template<typename Source>
auto NullabilityBodyAnalyzer::scan_writes_impl(NullState& state, const Source& source) noexcept
    -> void {
    const auto previous_aliases = range_aliases;
    visit_semantic_nodes(source, [&](const SemanticStatement& statement) noexcept {
        if (const auto* range = std::get_if<SemRangeLoop>(&statement.value)) {
            add_range_aliases(*range);
        }
    });
    visit_semantic_nodes(
        source,
        Overloaded {
            [&](const SemanticExpression& value) noexcept { scan_write(state, value); },
            [&](const SemanticStatement& value) noexcept {
                if (const auto* assignment = std::get_if<SemAssign>(&value.value)) {
                    invalidate(state, location(assignment->target, true));
                }
                if (const auto* initialize = std::get_if<SemInitialize>(&value.value)) {
                    invalidate(state, NullPlace {.root = initialize->binding, .path = {}});
                }
                if (const auto* loop = std::get_if<SemRangeLoop>(&value.value);
                    loop != nullptr && loop->access == AccessMode::Write) {
                    if (const auto* range = std::get_if<SemSequenceRange>(&loop->source)) {
                        invalidate(state, location(range->value, true));
                    }
                }
            },
        }
    );
    invalidate_exposed(state);
    range_aliases = previous_aliases;
}

auto NullabilityBodyAnalyzer::scan_writes(
    NullState& state,
    const SemanticExpression& source
) noexcept -> void {
    scan_writes_impl(state, source);
}

auto NullabilityBodyAnalyzer::scan_writes(NullState& state, const SemanticRegion& source) noexcept
    -> void {
    scan_writes_impl(state, source);
}

auto check_pointer_nullability(
    const SemIRProgram& program,
    AnalysisDiagnostics diagnostics
) noexcept -> AnalysisResult<void> {
    for (const auto entry : program.bodies().entries()) {
        NullabilityBodyAnalyzer(program, entry.value, diagnostics).run();
    }
    if (const auto failure = diagnostics.failure()) {
        return std::unexpected(*failure);
    }
    return {};
}
