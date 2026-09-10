module carven:backend.realization.body.impl;

import :backend.construction;
import :backend.lowering.context;
import :backend.realization.body;
import :backend.realization.realizer;
import std;

auto realize_body(
    ModuleLowering& context,
    const BodyConstruction& construction,
    BodyRealizationInputs inputs
) noexcept -> LoweredBody {
    return BodyRealizer(context, construction, std::move(inputs)).finish();
}
