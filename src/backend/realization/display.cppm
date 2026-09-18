module carven:backend.realization.display;

import :backend.lowering.context;
import :backend.target.expr;
import :semantic.semir.ids;

// The wrapper borrows a stabilized operand until its synchronous consumer completes.
auto realize_display(ModuleLowering& context, TypeID type, TargetExpr value) noexcept -> TargetExpr;
