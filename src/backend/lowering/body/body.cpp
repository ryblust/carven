module carven:backend.lowering.body.impl;

import :backend.construction;
import :backend.lowering.body;
import :backend.lowering.context;
import :backend.realization.body;
import :semantic.semir;
import std;

auto lower_body(ModuleLowering& context, BodyID body_id, BodyRealizationInputs inputs) noexcept
    -> LoweredBody {
    const auto construction = construct_body(context.semantic(), body_id);
    return realize_body(context, construction, std::move(inputs));
}
