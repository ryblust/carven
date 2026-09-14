module carven:backend.lowering.body.impl;

import :backend.construction;
import :backend.lowering.body;
import :backend.lowering.context;
import :backend.realization.realizer;
import :semantic.semir;
import std;

auto lower_body(ModuleLowering& context, BodyID body_id, BodyLoweringInputs inputs) noexcept
    -> LoweredBody {
    const auto construction = construct_body(context.semantic(), body_id);
    return BodyRealizer(context, construction, std::move(inputs)).finish();
}
