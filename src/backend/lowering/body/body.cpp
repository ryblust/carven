module carven:backend.lowering.body.impl;

import :backend.lowering.body;
import :backend.lowering.context;
import :backend.preparation.body;
import :backend.realization.realizer;
import :semantic.semir.decl;
import :semantic.semir.ids;
import :semantic.semir.type;
import std;

auto lower_body(ModuleLowering& context, BodyID body_id, BodyLoweringInputs inputs) noexcept
    -> LoweredBody {
    const auto* callable = std::get_if<CallableBodyExit>(&inputs.exit);
    const auto execution = callable != nullptr
        ? context.semantic()
              .callable_signatures()
              .signature(
                  context.semantic().declarations().callable(callable->callable_id).signature
              )
              .execution
        : std::get<TestBodyExit>(inputs.exit).execution == TestExecution::Asynchronous
        ? CallableExecutionKind::Async
        : CallableExecutionKind::Synchronous;
    const auto preparation = BodyPreparation(context.semantic(), body_id, execution);
    return BodyRealizer(context, preparation, std::move(inputs)).finish();
}
