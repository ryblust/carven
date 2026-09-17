module carven:backend.lowering.body;

import :backend.lowering.context;
import :backend.target.name;
import :backend.target.stmt;
import :semantic.semir.ids;
import std;

struct CallableBodyExit final {
    CallableID callable_id;
};

struct TestBodyExit final {};

using BodyExit = std::variant<CallableBodyExit, TestBodyExit>;

struct BodyLoweringInputs final {
    std::vector<TargetIdentifier> parameters;
    std::vector<TargetIdentifier> captures;
    BodyExit exit;
};

struct LoweredBody final {
    std::vector<TargetStmt> statements;
    std::vector<bool> referenced_parameters;
};

auto lower_body(ModuleLowering& context, BodyID body_id, BodyLoweringInputs inputs) noexcept
    -> LoweredBody;
