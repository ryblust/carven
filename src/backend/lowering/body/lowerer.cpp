module carven:backend.lowering.body.lowerer.impl;

import :backend.generation.names;
import :backend.lowering.body.decl;
import :backend.lowering.body.lowerer;
import :backend.lowering.context;
import :backend.target.stmt;
import :backend.target.origin;
import :backend.target.symbol;
import :semantic.semir.traversal;
import :semantic.semir;
import :support.invariant;
import std;

BodyLowerer::BodyLowerer(
    ModuleLowering& source_context,
    BodyID id,
    TargetBodyInputs target_inputs
) noexcept
    : context(source_context),
      body(context.semantic().bodies().body(id)),
      inputs(std::move(target_inputs)),
      names(context.make_callable_name_allocator()),
      parameter_bindings(body.inputs().parameters.begin(), body.inputs().parameters.end()),
      capture_bindings(body.inputs().captures.begin(), body.inputs().captures.end()) {
    if (inputs.parameters.size() != parameter_bindings.size()
        || inputs.captures.size() != capture_bindings.size()) {
        invariant_violation("target inputs do not match semantic body inputs");
    }
    for (auto index = 0uz; index < parameter_bindings.size(); ++index) {
        binding_names.emplace(parameter_bindings[index], inputs.parameters[index]);
        names.reserve(inputs.parameters[index].spelling());
        names.reserve(inputs.parameters[index].spelling(), callable_scope);
    }
    for (auto index = 0uz; index < capture_bindings.size(); ++index) {
        binding_names.emplace(capture_bindings[index], inputs.captures[index]);
        names.reserve(inputs.captures[index].spelling());
        names.reserve(inputs.captures[index].spelling(), callable_scope);
    }
    visit_semantic_nodes(body.region(), [&](const SemanticExpression& expression) noexcept {
        const auto* take = std::get_if<SemTake>(&expression.value);
        if (take == nullptr) {
            return;
        }
        const auto* binding = std::get_if<SemBinding>(&take->place->value);
        if (binding == nullptr) {
            invariant_violation("take source is not a complete owner binding");
        }
        taken_bindings.insert(binding->binding);
    });
    for (const auto binding : body.bindings()) {
        if (!binding_names.contains(binding.id)) {
            const auto preferred =
                names.source(context.semantic().provenance().spelling(binding.value.name));
            binding_names.emplace(
                binding.id,
                names.local_symbol(preferred.spelling(), binding.id.index(), callable_scope)
            );
        }
    }
}

auto BodyLowerer::finish() noexcept -> LoweredBody {
    auto statements = region(body.region(), LoweringDiscardResult {});

    for (const auto exit : statements.exits().targets) {
        if (exit.identity != 0
            || (exit.kind != LoweringExitKind::FunctionReturn
                && exit.kind != LoweringExitKind::Failure
                && exit.kind != LoweringExitKind::Test
                && exit.kind != LoweringExitKind::Unreachable)) {
            invariant_violation("body contains an unreceived control exit");
        }
    }
    auto completed = std::move(statements).finish();
    auto referenced_parameters =
        finish_body_declarations(completed, inputs.parameters, inputs.captures);
    return {
        .statements = std::move(completed),
        .referenced_parameters = std::move(referenced_parameters),
        .uses_test_context = uses_test_context
    };
}

auto BodyLowerer::region(const SemanticRegion& source, LoweringResultDestination result) noexcept
    -> LoweringStmtBuilder {
    auto statements = LoweringStmtBuilder();
    const auto append_from = [&](this const auto& self,
                                 std::size_t index,
                                 LoweringStmtBuilder& destination) noexcept -> void {
        for (; index < source.statements.size() && destination.continues(); ++index) {
            const auto& item = source.statements[index];
            if (const auto* initialization = std::get_if<SemInitialize>(&item.value);
                initialization != nullptr
                && can_extend_branch_scope(initialization->initializer)
                && external_exits(initialization->initializer)) {
                structured_expression(
                    initialization->initializer,
                    LoweringConsumeResult {
                        .consume =
                            [&](LoweringResult value, LoweringStmtBuilder& branch) noexcept {
                                auto binding = LoweringStmtBuilder();
                                declare_binding(
                                    initialization->binding,
                                    require_expression(std::move(value)),
                                    binding
                                );
                                binding.attribute(
                                    TargetSourceExpansionAttribution {
                                        .origin = target_source_origin(
                                            context.semantic().provenance(),
                                            item.origin
                                        )
                                    }
                                );
                                branch.append(std::move(binding));
                                self(index + 1uz, branch);
                            }
                    },
                    destination
                );
                return;
            }
            static_cast<void>(destination.accept(statement(item)));
        }
        if (destination.continues()) {
            if (source.result.has_value()) {
                result_expression(*source.result, result, destination);
            } else {
                deliver_result(LoweringCompleted {}, result, destination);
            }
        }
    };
    append_from(0uz, statements);
    return statements;
}
