module carven:backend.lowering.names;

import :backend.generation.names;
import :backend.target.name;
import :semantic.hir.expr;
import :semantic.hir.ids;

class TargetModuleLowerer;

auto symbol_name(TargetModuleLowerer& context, SymbolID symbol) noexcept -> TargetName;
auto symbol_reference_name(TargetModuleLowerer& context, SymbolID symbol) noexcept -> TargetName;
auto symbol_identifier(TargetModuleLowerer& context, SymbolID symbol) noexcept -> TargetIdentifier;
auto binding_identifier(
    TargetModuleLowerer& context,
    SymbolID symbol,
    const EvaluationEffect& initializer_effect
) noexcept -> TargetIdentifier;
auto symbol_is_used(const TargetModuleLowerer& context, SymbolID symbol) noexcept -> bool;
auto member_name(TargetModuleLowerer& context, const HIRMemberExpr& member) noexcept
    -> TargetMemberName;
