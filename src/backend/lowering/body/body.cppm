module carven:backend.lowering.body;

import :backend.lowering.context;
import :backend.realization.body;
import :semantic.semir;

auto lower_body(ModuleLowering& context, BodyID body_id, BodyRealizationInputs inputs) noexcept
    -> LoweredBody;
