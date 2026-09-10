module carven:backend.realization.body;

import :backend.construction;
import :backend.lowering.context;
import :backend.target.name;
import :backend.target.stmt;
import :semantic.semir;
import std;

struct CallableBodyExit final {
    CallableSignatureID signature;
};

struct TestBodyExit final {};

using BodyExit = std::variant<CallableBodyExit, TestBodyExit>;

struct BodyRealizationInputs final {
    std::vector<TargetIdentifier> parameters;
    std::vector<TargetIdentifier> captures;
    BodyExit exit;
};

struct LoweredBody final {
    std::vector<TargetStmt> statements;
    std::vector<bool> referenced_parameters;
    bool uses_test_context;
};

auto realize_body(
    ModuleLowering& context,
    const BodyConstruction& construction,
    BodyRealizationInputs inputs
) noexcept -> LoweredBody;
