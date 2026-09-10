module carven:backend.realization.constant;

import :backend.lowering.context;
import :backend.target.expr;
import :semantic.semir;
import std;

enum class RealizationLiteralContext { Exact, TargetTyped };

auto constant_expression(
    ModuleLowering& context,
    ConstantID constant,
    RealizationLiteralContext use = RealizationLiteralContext::Exact
) noexcept -> TargetExpr;

auto typed_integer_expression(
    ModuleLowering& context,
    const IntegerConstant& value,
    TypeID type,
    RealizationLiteralContext use = RealizationLiteralContext::Exact
) noexcept -> TargetExpr;

auto enum_case_index(const SemIRProgram& semantic, EnumCaseID case_id) noexcept -> std::size_t;

auto enum_case_expression(
    ModuleLowering& context,
    EnumCaseID case_id,
    std::vector<TargetExpr> payload
) noexcept -> TargetExpr;

auto numeric_enum_case_value_expression(ModuleLowering& context, EnumCaseID enum_case) noexcept
    -> TargetExpr;
