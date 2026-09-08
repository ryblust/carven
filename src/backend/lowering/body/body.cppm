module carven:backend.lowering.body;

import :backend.lowering.context;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.name;
import :backend.target.stmt;
import :semantic.semir;
import std;

struct TargetCallableBodyExit final {
    CallableSignatureID signature;
};

struct TargetTestBodyExit final {};

using TargetBodyExit = std::variant<TargetCallableBodyExit, TargetTestBodyExit>;

struct TargetBodyInputs final {
    std::vector<TargetIdentifier> parameters;
    std::vector<TargetIdentifier> captures;
    TargetBodyExit exit;
};

struct LoweredBody final {
    std::vector<TargetStmt> statements;
    std::vector<bool> referenced_parameters;
    bool uses_test_context;
};

auto lower_body(ModuleLowering& context, BodyID body, TargetBodyInputs inputs) noexcept
    -> LoweredBody;

auto lower_constant_expression(ModuleLowering& context, ConstantID constant) noexcept -> TargetExpr;

auto lower_numeric_enum_case_value_expression(
    ModuleLowering& context,
    EnumCaseID enum_case
) noexcept -> TargetExpr;
