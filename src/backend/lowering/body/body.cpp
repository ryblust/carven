module carven:backend.lowering.body.impl;

import :backend.preparation.body;
import :backend.lowering.body;
import :backend.lowering.context;
import :backend.realization.realizer;
import :semantic.semir.ids;
import std;

auto lower_body(ModuleLowering& context, BodyID body_id, BodyLoweringInputs inputs) noexcept
    -> LoweredBody {
    const auto preparation = BodyPreparation(context.semantic(), body_id);
    return BodyRealizer(context, preparation, std::move(inputs)).finish();
}
