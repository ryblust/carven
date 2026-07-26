module carven:backend.lowering.types;

import :backend.lowering.program;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.name;
import :backend.target.symbol;
import :semantic.hir.access;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :semantic.hir.type;
import std;

auto named_type(TargetModuleLowerer& context, TargetName name, bool constant = false) noexcept
    -> TargetTypeID;
auto intrinsic_type(
    TargetModuleLowerer& context,
    TargetSymbol symbol,
    bool constant = false
) noexcept -> TargetTypeID;
auto failure_carrier_type(
    TargetModuleLowerer& context,
    HIRTypeID result,
    FailureSetID failure_set
) noexcept -> TargetTypeID;
auto test_control_type(TargetModuleLowerer& context, TargetTypeID result) noexcept -> TargetTypeID;
auto parameter_type(
    TargetModuleLowerer& context,
    HIRAccessMode access,
    HIRTypeID source_type,
    TargetTypeID type
) noexcept -> TargetTypeID;
auto reference_type(
    TargetModuleLowerer& context,
    TargetTypeID type,
    bool const_qualified = false,
    bool rvalue = false
) noexcept -> TargetTypeID;
enum class CarrierConversion {
    Identity,
    Widen,
};
auto make_failure_carrier(
    TargetModuleLowerer& context,
    HIRTypeID result,
    FailureSetID failure_set
) noexcept -> FailureCarrierDescriptor;
auto classify_carrier_conversion(
    const TargetModuleLowerer& context,
    const FailureCarrierDescriptor& source,
    const FailureCarrierDescriptor& destination
) noexcept -> CarrierConversion;
auto lower_type(TargetModuleLowerer& context, HIRTypeID id) noexcept -> TargetTypeID;
auto lower_literal(
    const TargetModuleLowerer& context,
    const HIRLiteralValue& literal,
    HIRTypeID type
) noexcept -> TargetLiteralValue;
auto builtin_symbol(HIRBuiltinType type) noexcept -> TargetSymbol;
auto is_integer_type(const TargetModuleLowerer& context, HIRTypeID id) noexcept -> bool;
auto is_void_type(const TargetModuleLowerer& context, HIRTypeID id) noexcept -> bool;
auto is_foreign_type(const TargetModuleLowerer& context, HIRTypeID id) noexcept -> bool;
