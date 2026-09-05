module carven:backend.lowering.body.impl;

import :backend.lowering.body.lowerer;
import :backend.lowering.body;
import :backend.lowering.context;
import :backend.target.stmt;
import :semantic.semir;
import std;

auto lower_body(ModuleLowering& context, BodyID body, TargetBodyInputs inputs) noexcept
    -> LoweredBody {
    return body_lowering::BodyLowerer(context, body, std::move(inputs)).finish();
}
