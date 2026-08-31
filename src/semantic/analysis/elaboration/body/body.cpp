module carven:semantic.analysis.elaboration.body.impl;

import :diagnostics.code;
import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.stmt;
import :semantic.analysis.elaboration.types;
import :semantic.hir.access;
import :semantic.hir.symbol;
import :support.invariant;
import std;

auto ReturnTypeInference::record(ReturnObservation observation) noexcept -> void {
    values.push_back(std::move(observation));
}

auto ReturnTypeInference::observations() const noexcept -> std::span<const ReturnObservation> {
    return values;
}

auto ValueBranchState::enter(std::uint32_t loop_depth) noexcept -> void {
    if (entering_loop_depth.has_value()) {
        invariant_violation("value branch state cannot be entered more than once");
    }
    entering_loop_depth = loop_depth;
}

auto ValueBranchState::reject_transfer() noexcept -> void {
    transfer_rejected = true;
}

auto ValueBranchState::rejected_transfer() const noexcept -> bool {
    return transfer_rejected;
}

auto ValueBranchState::contains_loop(std::uint32_t loop_depth) const noexcept -> bool {
    return entering_loop_depth.has_value() && loop_depth > *entering_loop_depth;
}

auto root_body_control(std::optional<HIRTypeID> expected_return) noexcept -> BodyControl {
    return {
        .expected_return = expected_return,
        .loop_depth = 0,
        .permits_rethrow = false,
        .return_inference = nullptr,
        .value_branch = nullptr,
    };
}

auto inferred_body_control(ReturnTypeInference& inference) noexcept -> BodyControl {
    return {
        .expected_return = std::nullopt,
        .loop_depth = 0,
        .permits_rethrow = false,
        .return_inference = std::addressof(inference),
        .value_branch = nullptr,
    };
}

auto inside_loop(BodyControl context) noexcept -> BodyControl {
    ++context.loop_depth;
    return context;
}

auto inside_value_branch(BodyControl context, ValueBranchState& state) noexcept -> BodyControl {
    state.enter(context.loop_depth);
    context.value_branch = std::addressof(state);
    return context;
}

auto inside_catch(BodyControl context) noexcept -> BodyControl {
    context.permits_rethrow = true;
    return context;
}

BodyElaborator::BodyElaborator(ModuleAnalysis& value) noexcept
    : module_analysis(value),
      scopes(value.builder()) {}

auto BodyElaborator::elaborate_function(
    const ASTFunctionDecl& function,
    CallableID callable
) noexcept -> HIRBody {
    auto& builder = module_analysis.builder();
    if (builder.callable(callable).parameters.size() != function.parameters.size()) {
        invariant_violation("function signature does not match its source declaration");
    }
    const auto function_scope = scopes.enter_scope();
    const auto function_scope_id = scopes.current_scope();
    auto parameters = std::vector<HIRParameter>();
    parameters.reserve(function.parameters.size());
    for (auto index = 0uz; index < function.parameters.size(); ++index) {
        const auto& parameter = function.parameters[index];
        const auto parameter_type = builder.callable(callable).parameters[index].type;
        const auto access = access_mode(parameter.access);
        auto target = HIRBindingTarget {HIRDiscardBindingTarget {}};
        if (const auto* named = std::get_if<ASTNamedBindingTarget>(&parameter.target)) {
            const auto name = module_analysis.spelling(named->name_span);
            const auto symbol = module_analysis.append_symbol({
                .name = builder.intern_string(name),
                .module_id = std::nullopt,
                .role = SemanticSymbolRole::Parameter,
                .parent = std::nullopt,
            });
            if (!scopes.bind(name, symbol)) {
                module_analysis.emit(
                    named->name_span,
                    "duplicate parameter name",
                    DiagnosticCode::NameDuplicateParameter
                );
            }
            builder.record_symbol_lint_candidate(symbol, module_analysis.origin(named->name_span));
            set_symbol_type(module_analysis, symbol, parameter_type);
            builder.define_symbol_binding(
                symbol,
                access == HIRAccessMode::Read
                    ? SemanticBindingRole::ReadAlias
                    : (access == HIRAccessMode::Write ? SemanticBindingRole::WriteAlias
                                                      : SemanticBindingRole::Owner),
                access == HIRAccessMode::Write
            );
            target = HIRNamedBindingTarget {.symbol = symbol};
        }
        parameters.push_back({
            .access = access,
            .target = std::move(target),
            .type = parameter_type,
            .origin = module_analysis.origin(parameter.span),
        });
    }
    const auto result = builder.callable(callable).result;
    return {
        .parameters = std::move(parameters),
        .scope = function_scope_id,
        .root = build_block(module_analysis, scopes, function.body, root_body_control(result)),
    };
}

auto BodyElaborator::elaborate_test(ASTBlockID body, HIRTypeID result) noexcept -> HIRBlockID {
    return build_block(module_analysis, scopes, body, root_body_control(result));
}
