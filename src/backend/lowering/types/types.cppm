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

auto materialize_parameter_type(
    TargetModuleLowerer& context,
    const TargetCallParameter& parameter
) noexcept -> TargetTypeID;

auto reference_type(
    TargetModuleLowerer& context,
    TargetTypeID type,
    bool const_qualified = false,
    bool rvalue = false
) noexcept -> TargetTypeID;

auto make_failure_carrier(
    TargetModuleLowerer& context,
    HIRTypeID result,
    FailureSetID failure_set
) noexcept -> FailureCarrierDescriptor;

auto materialize_failure_carrier(TargetModuleLowerer& context, TargetCarrierShapeID shape) noexcept
    -> FailureCarrierDescriptor;

auto classify_carrier_conversion(
    const TargetModuleLowerer& context,
    const FailureCarrierDescriptor& source,
    const FailureCarrierDescriptor& destination
) noexcept -> TargetCarrierConversion;

auto lower_type(TargetModuleLowerer& context, HIRTypeID id) noexcept -> TargetTypeID;

auto lower_literal(
    const TargetModuleLowerer& context,
    const HIRLiteralValue& literal,
    HIRTypeID type
) noexcept -> TargetLiteralValue;

auto is_char_type(const TargetModuleLowerer& context, HIRTypeID id) noexcept -> bool;
auto is_integer_type(const TargetModuleLowerer& context, HIRTypeID id) noexcept -> bool;
auto is_void_type(const TargetModuleLowerer& context, HIRTypeID id) noexcept -> bool;
